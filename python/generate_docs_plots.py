"""Generate documentation plots for docs/img/.

Run with:
    uv run --project python python/generate_docs_plots.py
"""

import sys
from pathlib import Path

import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from scipy.signal import freqz

sys.path.insert(0, str(Path(__file__).parent))
from oversampler import _design, upsample, downsample, L, N_TAPS, BETA
from overdrive import process as od_process
from cabinet_ir import design_ir, CABINET_CONFIGS

DOCS = Path(__file__).parent.parent / "docs"
IMG  = DOCS / "img"
IMG.mkdir(parents=True, exist_ok=True)

SR    = 48000
SR_OS = SR * L   # 192000

plt.rcParams.update({
    'figure.facecolor': 'white',
    'axes.facecolor': '#f5f5f5',
    'axes.grid': True,
    'grid.alpha': 0.45,
    'axes.spines.top': False,
    'axes.spines.right': False,
    'font.family': 'sans-serif',
    'font.size': 10,
})


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def mag_db(H: np.ndarray) -> np.ndarray:
    return 20.0 * np.log10(np.abs(H) + 1e-15)


def spectrum_db(x: np.ndarray, sr: int = SR):
    """(freq, dBFS) using Hann window; 0 dBFS = full-scale sine."""
    N = len(x)
    win = np.hanning(N)
    H = np.fft.rfft(x * win)
    freq = np.fft.rfftfreq(N, 1.0 / sr)
    # Correct for Hann coherent gain (0.5) and one-sided doubling:
    # amplitude = |H| * 2 / (N * 0.5) = |H| * 4/N
    db = 20.0 * np.log10(np.abs(H) * 4.0 / N + 1e-15)
    return freq, db


def shelf_biquad(fs: float, fc: float, gain_db: float):
    """Audio EQ Cookbook high-shelf (matches Overdrive.h designShelf)."""
    A   = 10.0 ** (gain_db / 40.0)
    w0  = 2.0 * np.pi * fc / fs
    cw  = np.cos(w0); sw = np.sin(w0)
    sqA = np.sqrt(A)
    alpha = sw / 2.0 * np.sqrt((A + 1.0/A) * (np.sqrt(2.0) - 1.0) + 2.0)
    b0 =  A * ((A+1) + (A-1)*cw + 2*sqA*alpha)
    b1 = -2*A * ((A-1) + (A+1)*cw)
    b2 =  A * ((A+1) + (A-1)*cw - 2*sqA*alpha)
    a0 =       (A+1) - (A-1)*cw + 2*sqA*alpha
    a1 =   2 * ((A-1) - (A+1)*cw)
    a2 =       (A+1) - (A-1)*cw - 2*sqA*alpha
    b = np.array([b0/a0, b1/a0, b2/a0])
    a = np.array([1.0,   a1/a0, a2/a0])
    return b, a


# ---------------------------------------------------------------------------
# 1. SmoothedValue — step response
# ---------------------------------------------------------------------------

def plot_smoothed_value():
    ramp_ms = 20.0
    coeff = np.exp(-1.0 / (SR * ramp_ms * 0.001))

    N = int(SR * 0.12)   # 120 ms
    curr = 0.0
    out = np.zeros(N)
    for i in range(N):
        curr = curr * coeff + 1.0 * (1.0 - coeff)
        out[i] = curr

    t_ms = np.arange(N) / SR * 1000.0

    fig, ax = plt.subplots(figsize=(8, 3.5))
    ax.plot(t_ms, out, color='#2255aa', lw=2, label='Smoothed output')
    ax.axhline(1.0, color='#666', lw=1.0, ls='--', label='Target (1.0)')
    ax.axvline(ramp_ms, color='#cc3333', lw=1.0, ls=':')
    tau_val = 1.0 - np.exp(-1.0)
    ax.plot(ramp_ms, tau_val, 'o', color='#cc3333', ms=6,
            label=f'63% at τ = {ramp_ms:.0f} ms (one time constant)')
    ax.set_xlim(0, t_ms[-1])
    ax.set_ylim(-0.05, 1.15)
    ax.set_xlabel('Time (ms)')
    ax.set_ylabel('Normalised value')
    ax.set_title('SmoothedValue — step response  (rampMs = 20 ms, SR = 48 kHz)')
    ax.legend(fontsize=9)
    fig.tight_layout()
    fig.savefig(IMG / 'smoothed-value-step.png', dpi=150)
    plt.close(fig)
    print('  smoothed-value-step.png')


# ---------------------------------------------------------------------------
# 2. Delay — feedback LP filter frequency response
# ---------------------------------------------------------------------------

def plot_delay_lp():
    alpha = 1.0 - np.exp(-2.0 * np.pi * 4000.0 / SR)
    b = [alpha]
    a = [1.0, -(1.0 - alpha)]

    w, H = freqz(b, a, worN=8192, fs=SR)
    db = mag_db(H)

    # Find actual -3 dB point
    idx3 = np.argmin(np.abs(db + 3.0))
    f3db = w[idx3]

    fig, ax = plt.subplots(figsize=(8, 3.5))
    ax.semilogx(w, db, color='#2255aa', lw=2)
    ax.axhline(-3.0, color='#999', lw=0.9, ls='--', label='−3 dB')
    ax.axvline(f3db, color='#cc3333', lw=1.0, ls=':',
               label=f'−3 dB at {f3db:.0f} Hz')
    ax.set_xlim(80, SR / 2)
    ax.set_ylim(-45, 2)
    ax.set_xlabel('Frequency (Hz)')
    ax.set_ylabel('Magnitude (dB)')
    ax.set_title('Delay — feedback low-pass filter  (1-pole IIR, fc ≈ 4 kHz)')
    ax.legend(fontsize=9)
    fig.tight_layout()
    fig.savefig(IMG / 'delay-lp-response.png', dpi=150)
    plt.close(fig)
    print('  delay-lp-response.png')


# ---------------------------------------------------------------------------
# 3. Delay — echo decay envelope at various feedback levels
# ---------------------------------------------------------------------------

def plot_delay_decay():
    delay_ms = 300
    delay_smp = int(SR * delay_ms / 1000.0)
    alpha = 1.0 - np.exp(-2.0 * np.pi * 4000.0 / SR)
    duration = int(SR * 3.0)

    # 10 ms tone burst at 330 Hz
    burst_len = int(SR * 0.01)
    inp = np.zeros(duration)
    t_b = np.arange(burst_len) / SR
    inp[:burst_len] = np.sin(2.0 * np.pi * 330.0 * t_b) * np.hanning(burst_len)

    feedback_vals = [0.3, 0.5, 0.7, 0.9]
    colors = ['#2255aa', '#22aa55', '#cc5500', '#882288']

    fig, ax = plt.subplots(figsize=(9, 4))
    hop = 512

    for fb, color in zip(feedback_vals, colors):
        buf = np.zeros(delay_smp + 1)
        lp_st = 0.0
        wp = 0
        env_t, env_v = [], []
        for i in range(duration):
            rp = (wp - delay_smp) % len(buf)
            wet = buf[rp]
            lp_st += alpha * (wet - lp_st)
            buf[wp] = inp[i] + fb * lp_st
            wp = (wp + 1) % len(buf)
            if i % hop == 0:
                chunk_start = max(0, i - hop)
                env_t.append(i / SR)
                env_v.append(np.max(np.abs(inp[chunk_start:i+1] + 0.0)))
        # Re-simulate to get actual output envelope
        buf = np.zeros(delay_smp + 1)
        lp_st = 0.0; wp = 0
        out = np.zeros(duration)
        for i in range(duration):
            rp = (wp - delay_smp) % len(buf)
            wet = buf[rp]
            lp_st += alpha * (wet - lp_st)
            buf[wp] = inp[i] + fb * lp_st
            wp = (wp + 1) % len(buf)
            out[i] = wet  # pure wet signal
        env_t2, env_v2 = [], []
        for k in range(0, duration - hop, hop):
            peak = np.max(np.abs(out[k:k+hop]))
            env_t2.append((k + hop / 2) / SR)
            env_v2.append(peak)
        env_v2 = np.array(env_v2)
        ax.plot(env_t2, 20.0 * np.log10(env_v2 + 1e-15),
                color=color, lw=1.8, label=f'feedback = {fb}')

    ax.set_xlim(0, 3.0)
    ax.set_ylim(-80, 5)
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Envelope (dB)')
    ax.set_title(f'Delay — echo decay envelope  (delay = {delay_ms} ms, various feedback levels)')
    ax.legend(fontsize=9)
    fig.tight_layout()
    fig.savefig(IMG / 'delay-echo-decay.png', dpi=150)
    plt.close(fig)
    print('  delay-echo-decay.png')


# ---------------------------------------------------------------------------
# 4. Oversampler — FIR magnitude response
# ---------------------------------------------------------------------------

def plot_oversampler_fir():
    h = _design()
    w, H = freqz(h, worN=16384, fs=SR_OS)
    db = mag_db(H)

    # Passband edge ≈ 24 kHz; stopband min attenuation
    pb_idx = np.argmin(np.abs(w - SR / 2))
    sb_start_idx = np.argmin(np.abs(w - (SR_OS / 2 - SR / 2)))  # symmetric
    sb_atten = np.min(db[sb_start_idx:])

    fig, axes = plt.subplots(2, 1, figsize=(9, 6))

    ax = axes[0]
    ax.plot(w / 1000.0, db, color='#2255aa', lw=1.5)
    ax.axvline(SR / 2 / 1000, color='#cc3333', lw=1.0, ls=':',
               label=f'Cutoff: {SR//2//1000} kHz (original Nyquist)')
    ax.fill_between(w / 1000.0, db, -200,
                    where=(w > SR / 2), alpha=0.08, color='#cc3333',
                    label='Stopband')
    ax.set_xlim(0, SR_OS / 2 / 1000)
    ax.set_ylim(-160, 5)
    ax.set_xlabel('Frequency (kHz)')
    ax.set_ylabel('Magnitude (dB)')
    ax.set_title(f'Oversampler FIR — full response  ({N_TAPS} taps, Kaiser β = {BETA}, 4× / {SR_OS//1000} kHz)')
    ax.legend(fontsize=9)
    ax.text(50, sb_atten + 8, f'min stopband\n≈ {sb_atten:.0f} dB',
            fontsize=8, color='#cc3333', ha='center')

    ax = axes[1]
    ax.plot(w / 1000.0, db, color='#2255aa', lw=1.5)
    ax.axvline(SR / 2 / 1000, color='#cc3333', lw=1.0, ls=':')
    ax.set_xlim(0, 32)
    ax.set_ylim(-6, 0.5)
    ax.set_xlabel('Frequency (kHz)')
    ax.set_ylabel('Magnitude (dB)')
    ax.set_title('Passband and transition detail (0 – 32 kHz)')

    fig.tight_layout()
    fig.savefig(IMG / 'oversampler-fir-response.png', dpi=150)
    plt.close(fig)
    print('  oversampler-fir-response.png')


# ---------------------------------------------------------------------------
# 5a. Overdrive — fastTanh accuracy (Padé [7/6] vs exact tanh)
# ---------------------------------------------------------------------------

def _fast_tanh(x: np.ndarray) -> np.ndarray:
    """Padé [7/6] approximant matching Overdrive.h fastTanh(), vectorised."""
    x = np.asarray(x, dtype=np.float32)
    out = np.empty_like(x)
    hi = x > 5.0;  lo = x < -5.0;  mid = ~(hi | lo)
    out[hi] = 1.0;  out[lo] = -1.0
    xm = x[mid];  x2 = xm * xm
    out[mid] = (xm * (135135.0 + x2 * (17325.0 + x2 * (378.0 + x2)))
                   / (135135.0 + x2 * (62370.0 + x2 * (3150.0 + x2 * 28.0))))
    return out


def plot_fast_tanh():
    x = np.linspace(-6.0, 6.0, 8000)
    y_exact = np.tanh(x.astype(np.float64))
    y_fast  = _fast_tanh(x).astype(np.float64)
    err = np.abs(y_fast - y_exact)

    fig, axes = plt.subplots(2, 1, figsize=(9, 6))

    ax = axes[0]
    ax.plot(x, y_exact, color='#333333', lw=2.2, ls='--', label='tanh(x)  (exact)')
    ax.plot(x, y_fast,  color='#2255aa', lw=1.5, label='fastTanh(x)  (Padé [7/6])')
    ax.axvline( 5.0, color='#cc3333', lw=0.8, ls=':', label='Clamp boundary (±5)')
    ax.axvline(-5.0, color='#cc3333', lw=0.8, ls=':')
    ax.set_xlim(-6, 6);  ax.set_ylim(-1.4, 1.4)
    ax.set_xlabel('x');  ax.set_ylabel('Output')
    ax.set_title('fastTanh — Padé [7/6] approximation vs exact tanh')
    ax.legend(fontsize=9)

    ax = axes[1]
    ax.semilogy(x, err + 1e-16, color='#cc3333', lw=1.5)
    ax.axhline(5e-4, color='#888', lw=0.9, ls='--', label='5×10⁻⁴ threshold')
    ax.axvline( 5.0, color='#cc3333', lw=0.8, ls=':')
    ax.axvline(-5.0, color='#cc3333', lw=0.8, ls=':')
    ax.set_xlim(-6, 6);  ax.set_ylim(1e-9, 1e-1)
    ax.set_xlabel('x');  ax.set_ylabel('Absolute error')
    ax.set_title('Approximation error |fastTanh(x) − tanh(x)|')
    ax.legend(fontsize=9)

    fig.tight_layout()
    fig.savefig(IMG / 'overdrive-fasttanh.png', dpi=150)
    plt.close(fig)
    print('  overdrive-fasttanh.png')


# ---------------------------------------------------------------------------
# 5b. Overdrive — pick sensitivity envelope follower
# ---------------------------------------------------------------------------

def plot_pick_sensitivity():
    env_attack  = float(np.exp(-1.0 / (SR * 0.001)))   # 1 ms attack
    env_release = float(np.exp(-1.0 / (SR * 0.100)))   # 100 ms release

    burst_len = int(SR * 0.030)   # 30 ms loud burst
    quiet_len = int(SR * 0.220)   # 220 ms quiet sustained note
    N = burst_len + quiet_len
    t_ms = np.arange(N) / SR * 1000.0

    sig = np.zeros(N)
    sig[:burst_len] = 0.8  * np.sin(2.0 * np.pi * 440.0 * np.arange(burst_len) / SR)
    sig[burst_len:] = 0.08 * np.sin(2.0 * np.pi * 440.0 * np.arange(quiet_len) / SR)

    env_state = 0.0
    env_out   = np.zeros(N)
    gain_env  = np.zeros(N)
    for i in range(N):
        a = abs(sig[i])
        c = env_attack if a > env_state else env_release
        env_state = c * env_state + (1.0 - c) * a
        env_out[i]  = env_state
        gain_env[i] = 1.0 - 0.292 * min(env_state, 1.0)

    gain_db = 20.0 * np.log10(gain_env + 1e-15)

    fig, axes = plt.subplots(3, 1, figsize=(9, 7), sharex=True)

    ax = axes[0]
    ax.plot(t_ms, sig, color='#2255aa', lw=0.7, alpha=0.75, label='Input signal')
    ax.set_ylabel('Amplitude')
    ax.set_title('Overdrive — pick sensitivity  (1 ms attack / 100 ms release)')
    ax.legend(fontsize=9)

    ax = axes[1]
    ax.plot(t_ms, env_out, color='#cc5500', lw=1.8, label='Envelope level')
    ax.axhline(1.0, color='#aaa', lw=0.8, ls='--', label='Clamp ceiling (1.0)')
    ax.set_ylabel('Envelope level');  ax.set_ylim(-0.05, 1.1)
    ax.legend(fontsize=9)

    ax = axes[2]
    ax.plot(t_ms, gain_db, color='#882288', lw=1.8, label='Gain reduction')
    ax.axhline( 0.0, color='#aaa', lw=0.8, ls='--')
    ax.axhline(-3.0, color='#888', lw=0.8, ls=':', label='−3 dB floor (envState = 1)')
    ax.set_ylabel('Gain (dB)');  ax.set_ylim(-4.0, 0.5)
    ax.set_xlabel('Time (ms)')
    ax.legend(fontsize=9)

    fig.tight_layout()
    fig.savefig(IMG / 'overdrive-pick-sensitivity.png', dpi=150)
    plt.close(fig)
    print('  overdrive-pick-sensitivity.png')


# ---------------------------------------------------------------------------
# 5. Overdrive — waveshaper transfer curves
# ---------------------------------------------------------------------------

def _hardclip(x):
    cx = np.clip(x, -1.0, 1.0)
    return (3.0 * cx - cx**3) * 0.5


def _softclip(x, g=50.0):
    scale = float(np.tanh(g))
    return np.tanh(x) / scale if scale > 1e-6 else x


def _foldback(x, T=1.0):
    xn = x + T
    m = np.mod(xn, 4.0 * T)
    return T - np.abs(m - 2.0 * T)


def _asymmetric(x, g=50.0):
    scale = float(np.tanh(g))
    pos = np.tanh(x) / scale if scale > 1e-6 else x
    neg = np.arctan(x) * (2.0 / np.pi)
    return np.where(x >= 0.0, pos, neg)


def _bitcrush(x, bits=4):
    xc = np.clip(x, -1.0, 1.0)
    step = 2.0 / (1 << bits)
    return np.clip(np.round(xc / step) * step, -1.0, 1.0)


def plot_waveshapers():
    x = np.linspace(-2.8, 2.8, 2000)
    g = 50.0

    configs = [
        ('HardClip',   _hardclip(x),         '#2255aa'),
        ('SoftClip',   _softclip(x, g),       '#22aa44'),
        ('Foldback',   _foldback(x),          '#cc5500'),
        ('Asymmetric', _asymmetric(x, g),     '#882288'),
        ('Bitcrush',   _bitcrush(x, bits=4),  '#cc2222'),
    ]

    fig, axes = plt.subplots(1, 5, figsize=(16, 4.2), sharey=True)
    for ax, (name, y, color) in zip(axes, configs):
        ax.plot(x, x, color='#cccccc', lw=0.9, ls='--')   # linear ref
        ax.plot(x, y, color=color, lw=2.2)
        ax.axhline(0, color='#aaa', lw=0.5)
        ax.axvline(0, color='#aaa', lw=0.5)
        ax.set_xlim(-2.8, 2.8)
        ax.set_ylim(-1.4, 1.4)
        ax.set_title(name, fontsize=10, fontweight='bold', color=color)
        ax.set_xlabel('Input (post-gain)')
        ax.set_xticks([-2, -1, 0, 1, 2])

    axes[0].set_ylabel('Output')
    fig.suptitle('Overdrive — waveshaper transfer curves  (Bitcrush shown at 4-bit resolution)',
                 fontsize=11, fontweight='bold')
    fig.tight_layout()
    fig.savefig(IMG / 'overdrive-waveshapers.png', dpi=150)
    plt.close(fig)
    print('  overdrive-waveshapers.png')


# ---------------------------------------------------------------------------
# 6. Overdrive — harmonic spectra for each mode
# ---------------------------------------------------------------------------

def plot_overdrive_spectra():
    sr = 48000
    # 6 kHz input: exact integer period at 192 kHz OS rate → no spurious harmonics
    N = sr  # 1 second
    t = np.arange(N) / sr
    sig = np.sin(2.0 * np.pi * 6000.0 * t).astype(np.float32)

    modes = ['hardclip', 'softclip', 'foldback', 'asymmetric', 'bitcrush']
    labels = ['HardClip', 'SoftClip', 'Foldback', 'Asymmetric', 'Bitcrush']
    colors = ['#2255aa', '#22aa44', '#cc5500', '#882288', '#cc2222']
    drive = 0.5   # g ≈ 50.5 — well into saturation

    fig, axes = plt.subplots(1, 5, figsize=(18, 4.5), sharey=True)
    for ax, mode, label, color in zip(axes, modes, labels, colors):
        out = od_process(sig, sr, drive=drive, tone=0.5, level=1.0,
                         mode=mode, pick_sens=False)
        freq, db = spectrum_db(out.astype(np.float64), sr)
        ax.plot(freq / 1000.0, db, color=color, lw=0.9, alpha=0.85)
        ax.axvline(6.0, color='#999', lw=0.8, ls=':', alpha=0.7)
        ax.set_xlim(0, 24)
        ax.set_ylim(-100, 5)
        ax.set_xlabel('Frequency (kHz)')
        ax.set_title(label, fontsize=10, fontweight='bold', color=color)

    axes[0].set_ylabel('Magnitude (dBFS)')
    fig.suptitle('Overdrive — output spectra  (6 kHz input, drive = 0.5, tone = 0.5)',
                 fontsize=11, fontweight='bold')
    fig.tight_layout()
    fig.savefig(IMG / 'overdrive-spectra.png', dpi=150)
    plt.close(fig)
    print('  overdrive-spectra.png')


# ---------------------------------------------------------------------------
# 7. Overdrive — tone blend frequency responses
# ---------------------------------------------------------------------------

def plot_tone_blend():
    fc = 3500.0
    omega = 2.0 * np.pi * fc / SR
    cosW = np.cos(omega); sinW = np.sin(omega)
    # 2nd-order Butterworth: Q = 1/sqrt(2), alpha = sin(ω)/(2Q) = sin(ω)/sqrt(2)
    alpha = sinW / np.sqrt(2.0)
    a0 = 1.0 + alpha

    lp_b = np.array([(1 - cosW) / 2 / a0, (1 - cosW) / a0, (1 - cosW) / 2 / a0])
    lp_a = np.array([1.0, -2.0 * cosW / a0, (1.0 - alpha) / a0])
    hp_b = np.array([(1 + cosW) / 2 / a0, -(1 + cosW) / a0, (1 + cosW) / 2 / a0])
    hp_a = lp_a.copy()

    w, H_lp = freqz(lp_b, lp_a, worN=8192, fs=SR)
    _, H_hp = freqz(hp_b, hp_a, worN=8192, fs=SR)

    tone_vals = [0.0, 0.25, 0.5, 0.75, 1.0]
    cmap = plt.cm.coolwarm
    colors = [cmap(v) for v in tone_vals]

    fig, ax = plt.subplots(figsize=(8, 4))
    for t_val, color in zip(tone_vals, colors):
        H_blend = (1.0 - t_val) * H_lp + t_val * H_hp
        ax.semilogx(w, mag_db(H_blend), color=color, lw=2.2,
                    label=f'tone = {t_val:.2f}')
    ax.axvline(fc, color='#888', lw=0.9, ls='--', label=f'{fc:.0f} Hz crossover')
    ax.set_xlim(100, SR / 2)
    ax.set_ylim(-28, 4)
    ax.set_xlabel('Frequency (Hz)')
    ax.set_ylabel('Magnitude (dB)')
    ax.set_title('Overdrive — tone blend  (2nd-order Butterworth LP + HP at 3.5 kHz)')
    ax.legend(fontsize=9, loc='lower right')
    fig.tight_layout()
    fig.savefig(IMG / 'overdrive-tone-blend.png', dpi=150)
    plt.close(fig)
    print('  overdrive-tone-blend.png')


# ---------------------------------------------------------------------------
# 8. Overdrive — mid peaking EQ
# ---------------------------------------------------------------------------

def plot_mid_eq():
    fc = 800.0
    bw = 1.5   # octaves
    w0 = 2.0 * np.pi * fc / SR
    sin_w0 = np.sin(w0)
    cos_w0 = np.cos(w0)

    mid_vals = [-6.0, -3.0, 3.0, 6.0, 10.0]
    colors = ['#1155cc', '#5588dd', '#dd8833', '#cc4411', '#990000']

    fig, ax = plt.subplots(figsize=(8, 4))
    ax.axhline(0.0, color='#aaa', lw=1.0, ls='--', label='0 dB (bypass)')

    for mid, color in zip(mid_vals, colors):
        A = 10.0 ** (mid / 40.0)
        alpha = sin_w0 * np.sinh(np.log(2.0) / 2.0 * bw * w0 / sin_w0)
        b0 = 1.0 + alpha * A;  b1 = -2.0 * cos_w0;  b2 = 1.0 - alpha * A
        a0 = 1.0 + alpha / A;  a1 = -2.0 * cos_w0;  a2 = 1.0 - alpha / A
        b = np.array([b0/a0, b1/a0, b2/a0])
        a = np.array([1.0,   a1/a0, a2/a0])
        w, H = freqz(b, a, worN=8192, fs=SR)
        ax.semilogx(w, mag_db(H), color=color, lw=1.9,
                    label=f'{mid:+.0f} dB')

    ax.axvline(fc, color='#888', lw=0.9, ls=':', label=f'{fc:.0f} Hz centre')
    ax.set_xlim(100, SR / 2)
    ax.set_ylim(-12, 15)
    ax.set_xlabel('Frequency (Hz)')
    ax.set_ylabel('Magnitude (dB)')
    ax.set_title('Overdrive — mid peaking EQ  (fc = 800 Hz, BW = 1.5 oct)')
    ax.legend(fontsize=9, loc='upper right', ncol=2)
    fig.tight_layout()
    fig.savefig(IMG / 'overdrive-mid-eq.png', dpi=150)
    plt.close(fig)
    print('  overdrive-mid-eq.png')


# ---------------------------------------------------------------------------
# 9. Overdrive — presence high shelf
# ---------------------------------------------------------------------------

def plot_presence_shelf():
    fc = 4000.0
    presence_vals = [2.0, 4.0, 6.0, 8.0]
    colors = ['#aaccff', '#6699dd', '#3366bb', '#113388']

    fig, ax = plt.subplots(figsize=(8, 4))
    ax.axhline(0.0, color='#aaa', lw=1.0, ls='--', label='0 dB (bypass)')

    for pres, color in zip(presence_vals, colors):
        b, a = shelf_biquad(SR, fc, pres)
        w, H = freqz(b, a, worN=8192, fs=SR)
        ax.semilogx(w, mag_db(H), color=color, lw=1.9, label=f'+{pres:.0f} dB')

    ax.axvline(fc, color='#cc3333', lw=0.9, ls=':', label=f'{fc:.0f} Hz shelf')
    ax.set_xlim(100, SR / 2)
    ax.set_ylim(-1, 12)
    ax.set_xlabel('Frequency (Hz)')
    ax.set_ylabel('Magnitude (dB)')
    ax.set_title('Overdrive — presence high shelf  (fc = 4 kHz)')
    ax.legend(fontsize=9)
    fig.tight_layout()
    fig.savefig(IMG / 'overdrive-presence-shelf.png', dpi=150)
    plt.close(fig)
    print('  overdrive-presence-shelf.png')


# ---------------------------------------------------------------------------
# 10. Overdrive — ClipShape pre/de-emphasis shelves
# ---------------------------------------------------------------------------

def plot_clip_shapes():
    fig, axes = plt.subplots(1, 2, figsize=(12, 4), sharey=True)

    configs = [
        (axes[0], 'MidFocus — ±6 dB shelf at 700 Hz', 700.0),
        (axes[1], 'BrightFocus — ±6 dB shelf at 3 kHz', 3000.0),
    ]

    for ax, title, fc in configs:
        b_pre, a_pre = shelf_biquad(SR_OS, fc, +6.0)
        b_de,  a_de  = shelf_biquad(SR_OS, fc, -6.0)
        w, H_pre = freqz(b_pre, a_pre, worN=16384, fs=SR_OS)
        _, H_de  = freqz(b_de,  a_de,  worN=16384, fs=SR_OS)
        ax.semilogx(w / 1000.0, mag_db(H_pre), color='#2255aa', lw=2.2,
                    label='Pre-emphasis (+6 dB, before clip)')
        ax.semilogx(w / 1000.0, mag_db(H_de),  color='#cc3333', lw=2.2, ls='--',
                    label='De-emphasis (−6 dB, after clip)')
        ax.axvline(fc / 1000.0, color='#888', lw=0.9, ls=':')
        ax.set_xlim(0.1, SR_OS / 2 / 1000.0)
        ax.set_ylim(-10, 10)
        ax.set_xlabel('Frequency (kHz)')
        ax.set_title(title, fontsize=10)
        ax.legend(fontsize=9)

    axes[0].set_ylabel('Magnitude (dB)')
    fig.suptitle('Overdrive — ClipShape pre/de-emphasis (designed at 192 kHz OS rate)',
                 fontsize=11, fontweight='bold')
    fig.tight_layout()
    fig.savefig(IMG / 'overdrive-clip-shapes.png', dpi=150)
    plt.close(fig)
    print('  overdrive-clip-shapes.png')


# ---------------------------------------------------------------------------
# 11. Cabinet IR — frequency response for all three cabinet types
# ---------------------------------------------------------------------------

def plot_cabinet_ir_response():
    configs = [
        ("1x12",  "#c8a96e", "1×12 Open-Back   (120 Hz peak, −6 dB/oct above 4 kHz)"),
        ("4x12",  "#5599cc", "4×12 Closed-Back  (80 Hz peak, −3 dB/oct above 3 kHz)"),
        ("combo", "#44aa88", "1×12 Combo       (180 Hz peak, −6 dB/oct above 5 kHz)"),
    ]

    fig, ax = plt.subplots(figsize=(9, 4))

    for cab_type, color, label in configs:
        ir = design_ir(cabinet_type=cab_type, sr=SR)
        # Zero-pad to 4096 for smooth frequency axis
        H = np.fft.rfft(ir, n=4096)
        freq = np.fft.rfftfreq(4096, d=1.0 / SR)
        db = mag_db(H)

        ax.semilogx(freq, db, color=color, lw=2.0, label=label)

    # Annotate key reference frequencies
    for f, note in [(120, "120 Hz"), (80, "80 Hz"), (180, "180 Hz"),
                    (3000, "3 kHz"), (4000, "4 kHz"), (5000, "5 kHz"),
                    (1000, "1 kHz (0 dB)")]:
        ax.axvline(f, color='#cccccc', lw=0.6, ls=':')

    ax.axhline(0.0, color='#999', lw=0.8, ls='--', label='0 dB (1 kHz normalisation)')
    ax.set_xlim(40, SR / 2)
    ax.set_ylim(-30, 12)
    ax.set_xlabel('Frequency (Hz)')
    ax.set_ylabel('Magnitude (dB)')
    ax.set_title('Cabinet IR — frequency response  (256-tap minimum-phase FIR, 48 kHz)',
                 fontsize=11)
    ax.legend(fontsize=9, loc='lower left')
    fig.tight_layout()
    fig.savefig(IMG / 'cabinet-ir-response.png', dpi=150)
    plt.close(fig)
    print('  cabinet-ir-response.png')


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

if __name__ == '__main__':
    print(f'Writing plots to {IMG}/')
    plot_smoothed_value()
    plot_delay_lp()
    plot_delay_decay()
    plot_oversampler_fir()
    plot_fast_tanh()
    plot_pick_sensitivity()
    plot_waveshapers()
    plot_overdrive_spectra()
    plot_tone_blend()
    plot_mid_eq()
    plot_presence_shelf()
    plot_clip_shapes()
    plot_cabinet_ir_response()
    print('Done.')
