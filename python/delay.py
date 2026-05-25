"""
Delay effect — simple feedback delay.

A single delay line with a first-order low-pass filter (fc ≈ 4 kHz) in the
feedback path.  The LPF softens each repeat, mimicking the high-frequency
roll-off of tape and bucket-brigade analog delays.

Parameters
----------
time_ms          1–2000    Delay time in milliseconds.
feedback         0.0–0.95  Fraction of the delayed signal fed back into the buffer.
                           Clamped to 0.95 to prevent infinite build-up.
mix              0.0–1.0   Wet/dry mix: 0 = fully dry, 1 = fully wet.
interp           "linear"  Interpolation mode: "linear" (default) or "none".
                           "linear" reads between adjacent buffer samples for
                           sub-sample accuracy; "none" truncates to integer delay.
wow_rate         0.0–2.0   Wow LFO rate in Hz (0 = disabled).
wow_depth_ms     0.0–10.0  Wow peak delay deviation in ms.
flutter_rate     3.0–12.0  Flutter LFO rate in Hz (default 8 Hz).
flutter_depth_ms 0.0–2.0   Flutter peak delay deviation in ms (0 = disabled).
tape_sat         False     Enable tape saturation in feedback path.
                           Applies tanh(satDrive·lp) / tanh(satDrive) after the LP,
                           limiting runaway build-up and adding warmth.
tape_age         0.0–1.0   Tape age: simultaneously lowers LP cutoff (4 kHz → 1.5 kHz)
                           and raises saturation drive (2.0 → 5.0).
duck_threshold   −30–0 dBFS Ducking threshold. Envelope follower tracks the dry input;
                           when it exceeds this level, the wet gain is attenuated.
                           0.0 dBFS (default) = only triggers at full scale.
duck_depth       0.0–1.0   Duck depth: 0 = no effect; 1 = wet fully muted while input
                           is above threshold. Uses a 5 ms attack / 500 ms release
                           envelope follower on the dry input.
diffusion        0.0–1.0   Pre-delay diffusion: 0 = bypassed, 1 = full allpass chain.
                           Four Schroeder allpass stages (11, 17, 23, 31 ms; coeff 0.5)
                           smear input transients before they enter the delay line,
                           giving repeats a "bloom" character resembling a tape or
                           reverb-delay hybrid. The dry output path is unaffected.
"""

import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
from utils import DEFAULT_SR, DelayLine, load_wav, plot_waveform, save_wav


def process(
    signal: np.ndarray,
    sr: int = DEFAULT_SR,
    time_ms: float = 300.0,
    feedback: float = 0.4,
    mix: float = 0.5,
    interp: str = "linear",
    wow_rate: float = 0.0,
    wow_depth_ms: float = 0.0,
    flutter_rate: float = 8.0,
    flutter_depth_ms: float = 0.0,
    tape_sat: bool = False,
    tape_age: float = 0.0,
    duck_threshold: float = 0.0,
    duck_depth: float = 0.0,
    diffusion: float = 0.0,
) -> np.ndarray:
    feedback = float(np.clip(feedback, 0.0, 0.95))
    tape_age = float(np.clip(tape_age, 0.0, 1.0))
    delay_samples_f = max(1.0, sr * time_ms / 1000.0)
    delay_int = int(delay_samples_f)
    if interp != "linear":
        delay_samples_f = float(delay_int)

    # Tape mode auto-upgrades to Lagrange (matches C++ Delay.h behaviour).
    if tape_sat and interp == "linear":
        interp = "lagrange"

    wow_depth_samples    = sr * wow_depth_ms    / 1000.0
    flutter_depth_samples = sr * flutter_depth_ms / 1000.0

    # Buffer must fit base delay + maximum combined modulation offset.
    # Lagrange reads read(di+3), so needs 4 extra samples of headroom vs linear.
    max_mod = wow_depth_samples + flutter_depth_samples
    lagrange_extra = 4 if interp == "lagrange" else 0
    buf_size = delay_int + int(np.ceil(max_mod)) + 3 + lagrange_extra

    signal = signal.astype(np.float64)
    out = np.zeros_like(signal)
    dl = DelayLine(buf_size)

    # Tape age modifies LP cutoff (4 kHz → 1.5 kHz) and saturation drive (2 → 5)
    fc = 4000.0 - tape_age * 2500.0  # 4000 @ age=0, 1500 @ age=1
    sat_drive = 2.0 + tape_age * 3.0  # 2.0 @ age=0, 5.0 @ age=1
    sat_scale = 1.0 / np.tanh(sat_drive) if tape_sat else 1.0

    # Single-pole LP coefficient for feedback path
    alpha = 1.0 - np.exp(-2.0 * np.pi * fc / sr)
    lp = 0.0

    # Duck: input-level envelope follower (5 ms attack / 500 ms release)
    duck_threshold_lin = 10.0 ** (duck_threshold / 20.0)
    duck_alpha_a = 1.0 - np.exp(-1.0 / (sr * 0.005))   # 5 ms attack
    duck_alpha_r = 1.0 - np.exp(-1.0 / (sr * 0.500))   # 500 ms release
    duck_env = 0.0

    # Pre-diffusion allpass network (4 Schroeder stages: 11, 17, 23, 31 ms; coeff 0.5)
    AP_DELAYS_MS = [11.0, 17.0, 23.0, 31.0]
    AP_G = 0.5
    ap_D = [max(1, round(ms * sr / 1000.0)) for ms in AP_DELAYS_MS]
    ap_bufs = [np.zeros(D + 2, dtype=np.float64) for D in ap_D]
    ap_pos = [0] * 4

    # Wow LFO — sine at wow_rate Hz
    wow_phase     = np.float32(0.0)
    wow_phase_inc = np.float32(2.0 * np.pi * wow_rate / sr) if wow_rate > 0 else np.float32(0.0)

    # Flutter LFO — sum of two sines at slightly different rates (band-limited noise)
    flutter_rate2      = flutter_rate * 1.07
    flutter_phase1     = np.float32(0.0)
    flutter_phase2     = np.float32(0.0)
    flutter_phase_inc1 = np.float32(2.0 * np.pi * flutter_rate  / sr)
    flutter_phase_inc2 = np.float32(2.0 * np.pi * flutter_rate2 / sr)

    two_pi = np.float32(6.283185307179586)
    # Lagrange needs read(di+3): cap at buf_size-5 so di+3 < buf_size.
    max_read = float(buf_size - 5) if interp == "lagrange" else float(buf_size - 2)

    for i, x in enumerate(signal):
        mod_offset = 0.0
        if wow_depth_ms > 0.0:
            mod_offset += wow_depth_samples * float(np.sin(wow_phase))
            wow_phase += wow_phase_inc
            if wow_phase >= two_pi:
                wow_phase -= two_pi
        if flutter_depth_ms > 0.0:
            mod_offset += flutter_depth_samples * 0.5 * float(
                np.sin(flutter_phase1) + np.sin(flutter_phase2)
            )
            flutter_phase1 += flutter_phase_inc1
            if flutter_phase1 >= two_pi:
                flutter_phase1 -= two_pi
            flutter_phase2 += flutter_phase_inc2
            if flutter_phase2 >= two_pi:
                flutter_phase2 -= two_pi

        eff_delay = max(1.0, min(delay_samples_f + mod_offset, max_read))
        wet = dl.read_lagrange(eff_delay) if interp == "lagrange" else dl.read_lerp(eff_delay)
        lp += alpha * (wet - lp)
        fb_signal = np.tanh(sat_drive * lp) * sat_scale if tape_sat else lp

        # Pre-diffusion: run dry input through 4-stage Schroeder allpass chain
        if diffusion > 0.0:
            ap_x = float(x)
            for k in range(4):
                D = ap_D[k]
                n = len(ap_bufs[k])
                pos = ap_pos[k]
                state = ap_bufs[k][(pos - D) % n]
                u = ap_x + AP_G * state
                ap_bufs[k][pos] = u
                ap_pos[k] = (pos + 1) % n
                ap_x = state - AP_G * u  # y = v[n-D] - g*v[n]
            pre = (1.0 - diffusion) * float(x) + diffusion * ap_x
        else:
            pre = float(x)

        dl.push(pre + feedback * fb_signal)

        # Duck envelope follower on dry input; attenuates wet when above threshold
        if duck_depth > 0.0:
            abs_x = abs(x)
            if abs_x > duck_env:
                duck_env += duck_alpha_a * (abs_x - duck_env)
            else:
                duck_env += duck_alpha_r * (abs_x - duck_env)
            duck_gain = 1.0 - duck_depth if duck_env > duck_threshold_lin else 1.0
        else:
            duck_gain = 1.0

        out[i] = (1.0 - mix) * x + mix * duck_gain * wet

    return out.astype(np.float32)


def process_stereo(
    left: np.ndarray,
    right: np.ndarray,
    sr: int = DEFAULT_SR,
    time_ms: float = 300.0,
    feedback: float = 0.4,
    mix: float = 0.5,
    interp: str = "linear",
    wow_rate: float = 0.0,
    wow_depth_ms: float = 0.0,
    flutter_rate: float = 8.0,
    flutter_depth_ms: float = 0.0,
    tape_sat: bool = False,
    tape_age: float = 0.0,
    duck_threshold: float = 0.0,
    duck_depth: float = 0.0,
    diffusion: float = 0.0,
    ping_pong: bool = False,
) -> tuple[np.ndarray, np.ndarray]:
    """Stereo delay processing.

    ping_pong=False (independent): left and right share parameters but use
    separate delay lines; right delay time is left × 1.02 for stereo width.

    ping_pong=True: cross-channel feedback — left delay is fed by right's LP
    output and vice versa, producing alternating L/R echoes.

    Returns (left_out, right_out) as float32 arrays.
    """
    feedback = float(np.clip(feedback, 0.0, 0.95))
    tape_age = float(np.clip(tape_age, 0.0, 1.0))

    if tape_sat and interp == "linear":
        interp = "lagrange"

    delay_samples_l = max(1.0, sr * time_ms / 1000.0)
    stereo_ratio = 1.0 if ping_pong else 1.02
    delay_samples_r = max(1.0, delay_samples_l * stereo_ratio)

    max_delay_int = int(delay_samples_r) + 1
    wow_depth_samples    = sr * wow_depth_ms    / 1000.0
    flutter_depth_samples = sr * flutter_depth_ms / 1000.0
    max_mod = wow_depth_samples + flutter_depth_samples
    lagrange_extra = 4 if interp == "lagrange" else 0
    buf_size = max_delay_int + int(np.ceil(max_mod)) + 3 + lagrange_extra

    left  = left.astype(np.float64)
    right = right.astype(np.float64)
    out_l = np.zeros_like(left)
    out_r = np.zeros_like(right)

    dl_l = DelayLine(buf_size)
    dl_r = DelayLine(buf_size)

    fc = 4000.0 - tape_age * 2500.0
    sat_drive = 2.0 + tape_age * 3.0
    sat_scale = 1.0 / np.tanh(sat_drive) if tape_sat else 1.0
    alpha = 1.0 - np.exp(-2.0 * np.pi * fc / sr)
    lp_l = 0.0
    lp_r = 0.0

    duck_threshold_lin = 10.0 ** (duck_threshold / 20.0)
    duck_alpha_a = 1.0 - np.exp(-1.0 / (sr * 0.005))
    duck_alpha_r_coef = 1.0 - np.exp(-1.0 / (sr * 0.500))
    duck_env_l = 0.0
    duck_env_r = 0.0

    AP_DELAYS_MS = [11.0, 17.0, 23.0, 31.0]
    AP_G = 0.5
    ap_D = [max(1, round(ms * sr / 1000.0)) for ms in AP_DELAYS_MS]
    ap_bufs_l = [np.zeros(D + 2, dtype=np.float64) for D in ap_D]
    ap_bufs_r = [np.zeros(D + 2, dtype=np.float64) for D in ap_D]
    ap_pos_l = [0] * 4
    ap_pos_r = [0] * 4

    wow_phase     = np.float32(0.0)
    wow_phase_inc = np.float32(2.0 * np.pi * wow_rate / sr) if wow_rate > 0 else np.float32(0.0)
    flutter_rate2      = flutter_rate * 1.07
    flutter_phase1     = np.float32(0.0)
    flutter_phase2     = np.float32(0.0)
    flutter_phase_inc1 = np.float32(2.0 * np.pi * flutter_rate  / sr)
    flutter_phase_inc2 = np.float32(2.0 * np.pi * flutter_rate2 / sr)

    two_pi = np.float32(6.283185307179586)
    max_read = float(buf_size - 5) if interp == "lagrange" else float(buf_size - 2)

    n_samples = min(len(left), len(right))
    for i in range(n_samples):
        xL = left[i]
        xR = right[i]

        mod_offset = 0.0
        if wow_depth_ms > 0.0:
            mod_offset += wow_depth_samples * float(np.sin(wow_phase))
            wow_phase += wow_phase_inc
            if wow_phase >= two_pi:
                wow_phase -= two_pi
        if flutter_depth_ms > 0.0:
            mod_offset += flutter_depth_samples * 0.5 * float(
                np.sin(flutter_phase1) + np.sin(flutter_phase2)
            )
            flutter_phase1 += flutter_phase_inc1
            if flutter_phase1 >= two_pi:
                flutter_phase1 -= two_pi
            flutter_phase2 += flutter_phase_inc2
            if flutter_phase2 >= two_pi:
                flutter_phase2 -= two_pi

        eff_l = max(1.0, min(delay_samples_l + mod_offset, max_read))
        eff_r = max(1.0, min(delay_samples_r + mod_offset, max_read))

        if interp == "lagrange":
            wet_l = dl_l.read_lagrange(eff_l)
            wet_r = dl_r.read_lagrange(eff_r)
        else:
            wet_l = dl_l.read_lerp(eff_l)
            wet_r = dl_r.read_lerp(eff_r)

        lp_l += alpha * (wet_l - lp_l)
        lp_r += alpha * (wet_r - lp_r)
        fb_l = np.tanh(sat_drive * lp_l) * sat_scale if tape_sat else lp_l
        fb_r = np.tanh(sat_drive * lp_r) * sat_scale if tape_sat else lp_r

        # Pre-diffusion: independent allpass chains per channel
        if diffusion > 0.0:
            ap_xL, ap_xR = float(xL), float(xR)
            for k in range(4):
                D = ap_D[k]
                n = len(ap_bufs_l[k])
                pos = ap_pos_l[k]
                state = ap_bufs_l[k][(pos - D) % n]
                u = ap_xL + AP_G * state
                ap_bufs_l[k][pos] = u
                ap_pos_l[k] = (pos + 1) % n
                ap_xL = state - AP_G * u

                n = len(ap_bufs_r[k])
                pos = ap_pos_r[k]
                state = ap_bufs_r[k][(pos - D) % n]
                u = ap_xR + AP_G * state
                ap_bufs_r[k][pos] = u
                ap_pos_r[k] = (pos + 1) % n
                ap_xR = state - AP_G * u

            pre_l = (1.0 - diffusion) * float(xL) + diffusion * ap_xL
            pre_r = (1.0 - diffusion) * float(xR) + diffusion * ap_xR
        else:
            pre_l, pre_r = float(xL), float(xR)

        # Write to delay lines: ping-pong uses cross-channel feedback
        if ping_pong:
            dl_l.push(pre_l + feedback * fb_r)
            dl_r.push(pre_r + feedback * fb_l)
        else:
            dl_l.push(pre_l + feedback * fb_l)
            dl_r.push(pre_r + feedback * fb_r)

        # Duck per channel
        if duck_depth > 0.0:
            abs_l = abs(xL)
            duck_env_l += (duck_alpha_a if abs_l > duck_env_l else duck_alpha_r_coef) * (abs_l - duck_env_l)
            duck_gain_l = 1.0 - duck_depth if duck_env_l > duck_threshold_lin else 1.0
            abs_r = abs(xR)
            duck_env_r += (duck_alpha_a if abs_r > duck_env_r else duck_alpha_r_coef) * (abs_r - duck_env_r)
            duck_gain_r = 1.0 - duck_depth if duck_env_r > duck_threshold_lin else 1.0
        else:
            duck_gain_l = duck_gain_r = 1.0

        out_l[i] = (1.0 - mix) * xL + mix * duck_gain_l * wet_l
        out_r[i] = (1.0 - mix) * xR + mix * duck_gain_r * wet_r

    return out_l.astype(np.float32), out_r.astype(np.float32)


def main() -> None:
    parser = argparse.ArgumentParser(description="Delay — simple feedback")
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--time-ms",       type=float, default=300.0)
    parser.add_argument("--feedback",      type=float, default=0.4)
    parser.add_argument("--mix",           type=float, default=0.5)
    parser.add_argument("--interp",        choices=["linear", "none"], default="linear")
    parser.add_argument("--wow",           type=float, default=0.0,   dest="wow_rate",
                        metavar="HZ",      help="Wow LFO rate in Hz (0 = off)")
    parser.add_argument("--wow-depth",     type=float, default=0.0,   dest="wow_depth_ms",
                        metavar="MS",      help="Wow peak delay deviation in ms")
    parser.add_argument("--flutter",       type=float, default=8.0,   dest="flutter_rate",
                        metavar="HZ",      help="Flutter LFO rate in Hz (default 8)")
    parser.add_argument("--flutter-depth", type=float, default=0.0,   dest="flutter_depth_ms",
                        metavar="MS",      help="Flutter peak delay deviation in ms")
    parser.add_argument("--tape-sat",  action="store_true", dest="tape_sat",
                        help="Enable tape saturation in feedback path")
    parser.add_argument("--tape-age",  type=float, default=0.0, dest="tape_age",
                        metavar="0-1", help="Tape age: darkens LP and increases saturation (0–1)")
    parser.add_argument("--duck-threshold", type=float, default=0.0, dest="duck_threshold",
                        metavar="DBFS", help="Duck threshold in dBFS, -30 to 0 (default 0 = disabled)")
    parser.add_argument("--duck-depth", type=float, default=0.0, dest="duck_depth",
                        metavar="0-1", help="Duck depth 0–1 (0 = no duck, 1 = fully silent when ducked)")
    parser.add_argument("--diffusion", type=float, default=0.0,
                        metavar="0-1", help="Pre-delay diffusion 0–1 (0 = off, 1 = full allpass chain)")
    parser.add_argument("--stereo",    action="store_true",
                        help="Independent stereo output (right time ×1.02 for width)")
    parser.add_argument("--ping-pong", action="store_true", dest="ping_pong",
                        help="Ping-pong stereo: L and R delay lines cross-feed each other")
    parser.add_argument("--plot", action="store_true")
    args = parser.parse_args()

    signal, sr = load_wav(args.input)

    stereo_mode = args.stereo or args.ping_pong
    if stereo_mode:
        # Mono input → treat as L; R input is same signal (independent) or silence (ping-pong)
        mono = signal if signal.ndim == 1 else signal[:, 0]
        r_in = np.zeros_like(mono) if args.ping_pong else mono.copy()
        out_l, out_r = process_stereo(
            mono, r_in, sr,
            time_ms=args.time_ms, feedback=args.feedback, mix=args.mix, interp=args.interp,
            wow_rate=args.wow_rate, wow_depth_ms=args.wow_depth_ms,
            flutter_rate=args.flutter_rate, flutter_depth_ms=args.flutter_depth_ms,
            tape_sat=args.tape_sat, tape_age=args.tape_age,
            duck_threshold=args.duck_threshold, duck_depth=args.duck_depth,
            diffusion=args.diffusion, ping_pong=args.ping_pong,
        )
        stereo_out = np.column_stack([out_l, out_r])
        save_wav(args.output, stereo_out, sr)
    else:
        out = process(signal, sr, time_ms=args.time_ms, feedback=args.feedback,
                      mix=args.mix, interp=args.interp,
                      wow_rate=args.wow_rate, wow_depth_ms=args.wow_depth_ms,
                      flutter_rate=args.flutter_rate, flutter_depth_ms=args.flutter_depth_ms,
                      tape_sat=args.tape_sat, tape_age=args.tape_age,
                      duck_threshold=args.duck_threshold, duck_depth=args.duck_depth,
                      diffusion=args.diffusion)
        save_wav(args.output, out, sr)

    print(f"Written: {args.output}")

    if args.plot and not stereo_mode:
        plot_waveform(signal, sr, title="Input")
        plot_waveform(out, sr, title=f"Delay {args.time_ms}ms fb={args.feedback} mix={args.mix}")


if __name__ == "__main__":
    main()
