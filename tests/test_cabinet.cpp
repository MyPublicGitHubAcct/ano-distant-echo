#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <vector>

#include "CabinetIR.h"

static std::vector<float> makeSine(float freq, float duration, double sr)
{
    int n = (int)(sr * duration);
    std::vector<float> out(n);
    for (int i = 0; i < n; ++i)
        out[i] = std::sin(2.0f * (float)M_PI * freq * (float)i / (float)sr);
    return out;
}

// Compute RMS of a float buffer from sample `skip` to end.
static float rmsFrom(const std::vector<float>& buf, int skip)
{
    float acc = 0.0f;
    int count = (int)buf.size() - skip;
    if (count <= 0) return 0.0f;
    for (int i = skip; i < (int)buf.size(); ++i)
        acc += buf[i] * buf[i];
    return std::sqrt(acc / count);
}

// Process a sine through a CabinetIR of the given type and return steady-state RMS.
// SKIP must be > IR length (256 taps) to measure past the startup transient.
static float cabRmsAt(float freq, CabinetType type, double sr = 48000.0,
                      int N = 4096, int skip = 512, float amp = 1.0f)
{
    std::vector<float> sig(N);
    for (int i = 0; i < N; ++i)
        sig[i] = amp * std::sin(2.0f * (float)M_PI * freq * (float)i / (float)sr);
    CabinetIR cab;
    cab.setEnabled(true);
    cab.setType(type);
    cab.prepare(sr, N);
    cab.process(sig.data(), N);
    return rmsFrom(sig, skip);
}

// ---------------------------------------------------------------------------
// Basic behavioural tests (all three types)
// ---------------------------------------------------------------------------

TEST_CASE("CabinetIR: silence in → silence out", "[cabinet]")
{
    double sr = GENERATE(44100.0, 48000.0, 96000.0);
    std::vector<float> silence(4096, 0.0f);
    CabinetIR cab;
    cab.setEnabled(true);
    cab.prepare(sr, (int)silence.size());
    cab.process(silence.data(), (int)silence.size());
    for (float s : silence)
        CHECK(s == Catch::Approx(0.0f).margin(1e-6f));
}

TEST_CASE("CabinetIR: bypass when disabled", "[cabinet]")
{
    auto sig = makeSine(440.0f, 0.05f, 48000.0);
    std::vector<float> orig = sig;

    CabinetIR cab;
    cab.setEnabled(false);
    cab.prepare(48000.0, (int)sig.size());
    cab.process(sig.data(), (int)sig.size());

    for (size_t i = 0; i < sig.size(); ++i)
        CHECK(sig[i] == Catch::Approx(orig[i]).margin(1e-9f));
}

TEST_CASE("CabinetIR: no NaN/Inf for all cabinet types at multiple sample rates", "[cabinet]")
{
    double sr   = GENERATE(44100.0, 48000.0, 96000.0);
    auto   type = GENERATE(CabinetType::OpenBack1x12,
                           CabinetType::ClosedBack4x12,
                           CabinetType::Combo1x12);

    auto sig = makeSine(440.0f, 0.1f, sr);
    CabinetIR cab;
    cab.setEnabled(true);
    cab.setType(type);
    cab.prepare(sr, (int)sig.size());
    cab.process(sig.data(), (int)sig.size());
    for (float s : sig)
        REQUIRE(std::isfinite(s));
}

TEST_CASE("CabinetIR: reset clears history", "[cabinet]")
{
    auto loud = makeSine(440.0f, 0.05f, 48000.0);
    for (auto& s : loud) s *= 10.0f;

    CabinetIR cab;
    cab.setEnabled(true);
    cab.prepare(48000.0, (int)loud.size());
    cab.process(loud.data(), (int)loud.size());

    cab.reset();

    std::vector<float> silence(256, 0.0f);
    cab.process(silence.data(), (int)silence.size());
    for (float s : silence)
        CHECK(s == Catch::Approx(0.0f).margin(1e-6f));
}

TEST_CASE("CabinetIR: all cabinet IR header lengths are 256", "[cabinet]")
{
    CHECK(cabinet_ir_len       == 256);
    CHECK(cabinet_ir_4x12_len  == 256);
    CHECK(cabinet_ir_combo_len == 256);
}

// ---------------------------------------------------------------------------
// Frequency-response tests
// ---------------------------------------------------------------------------

TEST_CASE("CabinetIR: 1 kHz passband gain is near unity for all cabinet types", "[cabinet]")
{
    // IR is normalised at 1 kHz — passband gain must stay within ±1 dB for every type.
    // Amplitude 0.5 keeps the output below ±1.0 even if the resonance peak reaches +6 dB.
    auto type = GENERATE(CabinetType::OpenBack1x12,
                         CabinetType::ClosedBack4x12,
                         CabinetType::Combo1x12);

    const double sr   = 48000.0;
    const int    N    = 4096;
    const int    SKIP = 512;  // > 2 × 256-tap IR length

    std::vector<float> sig(N), orig(N);
    for (int i = 0; i < N; ++i)
        sig[i] = orig[i] = 0.5f * std::sin(2.0f * (float)M_PI * 1000.0f * (float)i / (float)sr);

    CabinetIR cab;
    cab.setEnabled(true);
    cab.setType(type);
    cab.prepare(sr, N);
    cab.process(sig.data(), N);

    float rmsIn  = rmsFrom(orig, SKIP);
    float rmsOut = rmsFrom(sig,  SKIP);

    // ±1 dB = factors 0.891 to 1.122
    CHECK(rmsOut > rmsIn * 0.89f);
    CHECK(rmsOut < rmsIn * 1.13f);
}

TEST_CASE("CabinetIR: 10 kHz is attenuated for each cabinet type", "[cabinet]")
{
    // All types roll off above their respective cutoff frequencies.
    // Theoretical gains at 10 kHz (IR normalised at 1 kHz):
    //   1x12:  (4000/10000)^1.0  ≈ 0.400  →  threshold 0.55
    //   4x12:  (3000/10000)^0.5  ≈ 0.548  →  threshold 0.70
    //   combo: (5000/10000)^1.0  = 0.500  →  threshold 0.65
    struct Cfg { CabinetType type; float maxGain; };
    auto cfg = GENERATE(
        Cfg{CabinetType::OpenBack1x12,   0.55f},
        Cfg{CabinetType::ClosedBack4x12, 0.70f},
        Cfg{CabinetType::Combo1x12,      0.65f}
    );

    const double sr   = 48000.0;
    const int    N    = 4096;
    const int    SKIP = 512;

    auto sig = makeSine(10000.0f, (float)N / (float)sr, sr);
    std::vector<float> orig = sig;

    CabinetIR cab;
    cab.setEnabled(true);
    cab.setType(cfg.type);
    cab.prepare(sr, N);
    cab.process(sig.data(), N);

    float rmsIn  = rmsFrom(orig, SKIP);
    float rmsOut = rmsFrom(sig,  SKIP);

    CHECK(rmsOut < rmsIn * cfg.maxGain);
}

TEST_CASE("CabinetIR: 4x12 attenuates 10 kHz less than 1x12 (shallower rolloff)", "[cabinet]")
{
    // 4x12 uses −3 dB/oct rolloff above 3 kHz vs 1x12's −6 dB/oct above 4 kHz.
    // At 10 kHz: 4x12 gain ≈ 0.548, 1x12 gain ≈ 0.400 — 4x12 must have more HF energy.
    float rms1x12 = cabRmsAt(10000.0f, CabinetType::OpenBack1x12);
    float rms4x12 = cabRmsAt(10000.0f, CabinetType::ClosedBack4x12);

    CHECK(rms4x12 > rms1x12);
}

TEST_CASE("CabinetIR: combo attenuates 10 kHz less than 1x12 (higher cutoff frequency)", "[cabinet]")
{
    // Combo cutoff at 5 kHz vs 1x12's 4 kHz; same rolloff slope → less attenuation at 10 kHz.
    // At 10 kHz: combo gain ≈ 0.500, 1x12 gain ≈ 0.400.
    float rms1x12  = cabRmsAt(10000.0f, CabinetType::OpenBack1x12);
    float rmsCombo = cabRmsAt(10000.0f, CabinetType::Combo1x12);

    CHECK(rmsCombo > rms1x12);
}

TEST_CASE("CabinetIR: resonance peak is louder than 10 kHz rolloff for each type", "[cabinet]")
{
    // Each cabinet type has a +6 dB resonance peak at a characteristic frequency.
    // That frequency should produce at least 50% more RMS than a deeply-attenuated 10 kHz tone,
    // confirming both the resonance peak and the HF rolloff are active.
    //
    // Amplitude 0.3 keeps the resonance-boosted output (×2) well within ±1.0.
    struct Cfg { CabinetType type; float f_res; };
    auto cfg = GENERATE(
        Cfg{CabinetType::OpenBack1x12,   120.0f},
        Cfg{CabinetType::ClosedBack4x12,  80.0f},
        Cfg{CabinetType::Combo1x12,      180.0f}
    );

    const int N = 8192;  // ≥ 10 periods at 80 Hz@48 kHz for reliable RMS

    float rmsRes = cabRmsAt(cfg.f_res,  cfg.type, 48000.0, N, 512, 0.3f);
    float rms10k = cabRmsAt(10000.0f,   cfg.type, 48000.0, N, 512, 0.3f);

    CHECK(rmsRes > rms10k * 1.5f);
}

TEST_CASE("CabinetIR: 1x12 and 4x12 have distinct frequency responses at 10 kHz", "[cabinet]")
{
    // Catches the case where setType() silently ignores the change and both types
    // would use the same IR.  The known 0.548 vs 0.400 difference must be measurable.
    float rms1x12 = cabRmsAt(10000.0f, CabinetType::OpenBack1x12);
    float rms4x12 = cabRmsAt(10000.0f, CabinetType::ClosedBack4x12);

    // At minimum the 4x12 should be at least 10% louder at 10 kHz
    CHECK(rms4x12 > rms1x12 * 1.10f);
}

TEST_CASE("CabinetIR: each cabinet type has a distinct resonance frequency", "[cabinet]")
{
    // Verifies that each type's peak lands at a clearly different frequency.
    // 1x12 (120 Hz) must be louder than 4x12 (80 Hz) peak when measured at 120 Hz,
    // and 4x12 must be louder than 1x12 when measured at 80 Hz — they have different shapes.
    const int N = 8192;

    float rms1x12_at120 = cabRmsAt(120.0f, CabinetType::OpenBack1x12,   48000.0, N, 512, 0.3f);
    float rms4x12_at120 = cabRmsAt(120.0f, CabinetType::ClosedBack4x12, 48000.0, N, 512, 0.3f);

    float rms1x12_at80  = cabRmsAt(80.0f,  CabinetType::OpenBack1x12,   48000.0, N, 512, 0.3f);
    float rms4x12_at80  = cabRmsAt(80.0f,  CabinetType::ClosedBack4x12, 48000.0, N, 512, 0.3f);

    // 1x12 peak is at 120 Hz — should be relatively louder there than 4x12 (peak at 80 Hz)
    // i.e., 1x12_at120 / 1x12_at80 > 4x12_at120 / 4x12_at80
    float ratio1x12 = rms1x12_at120 / rms1x12_at80;
    float ratio4x12 = rms4x12_at120 / rms4x12_at80;
    CHECK(ratio1x12 > ratio4x12);
}

// ---------------------------------------------------------------------------
// Type-switching
// ---------------------------------------------------------------------------

TEST_CASE("CabinetIR: setType clears convolution history", "[cabinet]")
{
    // Prime with a loud signal on 1x12, then switch to 4x12.
    // setType() resets the internal history buffer — subsequent silence must be silent.
    const double sr = 48000.0;
    const int N = 512;

    auto loud = makeSine(440.0f, (float)N / (float)sr, sr);
    for (auto& s : loud) s *= 5.0f;

    CabinetIR cab;
    cab.setEnabled(true);
    cab.prepare(sr, N);
    cab.process(loud.data(), N);  // prime 1x12 history

    cab.setType(CabinetType::ClosedBack4x12);  // must reset history

    std::vector<float> silence(N, 0.0f);
    cab.process(silence.data(), N);
    for (float s : silence)
        CHECK(s == Catch::Approx(0.0f).margin(1e-6f));
}

TEST_CASE("CabinetIR: setType before prepare uses correct IR on first process", "[cabinet]")
{
    // setType() called before prepare() must still activate the correct IR.
    // Verify by comparing 10 kHz response of 1x12 vs 4x12.
    const double sr = 48000.0;
    const int    N  = 4096;
    const int SKIP  = 512;

    auto rms10k = [&](CabinetType type) {
        auto sig = makeSine(10000.0f, (float)N / (float)sr, sr);
        CabinetIR cab;
        cab.setEnabled(true);
        cab.setType(type);      // set before prepare
        cab.prepare(sr, N);
        cab.process(sig.data(), N);
        return rmsFrom(sig, SKIP);
    };

    float rms1x12 = rms10k(CabinetType::OpenBack1x12);
    float rms4x12 = rms10k(CabinetType::ClosedBack4x12);

    CHECK(rms4x12 > rms1x12 * 1.10f);
}

TEST_CASE("CabinetIR: 1x12 output is non-trivially different from input", "[cabinet]")
{
    // Preserve the original characterisation test; 10 kHz must be attenuated < 60%.
    auto sig = makeSine(10000.0f, 0.05f, 48000.0);
    std::vector<float> orig = sig;

    CabinetIR cab;
    cab.setEnabled(true);
    cab.prepare(48000.0, (int)sig.size());
    cab.process(sig.data(), (int)sig.size());

    float rmsIn = 0.0f, rmsOut = 0.0f;
    for (size_t i = 256; i < orig.size(); ++i) {
        rmsIn  += orig[i] * orig[i];
        rmsOut += sig[i]  * sig[i];
    }
    rmsIn  = std::sqrt(rmsIn  / (orig.size() - 256));
    rmsOut = std::sqrt(rmsOut / (sig.size()  - 256));
    CHECK(rmsOut < rmsIn * 0.6f);
}
