"""
Speaker cabinet IR design — minimum-phase FIR for several cabinet types.

Cabinet types
-------------
  1x12  (default) — 1×12 open-back:
      Unity gain below resonance; +6 dB peak at 120 Hz (Q=2);
      −6 dB/octave rolloff above 4 kHz.
  4x12           — 4×12 closed-back:
      +6 dB peak at 80 Hz (Q=2); −3 dB/octave rolloff above 3 kHz.
  combo          — 1×12 combo:
      +6 dB peak at 180 Hz (Q=2); −6 dB/octave rolloff above 5 kHz.

Algorithm
---------
  Minimum-phase reconstruction via real cepstrum:
    1. Build target magnitude on a 512-point frequency grid
    2. Log-cepstrum → apply minimum-phase lifter → reconstruct
    3. Truncate to N_TAPS = 256; normalise to |H(1 kHz)| = 1.0

Usage
-----
  # Generate all C++ coefficient headers:
  uv run --project python python/cabinet_ir.py --generate-header
  uv run --project python python/cabinet_ir.py --generate-header --type 4x12
  uv run --project python python/cabinet_ir.py --generate-header --type combo

  # Process a WAV file:
  uv run --project python python/cabinet_ir.py in.wav out.wav [--type 1x12|4x12|combo]
"""

import argparse
import sys
from pathlib import Path
from textwrap import wrap

import numpy as np
from scipy.signal import fftconvolve

sys.path.insert(0, str(Path(__file__).parent))
from utils import DEFAULT_SR, load_wav, save_wav

N_TAPS = 256
_FFT_SIZE = 512  # >= 2*N_TAPS so the minimum-phase reconstruction has room

# Per-type target response parameters.
# rolloff_pow: exponent for HF rolloff; 1.0 = −6 dB/oct, 0.5 = −3 dB/oct.
CABINET_CONFIGS: dict[str, dict] = {
    "1x12": {
        "description": "1×12 open-back",
        "f_res": 120.0, "peak_db": 6.0, "Q": 2.0,
        "f_cut": 4000.0, "rolloff_pow": 1.0,
        "header": "CabinetIR_data.h",           # backward-compatible name
        "var_prefix": "cabinet_ir",
    },
    "4x12": {
        "description": "4×12 closed-back",
        "f_res": 80.0, "peak_db": 6.0, "Q": 2.0,
        "f_cut": 3000.0, "rolloff_pow": 0.5,    # −3 dB/oct
        "header": "CabinetIR_data_4x12.h",
        "var_prefix": "cabinet_ir_4x12",
    },
    "combo": {
        "description": "1×12 combo",
        "f_res": 180.0, "peak_db": 6.0, "Q": 2.0,
        "f_cut": 5000.0, "rolloff_pow": 1.0,
        "header": "CabinetIR_data_combo.h",
        "var_prefix": "cabinet_ir_combo",
    },
}


def _target_magnitude(freqs: np.ndarray, f_res: float = 120.0,
                       f_cut: float = 4000.0, peak_db: float = 6.0,
                       Q: float = 2.0, rolloff_pow: float = 1.0) -> np.ndarray:
    """Scalar magnitude response for a speaker cabinet at each frequency."""
    mag = np.ones(len(freqs), dtype=np.float64)

    # Resonance peak at f_res — Lorentzian profile
    peak_lin = 10.0 ** (peak_db / 20.0)
    bw = f_res / Q
    for i, f in enumerate(freqs):
        if f > 0:
            mag[i] *= 1.0 + (peak_lin - 1.0) / (1.0 + ((f - f_res) / bw) ** 2)

    # HF rolloff above f_cut: rolloff_pow=1.0 → −6 dB/oct, 0.5 → −3 dB/oct
    for i, f in enumerate(freqs):
        if f > f_cut:
            mag[i] *= (f_cut / f) ** rolloff_pow

    return mag


def design_ir(n_taps: int = N_TAPS, sr: float = float(DEFAULT_SR),
              cabinet_type: str = "1x12") -> np.ndarray:
    """
    Compute the minimum-phase FIR impulse response for the given cabinet type.
    Returns float32 array of n_taps coefficients.
    """
    cfg = CABINET_CONFIGS[cabinet_type]
    N = _FFT_SIZE

    # One-sided magnitude at the FFT frequency bins
    freqs = np.fft.rfftfreq(N, d=1.0 / sr)
    mag_half = _target_magnitude(freqs,
                                  f_res=cfg["f_res"],
                                  f_cut=cfg["f_cut"],
                                  peak_db=cfg["peak_db"],
                                  Q=cfg["Q"],
                                  rolloff_pow=cfg["rolloff_pow"])

    # Build full symmetric magnitude spectrum (length N)
    mag_full = np.concatenate([mag_half, mag_half[-2:0:-1]])  # mirror, length N

    # Minimum-phase reconstruction via real cepstrum
    log_mag = np.log(np.clip(mag_full, 1e-12, None))
    cep = np.real(np.fft.ifft(log_mag))  # real cepstrum

    # Minimum-phase lifter: double positive-time, zero negative-time components
    win = np.zeros(N, dtype=np.float64)
    win[0] = 1.0
    win[1: N // 2] = 2.0
    win[N // 2] = 1.0  # Nyquist bin

    H_min = np.exp(np.fft.fft(cep * win))
    ir_full = np.real(np.fft.ifft(H_min))

    # Truncate to n_taps (minimum-phase energy is front-loaded)
    ir = ir_full[:n_taps].copy()

    # Normalise so the passband gain at 1 kHz == 1.0 (unity gain in the flat region).
    # Time-domain peak normalisation does not constrain the frequency-domain response.
    H_ref = np.abs(np.fft.rfft(ir, n=4096))
    f_ref = np.fft.rfftfreq(4096, d=1.0 / sr)
    idx_1k = int(np.argmin(np.abs(f_ref - 1000.0)))
    gain_1k = H_ref[idx_1k]
    if gain_1k > 1e-12:
        ir /= gain_1k

    return ir.astype(np.float32)


# Module-level IR cache keyed by (type, sr) — shared by process() and generate_header()
_IR_CACHE: dict[tuple, np.ndarray] = {}


def _get_ir(sr: float = float(DEFAULT_SR), cabinet_type: str = "1x12") -> np.ndarray:
    key = (cabinet_type, sr)
    if key not in _IR_CACHE:
        _IR_CACHE[key] = design_ir(N_TAPS, sr, cabinet_type)
    return _IR_CACHE[key]


def process(signal: np.ndarray, sr: float, cabinet: bool = True,
            cabinet_type: str = "1x12") -> np.ndarray:
    """
    Apply cabinet IR convolution using scipy fftconvolve (float32 IR).
    This is the Python reference used to generate golden WAVs.
    """
    if not cabinet:
        return signal.copy().astype(np.float32)
    ir = _get_ir(sr, cabinet_type)
    # fftconvolve with float32 IR, trim to input length, then clip to [-1, 1].
    # Clipping matches the C++ wav_compare behaviour and the JUCE processBlock clamp.
    out = fftconvolve(signal.astype(np.float64), ir.astype(np.float64), mode="full")
    return np.clip(out[: len(signal)], -1.0, 1.0).astype(np.float32)


def generate_header(out_path: Path | None, sr: float = float(DEFAULT_SR),
                    cabinet_type: str = "1x12") -> None:
    """Write a libs/effects/CabinetIR_data*.h with float32 IR coefficients."""
    cfg = CABINET_CONFIGS[cabinet_type]
    ir = design_ir(N_TAPS, sr, cabinet_type)
    pfx = cfg["var_prefix"]      # e.g. "cabinet_ir_4x12"

    effects_dir = Path(__file__).parent.parent / "libs" / "effects"
    header = effects_dir / cfg["header"]
    if out_path is not None:
        header = out_path

    lines: list[str] = [
        "#pragma once",
        f"// Generated by python/cabinet_ir.py --type {cabinet_type} — do not edit by hand.",
        f"// Minimum-phase {cfg['description']} cabinet IR, {len(ir)} taps at 48 kHz.",
        f"// Target: resonance peak at {cfg['f_res']:.0f} Hz; "
        f"rolloff above {cfg['f_cut']:.0f} Hz.",
        "",
        f"constexpr int {pfx}_len = {len(ir)};",
        f"constexpr float {pfx}_data[{pfx}_len] = {{",
    ]

    vals = [f"{v:.8f}f" for v in ir]
    rows = [vals[i: i + 8] for i in range(0, len(vals), 8)]
    for row in rows:
        lines.append("    " + ", ".join(row) + ",")

    lines += ["};", ""]
    header.write_text("\n".join(lines))
    print(f"Written: {header}")


def _validate_direct_form(signal: np.ndarray, sr: float,
                          cabinet_type: str = "1x12") -> None:
    """Verify direct-form convolution matches fftconvolve within 5e-4."""
    ir = _get_ir(sr, cabinet_type)
    ref = process(signal, sr, cabinet=True, cabinet_type=cabinet_type)

    # Direct-form convolution in Python (float64 arithmetic)
    N = len(ir)
    hist = np.zeros(N, dtype=np.float64)
    out = np.zeros(len(signal), dtype=np.float64)
    head = 0
    for i, x in enumerate(signal):
        hist[head] = x
        acc = 0.0
        for k in range(N):
            acc += float(ir[k]) * hist[(head - k) % N]
        out[i] = acc
        head = (head + 1) % N

    err = np.max(np.abs(out.astype(np.float32) - ref))
    status = "PASS" if err < 5e-4 else "FAIL"
    print(f"  direct-form vs fftconvolve: max_err={err:.2e}  [{status}]")


def main() -> None:
    parser = argparse.ArgumentParser(description="Cabinet IR designer")
    parser.add_argument("input",  nargs="?", help="Input WAV")
    parser.add_argument("output", nargs="?", help="Output WAV")
    parser.add_argument("--type", default="1x12",
                        choices=list(CABINET_CONFIGS.keys()),
                        help="Cabinet type (default: 1x12)")
    parser.add_argument("--generate-header", action="store_true",
                        help="Write libs/effects/CabinetIR_data*.h and exit")
    parser.add_argument("--validate", action="store_true",
                        help="Validate direct-form vs fftconvolve on the input")
    parser.add_argument("--out-path", default=None,
                        help="Override output path for --generate-header")
    args = parser.parse_args()

    if args.generate_header:
        out = Path(args.out_path) if args.out_path else None
        generate_header(out, cabinet_type=args.type)
        return

    if not args.input or not args.output:
        parser.print_help()
        sys.exit(1)

    signal, sr = load_wav(args.input)
    if args.validate:
        print(f"Validating direct-form convolution against fftconvolve ({args.type}):")
        _validate_direct_form(signal, sr, cabinet_type=args.type)

    out = process(signal, float(sr), cabinet=True, cabinet_type=args.type)
    save_wav(args.output, out, sr)
    print(f"Written: {args.output}")


if __name__ == "__main__":
    main()
