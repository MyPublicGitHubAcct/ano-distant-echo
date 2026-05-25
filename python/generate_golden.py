"""
Generate reference input WAVs and Python golden output WAVs.

Inputs land in tests/golden/input/.
Golden outputs land in tests/golden/.

Run once (or whenever the Python effect algorithms change):
    uv run python/generate_golden.py
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import numpy as np

import cabinet_ir as cabinet_mod
import delay as delay_mod
import overdrive as overdrive_mod
from utils import DEFAULT_SR, GOLDEN_DIR, save_wav, sine

INPUT_DIR = GOLDEN_DIR / "input"

# ---------------------------------------------------------------------------
# Reference input signals
# All are mono at 48 kHz, 1 second long.
# ---------------------------------------------------------------------------
SR = DEFAULT_SR  # 48000

INPUTS: dict[str, np.ndarray] = {
    # 440 Hz (A4) at three dynamics — emulates soft, medium, and hard picking
    "soft":   sine(440.0, 1.0, SR, amplitude=0.1),
    "medium": sine(440.0, 1.0, SR, amplitude=0.5),
    "hard":   sine(440.0, 1.0, SR, amplitude=0.95),
    # 110 Hz (A2) — low E string fundamental
    "bass":   sine(110.0, 1.0, SR, amplitude=0.5),
    # Power chord: root + fifth + octave (110, 165, 220 Hz)
    "chord":  (sine(110.0, 1.0, SR, 0.3)
               + sine(165.0, 1.0, SR, 0.3)
               + sine(220.0, 1.0, SR, 0.3)).astype(np.float32),
}

# ---------------------------------------------------------------------------
# Golden test cases — (effect, input_name, params, output_name)
# Params must match the wav_compare CLI flags used in the Makefile.
# ---------------------------------------------------------------------------
CASES: list[tuple] = [
    # Overdrive — HardClip mode (polynomial waveshaper)
    ("overdrive", "medium", dict(drive=0.5, tone=0.5, level=0.8),                  "overdrive_default"),
    ("overdrive", "medium", dict(drive=1.0, tone=0.5, level=0.8),                  "overdrive_drive_high"),
    ("overdrive", "medium", dict(drive=0.5, tone=0.9, level=0.8),                  "overdrive_tone_bright"),
    ("overdrive", "hard",   dict(drive=0.5, tone=0.5, level=0.8),                  "overdrive_hard_input"),
    # Overdrive — additional distortion modes
    ("overdrive", "medium", dict(drive=0.5, tone=0.5, level=0.8, mode="softclip"),  "overdrive_softclip"),
    ("overdrive", "medium", dict(drive=0.5, tone=0.5, level=0.8, mode="foldback"),  "overdrive_foldback"),
    ("overdrive", "medium", dict(drive=0.5, tone=0.5, level=0.8, mode="asymmetric"), "overdrive_asymmetric"),
    ("overdrive", "medium", dict(drive=0.5, tone=0.5, level=0.8, mode="bitcrush"),  "overdrive_bitcrush"),
    # 13c: ClipShape pre/de-emphasis
    ("overdrive", "medium", dict(drive=0.5, tone=0.5, level=0.8, clip_shape="midfocus"),    "overdrive_midfocus"),
    ("overdrive", "medium", dict(drive=0.5, tone=0.5, level=0.8, clip_shape="brightfocus"), "overdrive_brightfocus"),
    # 13d: mid/presence EQ
    ("overdrive", "medium", dict(drive=0.5, tone=0.5, level=0.8, mid_db=6.0),      "overdrive_mid_boost"),
    ("overdrive", "medium", dict(drive=0.5, tone=0.5, level=0.8, presence_db=4.0), "overdrive_presence"),
    # 16a: bias / operating-point shift — SoftClip generates even-order harmonics when biased
    ("overdrive", "medium", dict(drive=0.5, tone=0.5, level=0.8, mode="softclip", bias= 0.3), "overdrive_bias_pos"),
    ("overdrive", "medium", dict(drive=0.5, tone=0.5, level=0.8, mode="softclip", bias=-0.3), "overdrive_bias_neg"),
    # 16g: cabinet IR convolution — direct-form C++ vs fftconvolve Python (all three types)
    ("cabinet", "medium", dict(),                       "cabinet_medium"),
    ("cabinet", "hard",   dict(),                       "cabinet_hard"),
    ("cabinet", "medium", dict(cabinet_type="4x12"),    "cabinet_4x12_medium"),
    ("cabinet", "hard",   dict(cabinet_type="4x12"),    "cabinet_4x12_hard"),
    ("cabinet", "medium", dict(cabinet_type="combo"),   "cabinet_combo_medium"),
    ("cabinet", "hard",   dict(cabinet_type="combo"),   "cabinet_combo_hard"),
    # Delay
    ("delay", "medium", dict(time_ms=300.0, feedback=0.4, mix=0.5), "delay_default"),
    ("delay", "medium", dict(time_ms=500.0, feedback=0.6, mix=0.5), "delay_long"),
    ("delay", "bass",   dict(time_ms=300.0, feedback=0.4, mix=0.5), "delay_bass"),
    # 14c: wow and flutter
    ("delay", "medium", dict(time_ms=300.0, feedback=0.4, mix=0.5, wow_rate=0.5, wow_depth_ms=4.0),         "delay_wow"),
    ("delay", "medium", dict(time_ms=300.0, feedback=0.4, mix=0.5, flutter_rate=8.0, flutter_depth_ms=1.0), "delay_flutter"),
    # 14d: tape saturation
    ("delay", "medium", dict(time_ms=300.0, feedback=0.4, mix=0.5, tape_sat=True),                          "delay_tape_sat"),
    ("delay", "medium", dict(time_ms=300.0, feedback=0.4, mix=0.5, tape_sat=True, tape_age=0.8),            "delay_tape_age"),
    # 14e: ducking
    ("delay", "medium", dict(time_ms=300.0, feedback=0.4, mix=0.5, duck_threshold=-20.0, duck_depth=0.5),   "delay_duck"),
    ("delay", "medium", dict(time_ms=300.0, feedback=0.4, mix=0.5, duck_threshold=-20.0, duck_depth=1.0),   "delay_duck_deep"),
    # 14f: diffusion
    ("delay", "medium", dict(time_ms=300.0, feedback=0.4, mix=0.5, diffusion=0.5), "delay_diffusion_half"),
    ("delay", "medium", dict(time_ms=300.0, feedback=0.4, mix=0.5, diffusion=1.0), "delay_diffusion_full"),
    # 15: stereo modes — independent (right time ×1.02) and ping-pong (cross-feedback)
    ("delay_stereo_independent", "medium", dict(time_ms=300.0, feedback=0.4, mix=0.5), "delay_stereo_independent"),
    ("delay_stereo_pingpong",    "medium", dict(time_ms=300.0, feedback=0.4, mix=0.5), "delay_stereo_pingpong"),
]


def main() -> None:
    INPUT_DIR.mkdir(parents=True, exist_ok=True)
    GOLDEN_DIR.mkdir(parents=True, exist_ok=True)

    # Write reference inputs
    for name, signal in INPUTS.items():
        path = INPUT_DIR / f"{name}.wav"
        save_wav(path, signal, SR)
        print(f"  input  {path.relative_to(Path.cwd())}")

    # Generate golden outputs
    for effect, input_name, params, out_name in CASES:
        signal = INPUTS[input_name]
        if effect == "cabinet":
            out = cabinet_mod.process(signal, float(SR), cabinet=True, **params)
            path = GOLDEN_DIR / f"{out_name}.wav"
            save_wav(path, out, SR)
        elif effect == "overdrive":
            out = overdrive_mod.process(signal, SR, **params)
            path = GOLDEN_DIR / f"{out_name}.wav"
            save_wav(path, out, SR)
        elif effect == "delay":
            out = delay_mod.process(signal, SR, **params)
            path = GOLDEN_DIR / f"{out_name}.wav"
            save_wav(path, out, SR)
        elif effect == "delay_stereo_independent":
            # Stereo independent: both channels receive same signal; R uses ×1.02 time
            out_l, out_r = delay_mod.process_stereo(
                signal.copy(), signal.copy(), SR, ping_pong=False, **params)
            path = GOLDEN_DIR / f"{out_name}.wav"
            save_wav(path, np.column_stack([out_l, out_r]), SR)
        elif effect == "delay_stereo_pingpong":
            # Ping-pong: L receives signal, R starts silent; echoes alternate channels
            r_in = np.zeros_like(signal)
            out_l, out_r = delay_mod.process_stereo(
                signal.copy(), r_in, SR, ping_pong=True, **params)
            path = GOLDEN_DIR / f"{out_name}.wav"
            save_wav(path, np.column_stack([out_l, out_r]), SR)
        else:
            raise ValueError(f"Unknown effect: {effect}")

        print(f"  golden {path.relative_to(Path.cwd())}")

    print(f"\nGenerated {len(INPUTS)} inputs and {len(CASES)} golden files.")


if __name__ == "__main__":
    main()
