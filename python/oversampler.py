"""
4× polyphase FIR oversampler for overdrive aliasing suppression.

Filter: 128-tap Kaiser-windowed linear-phase LP FIR
  - Kaiser β = 8.0  → ≈ 80 dB minimum stopband attenuation
  - Cutoff at the original Nyquist (= 1/L of upsampled Nyquist)
  - Symmetric (Type-I), linear phase

Both upsample() and downsample() process the entire signal offline (causal,
no look-ahead padding) so they match the streaming C++ Oversampler within
the 5e-4 golden tolerance.

Usage
-----
    from oversampler import upsample, downsample, L
    x_os = upsample(x)          # len(x)*L samples at 4×
    y    = downsample(x_os)     # back to len(x) samples at 1×
"""

import numpy as np
from scipy.signal import firwin, upfirdn

L = 4        # oversampling factor (matches OVERDRIVE_OVERSAMPLING in C++)
N_TAPS = 128 # total FIR taps (divisible by L → 32 taps per polyphase phase)
BETA = 8.0   # Kaiser window β → ≈ 80 dB stopband attenuation


def _design(factor: int = L, n_taps: int = N_TAPS, beta: float = BETA) -> np.ndarray:
    """Return unit-DC-gain LP FIR coefficients (NOT pre-scaled for upsampling)."""
    # In scipy's firwin: cutoff is normalized to Nyquist (1.0 = Nyquist = fs/2).
    # At the upsampled rate (factor*fs), the original Nyquist (fs/2) maps to
    # (fs/2) / (factor*fs/2) = 1/factor of the upsampled Nyquist.
    cutoff = 1.0 / factor
    return firwin(n_taps, cutoff, window=("kaiser", beta))


_H = _design()  # unit-DC-gain; shape (N_TAPS,)


def upsample(x: np.ndarray, factor: int = L) -> np.ndarray:
    """Upsample x by *factor* using polyphase LP FIR.

    Returns exactly factor*len(x) samples (causal, same group delay as C++).
    Amplitude is preserved: a DC signal of amplitude A gives output of amplitude A.
    """
    h = _design(factor) if factor != L else _H
    # upfirdn inserts (factor-1) zeros between samples; multiplying h by factor
    # compensates for the resulting 1/factor amplitude loss.
    y = upfirdn(h * factor, x, up=factor)
    # The full linear convolution has len(x)*factor + len(h) - 1 samples.
    # Trim to factor*len(x): this is the causal output (same latency as C++ streaming).
    return y[: factor * len(x)]


def downsample(x_os: np.ndarray, factor: int = L) -> np.ndarray:
    """Downsample x_os by *factor* using the same LP FIR, then decimate.

    Returns exactly ceil(len(x_os)/factor) samples.
    """
    h = _design(factor) if factor != L else _H
    y = upfirdn(h, x_os, down=factor)
    n_out = (len(x_os) + factor - 1) // factor
    return y[:n_out]
