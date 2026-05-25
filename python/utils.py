"""Shared utilities for effect prototyping and validation."""

import numpy as np
import soundfile as sf
import matplotlib.pyplot as plt
from pathlib import Path


class DelayLine:
    """Circular buffer for delay lines and FIR history buffers.

    Convention
    ----------
    push(x)       – write x, advance the write pointer.
    read(d)       – return the sample pushed d steps ago (d=1 = most recently pushed).
    read_lerp(d)  – fractional tap via linear interpolation.

    Typical per-sample usage (delay of exactly D samples)::

        wet = dl.read_lerp(D)   # read BEFORE push
        dl.push(input_sample)

    After push(x), read(1) == x.
    """

    def __init__(self, size: int, dtype=np.float64):
        self._buf = np.zeros(size, dtype=dtype)
        self._pos = 0

    def reset(self) -> None:
        self._buf[:] = 0.0
        self._pos = 0

    def push(self, x: float) -> None:
        self._buf[self._pos] = x
        self._pos = (self._pos + 1) % len(self._buf)

    def read(self, d: int) -> float:
        return self._buf[(self._pos - d) % len(self._buf)]

    def read_lerp(self, d: float) -> float:
        di = int(d)
        frac = d - di
        return (1.0 - frac) * self.read(di) + frac * self.read(di + 1)

    def read_lagrange(self, d: float) -> float:
        """4th-order Lagrange interpolation (5-point kernel, nodes at -1..3)."""
        di = int(d)
        t = d - di
        h0 =  t * (t-1) * (t-2) * (t-3) / 24.0
        h1 = (t+1) * (t-1) * (t-2) * (t-3) / -6.0
        h2 = (t+1) * t * (t-2) * (t-3) / 4.0
        h3 = (t+1) * t * (t-1) * (t-3) / -6.0
        h4 = (t+1) * t * (t-1) * (t-2) / 24.0
        return (h0 * self.read(di-1) + h1 * self.read(di)   + h2 * self.read(di+1)
              + h3 * self.read(di+2) + h4 * self.read(di+3))

SAMPLE_RATES = [44100, 48000, 96000]
DEFAULT_SR = 48000
GOLDEN_DIR = Path(__file__).parent.parent / "tests" / "golden"


def load_wav(path: str | Path) -> tuple[np.ndarray, int]:
    """Return (samples, sample_rate). Samples are float32, shape (frames, channels) or (frames,)."""
    data, sr = sf.read(path, dtype="float32")
    return data, sr


def save_wav(path: str | Path, data: np.ndarray, sr: int = DEFAULT_SR) -> None:
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    sf.write(path, data, sr)


def process_blocks(effect_fn, signal: np.ndarray, block_size: int = 512) -> np.ndarray:
    """Run effect_fn(block) over a signal in fixed-size blocks, return concatenated output."""
    out = np.zeros_like(signal)
    for start in range(0, len(signal), block_size):
        block = signal[start : start + block_size]
        out[start : start + len(block)] = effect_fn(block)
    return out


def sine(freq: float, duration: float, sr: int = DEFAULT_SR, amplitude: float = 1.0) -> np.ndarray:
    t = np.linspace(0, duration, int(sr * duration), endpoint=False)
    return (amplitude * np.sin(2 * np.pi * freq * t)).astype(np.float32)


def silence(duration: float, sr: int = DEFAULT_SR) -> np.ndarray:
    return np.zeros(int(sr * duration), dtype=np.float32)


def plot_frequency_response(signal: np.ndarray, sr: int = DEFAULT_SR, title: str = "") -> None:
    freqs = np.fft.rfftfreq(len(signal), d=1.0 / sr)
    magnitude_db = 20 * np.log10(np.abs(np.fft.rfft(signal)) + 1e-12)
    plt.figure(figsize=(10, 4))
    plt.semilogx(freqs[1:], magnitude_db[1:])
    plt.xlabel("Frequency (Hz)")
    plt.ylabel("Magnitude (dB)")
    plt.title(title)
    plt.grid(True, which="both", alpha=0.3)
    plt.tight_layout()
    plt.show()


def plot_waveform(signal: np.ndarray, sr: int = DEFAULT_SR, title: str = "") -> None:
    t = np.linspace(0, len(signal) / sr, len(signal), endpoint=False)
    plt.figure(figsize=(10, 3))
    plt.plot(t, signal)
    plt.xlabel("Time (s)")
    plt.ylabel("Amplitude")
    plt.title(title)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.show()


def compare_outputs(reference: np.ndarray, actual: np.ndarray, tolerance: float = 1e-5) -> bool:
    """Return True if outputs match within tolerance; print max error otherwise."""
    if reference.shape != actual.shape:
        print(f"Shape mismatch: {reference.shape} vs {actual.shape}")
        return False
    max_err = float(np.max(np.abs(reference - actual)))
    if max_err > tolerance:
        print(f"Max sample error {max_err:.2e} exceeds tolerance {tolerance:.2e}")
        return False
    return True
