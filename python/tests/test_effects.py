"""Pytest tests for the Python effect prototypes."""

import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).parent.parent))
import delay as delay_mod
import overdrive as overdrive_mod
from utils import DEFAULT_SR, SAMPLE_RATES, silence, sine


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _sine(freq=440.0, duration=0.1, sr=DEFAULT_SR, amp=1.0):
    return sine(freq, duration, sr, amplitude=amp)


# ---------------------------------------------------------------------------
# Overdrive
# ---------------------------------------------------------------------------

class TestOverdrive:
    def test_silence_in_silence_out(self):
        out = overdrive_mod.process(silence(0.1))
        assert np.allclose(out, 0.0, atol=1e-6)

    @pytest.mark.parametrize("sr", SAMPLE_RATES)
    def test_no_nan_inf(self, sr):
        sig = _sine(sr=sr)
        for drive in (0.0, 0.5, 1.0):
            for tone in (0.0, 0.5, 1.0):
                out = overdrive_mod.process(sig, sr=sr, drive=drive, tone=tone, level=1.0)
                assert np.all(np.isfinite(out)), f"NaN/Inf at drive={drive} tone={tone} sr={sr}"

    def test_output_bounded(self):
        # Amplified sine well above clip threshold
        sig = _sine(amp=10.0)
        out = overdrive_mod.process(sig, drive=1.0, level=1.0)
        assert np.all(np.abs(out) <= 1.0 + 1e-4)

    def test_asymmetric_clipping(self):
        # With drive=1 the gain is 100x; a 440 Hz sine at amp=0.1 is driven to ±10,
        # which clips to +1.0 / -0.6.  After tone filtering the positive peak should
        # still exceed the absolute negative peak.
        sig = _sine(amp=0.1, duration=0.2)
        out = overdrive_mod.process(sig, drive=1.0, tone=0.5, level=1.0, mode="asymmetric")
        assert out.max() > abs(out.min()) * 1.1

    def test_level_scales_output(self):
        sig = _sine(amp=0.1)
        out_half = overdrive_mod.process(sig, drive=0.5, tone=0.5, level=0.5)
        out_full = overdrive_mod.process(sig, drive=0.5, tone=0.5, level=1.0)
        assert np.allclose(out_half, out_full * 0.5, atol=1e-6)


# ---------------------------------------------------------------------------
# Delay
# ---------------------------------------------------------------------------

class TestDelay:
    def test_silence_in_silence_out(self):
        out = delay_mod.process(silence(0.5))
        assert np.allclose(out, 0.0, atol=1e-6)

    @pytest.mark.parametrize("sr", SAMPLE_RATES)
    def test_no_nan_inf(self, sr):
        sig = _sine(sr=sr)
        for fb in (0.0, 0.5, 0.95):
            out = delay_mod.process(sig, sr=sr, feedback=fb, mix=0.5)
            assert np.all(np.isfinite(out)), f"NaN/Inf at feedback={fb} sr={sr}"

    def test_dry_passthrough(self):
        sig = _sine()
        out = delay_mod.process(sig.copy(), mix=0.0, feedback=0.5)
        assert np.allclose(out, sig, atol=1e-6)

    def test_impulse_timing(self):
        sr = DEFAULT_SR
        time_ms = 100.0
        delay_samples = int(sr * time_ms / 1000.0)
        sig = np.zeros(delay_samples * 2, dtype=np.float32)
        sig[0] = 1.0
        out = delay_mod.process(sig, sr=sr, time_ms=time_ms, feedback=0.0, mix=1.0)
        assert abs(out[delay_samples] - 1.0) < 1e-4
        assert np.all(np.abs(out[1:delay_samples]) < 1e-4)

    def test_feedback_decays(self):
        sr = DEFAULT_SR
        time_ms = 50.0
        delay_samples = int(sr * time_ms / 1000.0)
        sig = np.zeros(delay_samples * 5, dtype=np.float32)
        sig[0] = 1.0
        out = delay_mod.process(sig, sr=sr, time_ms=time_ms, feedback=0.5, mix=1.0)
        echo1 = abs(out[delay_samples])
        echo2 = abs(out[2 * delay_samples])
        assert echo1 > 0.1
        assert echo2 > 0.01
        assert echo2 < echo1

    def test_max_feedback_stays_bounded(self):
        sig = np.zeros(DEFAULT_SR, dtype=np.float32)
        sig[0] = 1.0
        out = delay_mod.process(sig, feedback=0.95, mix=0.5)
        assert np.all(np.abs(out) < 100.0)
