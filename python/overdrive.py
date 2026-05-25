"""
Overdrive effect — diode hard clipping (DS-1 / RAT style) with configurable
distortion modes, 4× oversampling, pre/de-emphasis, mid/presence EQ, and
pick-sensitivity envelope follower.

Parameters
----------
drive        0.0–1.0   Pre-amp gain, mapped to 1x–100x.
tone         0.0–1.0   High-frequency blend: 0 = dark (LP), 1 = bright (HP).
level        0.0–1.0   Output volume scalar.
mode         str       Waveshaper: hardclip | softclip | foldback | asymmetric | bitcrush
clip_shape   str       Pre/de-emphasis: flat | midfocus | brightfocus   (13c)
mid_db       float     Peaking EQ at 800 Hz, −6 to +10 dB; default 0    (13d)
presence_db  float     High shelf at 4 kHz, 0 to +8 dB; default 0       (13d)
pick_sens    bool      Envelope-based gain reduction for pick sensitivity (13e)
bias         float     DC offset before waveshaper, −0.5 to +0.5; default 0 (16a)
"""

import argparse
import sys
from pathlib import Path

import numpy as np
from scipy.signal import butter, sosfilt, sosfiltfilt

sys.path.insert(0, str(Path(__file__).parent))
from oversampler import L as OS_L, downsample, upsample
from utils import DEFAULT_SR, load_wav, plot_frequency_response, plot_waveform, save_wav

MODES = ("hardclip", "softclip", "foldback", "asymmetric", "bitcrush")
CLIP_SHAPES = ("flat", "midfocus", "brightfocus")


# ---------------------------------------------------------------------------
# Filter design helpers
# ---------------------------------------------------------------------------

def _sos_hp(sr: int, cutoff: float = 100.0):
    return butter(1, cutoff / (sr / 2), btype="high", output="sos")


def _sos_tone(sr: int, crossover: float = 3500.0):
    lp = butter(2, crossover / (sr / 2), btype="low",  output="sos")
    hp = butter(2, crossover / (sr / 2), btype="high", output="sos")
    return lp, hp


def _sos_shelf(sr: int, fc: float, gain_db: float, shelf_type: str = "high") -> np.ndarray:
    """First-order shelving filter: gain_db boost/cut above (high) or below (low) fc."""
    A = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * np.pi * fc / sr
    cos_w = np.cos(w0)
    alpha = np.sin(w0) / 2.0 * np.sqrt((A + 1.0 / A) * (1.0 / 0.707 - 1.0) + 2.0)
    if shelf_type == "high":
        b0 =     A * ((A + 1) + (A - 1) * cos_w + 2 * np.sqrt(A) * alpha)
        b1 = -2 * A * ((A - 1) + (A + 1) * cos_w)
        b2 =     A * ((A + 1) + (A - 1) * cos_w - 2 * np.sqrt(A) * alpha)
        a0 =         (A + 1) - (A - 1) * cos_w + 2 * np.sqrt(A) * alpha
        a1 =     2 * ((A - 1) - (A + 1) * cos_w)
        a2 =         (A + 1) - (A - 1) * cos_w - 2 * np.sqrt(A) * alpha
    else:
        b0 =     A * ((A + 1) - (A - 1) * cos_w + 2 * np.sqrt(A) * alpha)
        b1 = 2 * A * ((A - 1) - (A + 1) * cos_w)
        b2 =     A * ((A + 1) - (A - 1) * cos_w - 2 * np.sqrt(A) * alpha)
        a0 =         (A + 1) + (A - 1) * cos_w + 2 * np.sqrt(A) * alpha
        a1 =    -2 * ((A - 1) + (A + 1) * cos_w)
        a2 =         (A + 1) + (A - 1) * cos_w - 2 * np.sqrt(A) * alpha
    return np.array([[b0 / a0, b1 / a0, b2 / a0, 1.0, a1 / a0, a2 / a0]])


def _sos_peaking(sr: int, fc: float, gain_db: float, bw_oct: float = 1.5) -> np.ndarray:
    """Biquad peaking (parametric) EQ."""
    A = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * np.pi * fc / sr
    alpha = np.sin(w0) * np.sinh(np.log(2) / 2.0 * bw_oct * w0 / np.sin(w0))
    b0 = 1.0 + alpha * A
    b1 = -2.0 * np.cos(w0)
    b2 = 1.0 - alpha * A
    a0 = 1.0 + alpha / A
    a1 = -2.0 * np.cos(w0)
    a2 = 1.0 - alpha / A
    return np.array([[b0 / a0, b1 / a0, b2 / a0, 1.0, a1 / a0, a2 / a0]])


# ---------------------------------------------------------------------------
# Waveshapers
# ---------------------------------------------------------------------------

def _wavefold(x: np.ndarray, threshold: float = 1.0) -> np.ndarray:
    """Triangle-wave fold: maps any x into [-T, T] via reflection."""
    T = threshold
    m = (x + T) % (4.0 * T)
    return T - np.abs(m - 2.0 * T)


def _bitcrush(x: np.ndarray, drive: float) -> np.ndarray:
    """Hard-clip to [-1,1] then quantize: bits = 16 − round(drive·14)."""
    x = np.clip(x, -1.0, 1.0)
    bits = max(2, round(16 - drive * 14))
    step = 2.0 / (2 ** bits)
    return np.clip(np.round(x / step) * step, -1.0, 1.0)


def _poly_hardclip(x: np.ndarray) -> np.ndarray:
    """Chebyshev polynomial knee + hard limit.

    (3x − x³)/2 blends from linear through the transition zone and reaches
    exactly ±1 at |x|=1; above that, hard-clip to ±1.
    Symmetric — no diode asymmetry (that lives in the Asymmetric mode).
    """
    bounded = np.clip(x, -1.0, 1.0)
    return (3.0 * bounded - bounded ** 3) / 2.0


def _tanh_softclip(x: np.ndarray, g: float) -> np.ndarray:
    """Normalised tanh: tanh(g·x_pre) / tanh(g) bounded to ±1.

    x here is the signal AFTER the pre-amp gain stage (x = g * x_pre).
    Equivalent form: tanh(x) / tanh(g), normalised so unity input → unity
    output at the knee (|x|=g maps to ±1).
    """
    scale = float(np.tanh(g))
    if scale < 1e-6:
        return x
    return np.tanh(x) / scale


def _asymmetric_clip(x: np.ndarray, g: float) -> np.ndarray:
    """Silicon(+)/germanium(−) two-stage soft clipper.

    Positive half: tanh(x)/tanh(g)  — harder, models silicon diode
    Negative half: atan(x)*(2/π)    — softer, models germanium diode
    Produces strong even-order harmonics.
    """
    scale = float(np.tanh(g))
    pos = np.tanh(x) / (scale if scale > 1e-6 else 1.0)
    neg = np.arctan(x) * (2.0 / np.pi)
    return np.where(x >= 0.0, pos, neg)


# ---------------------------------------------------------------------------
# Envelope follower (13e)
# ---------------------------------------------------------------------------

def _envelope_follow(x: np.ndarray, sr: int,
                     attack_ms: float = 1.0,
                     release_ms: float = 100.0) -> np.ndarray:
    """Peak detector with asymmetric attack/release. Returns envelope in [0, 1]."""
    att = np.exp(-1.0 / (sr * attack_ms * 0.001))
    rel = np.exp(-1.0 / (sr * release_ms * 0.001))
    env = np.zeros(len(x))
    state = 0.0
    for i, s in enumerate(np.abs(x)):
        c = att if s > state else rel
        state = c * state + (1.0 - c) * s
        env[i] = state
    return env


# ---------------------------------------------------------------------------
# Main process function
# ---------------------------------------------------------------------------

def process(
    signal: np.ndarray,
    sr: int = DEFAULT_SR,
    drive: float = 0.5,
    tone: float = 0.5,
    level: float = 0.8,
    mode: str = "hardclip",
    clip_shape: str = "flat",     # 13c
    mid_db: float = 0.0,          # 13d
    presence_db: float = 0.0,     # 13d
    pick_sens: bool = True,        # 13e
    bias: float = 0.0,             # 16a
) -> np.ndarray:
    g = 1.0 + drive * 99.0        # pre-amp gain factor (1–100×)
    sig = signal.astype(np.float64)

    # 13e: envelope follower tracks raw input at original rate (matches C++)
    if pick_sens:
        env = _envelope_follow(sig, sr)
        # Map peak envelope (0–1) to gain multiplier (1.0 → 0.708 = −3 dB max reduction)
        gain_env = 1.0 - 0.292 * np.clip(env, 0.0, 1.0)
    else:
        gain_env = np.ones(len(sig))

    # 13a: upsample raw signal to 4× rate (DC block happens at OS rate, not before)
    x_os = upsample(sig)
    sr_os = sr * OS_L

    # Upsample gain_env to 4× by repeating each value L times
    gain_env_os = np.repeat(gain_env, OS_L)[: len(x_os)]

    # DC block at oversampled rate
    x_os = sosfilt(_sos_hp(sr_os), x_os)

    # 13c: pre-emphasis (before gain stage)
    if clip_shape == "midfocus":
        x_os = sosfilt(_sos_shelf(sr_os, 700.0, +6.0, "high"), x_os)
    elif clip_shape == "brightfocus":
        x_os = sosfilt(_sos_shelf(sr_os, 3000.0, +6.0, "high"), x_os)

    # Apply gain (with envelope-based reduction)
    x_os = x_os * g * gain_env_os

    # 16a: bias — DC offset applied immediately before the waveshaper
    if bias != 0.0:
        x_os = x_os + bias

    # 13b: waveshaper
    if mode == "hardclip":
        x_os = _poly_hardclip(x_os)
    elif mode == "softclip":
        x_os = _tanh_softclip(x_os, g)
    elif mode == "foldback":
        x_os = _wavefold(x_os, threshold=1.0)
    elif mode == "asymmetric":
        x_os = _asymmetric_clip(x_os, g)
    elif mode == "bitcrush":
        x_os = _bitcrush(x_os, drive)
    else:
        raise ValueError(f"Unknown mode: {mode!r}. Choose from: {MODES}")

    # 13c: de-emphasis (matched, after clip)
    if clip_shape == "midfocus":
        x_os = sosfilt(_sos_shelf(sr_os, 700.0, -6.0, "high"), x_os)
    elif clip_shape == "brightfocus":
        x_os = sosfilt(_sos_shelf(sr_os, 3000.0, -6.0, "high"), x_os)

    # 13a: downsample back to original rate
    x = downsample(x_os)
    x = x[: len(signal)]  # trim to exact input length

    # Tone blend: LP + HP via scipy butter (at original rate)
    lp_sos, hp_sos = _sos_tone(sr)
    x = (1.0 - tone) * sosfilt(lp_sos, x) + tone * sosfilt(hp_sos, x)

    # 13d: mid peaking EQ
    if abs(mid_db) > 0.01:
        x = sosfilt(_sos_peaking(sr, 800.0, mid_db, bw_oct=1.5), x)

    # 13d: presence high shelf
    if abs(presence_db) > 0.01:
        x = sosfilt(_sos_shelf(sr, 4000.0, presence_db, "high"), x)

    return (x * level).astype(np.float32)


def main() -> None:
    parser = argparse.ArgumentParser(description="Overdrive — configurable waveshaper")
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--drive",       type=float, default=0.5)
    parser.add_argument("--tone",        type=float, default=0.5)
    parser.add_argument("--level",       type=float, default=0.8)
    parser.add_argument("--mode",        choices=MODES, default="hardclip")
    parser.add_argument("--clip-shape",  choices=CLIP_SHAPES, default="flat")
    parser.add_argument("--mid-db",      type=float, default=0.0)
    parser.add_argument("--presence-db", type=float, default=0.0)
    parser.add_argument("--bias",          type=float, default=0.0)
    parser.add_argument("--no-pick-sens", action="store_true")
    parser.add_argument("--plot",        action="store_true")
    args = parser.parse_args()

    signal, sr = load_wav(args.input)
    out = process(signal, sr,
                  drive=args.drive, tone=args.tone, level=args.level,
                  mode=args.mode, clip_shape=args.clip_shape,
                  mid_db=args.mid_db, presence_db=args.presence_db,
                  pick_sens=not args.no_pick_sens, bias=args.bias)
    save_wav(args.output, out, sr)
    print(f"Written: {args.output}")

    if args.plot:
        plot_waveform(signal, sr, title="Input")
        plot_waveform(out, sr, title=f"Overdrive [{args.mode}] drive={args.drive}")
        plot_frequency_response(signal, sr, title="Input spectrum")
        plot_frequency_response(out, sr, title=f"Overdrive [{args.mode}] spectrum")


if __name__ == "__main__":
    main()
