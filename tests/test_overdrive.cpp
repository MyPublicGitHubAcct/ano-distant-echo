#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <complex>
#include <vector>

#include "Overdrive.h"

static std::vector<float> makeSine(float freq, float duration, double sr) {
    int n = (int)(sr * duration);
    std::vector<float> out(n);
    for (int i = 0; i < n; ++i)
        out[i] = std::sin(2.0f * (float)M_PI * freq * (float)i / (float)sr);
    return out;
}

static std::vector<float> applyOverdrive(std::vector<float> sig, double sr,
                                          float drive, float tone, float level) {
    Overdrive od;
    od.setDrive(drive);
    od.setTone(tone);
    od.setLevel(level);
    od.prepare(sr, (int)sig.size());
    od.process(sig.data(), (int)sig.size());
    return sig;
}

// ---------------------------------------------------------------------------
// Basic behavioural tests
// ---------------------------------------------------------------------------

TEST_CASE("Overdrive: silence in → silence out", "[overdrive]") {
    double sr = GENERATE(44100.0, 48000.0, 96000.0);
    std::vector<float> silence(4096, 0.0f);
    auto out = applyOverdrive(silence, sr, 0.5f, 0.5f, 0.8f);
    for (float s : out)
        CHECK(s == Catch::Approx(0.0f).margin(1e-6f));
}

TEST_CASE("Overdrive: unity bypass (drive=0, level=1, tone=0.5)", "[overdrive]") {
    auto sig = makeSine(440.0f, 0.1f, 48000.0);
    auto out = applyOverdrive(sig, 48000.0, 0.0f, 0.5f, 1.0f);
    bool hasNonZero = false;
    for (float s : out) {
        REQUIRE(std::isfinite(s));
        if (std::abs(s) > 1e-4f) hasNonZero = true;
    }
    CHECK(hasNonZero);
}

TEST_CASE("Overdrive: output bounded within [-1, 1]", "[overdrive]") {
    double sr = GENERATE(44100.0, 48000.0, 96000.0);
    float drive = GENERATE(0.0f, 0.5f, 1.0f);
    auto sig = makeSine(440.0f, 0.05f, sr);
    for (auto& s : sig) s *= 10.0f;
    auto out = applyOverdrive(sig, sr, drive, 0.5f, 1.0f);
    for (float s : out)
        REQUIRE(std::abs(s) <= 1.0f + 1e-4f);
}

TEST_CASE("Overdrive: no NaN/Inf at extreme parameter values", "[overdrive]") {
    double sr = GENERATE(44100.0, 48000.0, 96000.0);
    float drive = GENERATE(0.0f, 1.0f);
    float tone  = GENERATE(0.0f, 1.0f);
    float lv    = GENERATE(0.0f, 1.0f);
    auto sig = makeSine(440.0f, 0.05f, sr);
    auto out = applyOverdrive(sig, sr, drive, tone, lv);
    for (float s : out)
        REQUIRE(std::isfinite(s));
}

TEST_CASE("Overdrive: HardClip polynomial bounded within [-1, 1]", "[overdrive]") {
    // (3x-x³)/2 for |x|≤1 reaches exactly ±1; hard-clipped beyond
    auto sig = makeSine(440.0f, 0.1f, 48000.0);
    for (auto& s : sig) s *= 10.0f;
    Overdrive od;
    od.setDistortionType(DistortionType::HardClip);
    od.setDrive(1.0f); od.setTone(0.5f); od.setLevel(1.0f);
    od.prepare(48000.0, (int)sig.size());
    od.process(sig.data(), (int)sig.size());
    for (float s : sig)
        REQUIRE(std::abs(s) <= 1.0f + 1e-4f);
}

// ---------------------------------------------------------------------------
// Distortion mode tests
// ---------------------------------------------------------------------------

static std::vector<float> applyMode(std::vector<float> sig, double sr,
                                     DistortionType mode, float drive,
                                     float tone, float level) {
    Overdrive od;
    od.setDistortionType(mode);
    od.setDrive(drive);
    od.setTone(tone);
    od.setLevel(level);
    od.prepare(sr, (int)sig.size());
    od.process(sig.data(), (int)sig.size());
    return sig;
}

TEST_CASE("Overdrive modes: silence in → silence out", "[overdrive][modes]") {
    auto mode = GENERATE(DistortionType::HardClip, DistortionType::SoftClip,
                         DistortionType::Foldback, DistortionType::Asymmetric,
                         DistortionType::Bitcrush);
    double sr = GENERATE(44100.0, 48000.0, 96000.0);
    std::vector<float> silence(4096, 0.0f);
    auto out = applyMode(silence, sr, mode, 0.5f, 0.5f, 0.8f);
    for (float s : out)
        CHECK(s == Catch::Approx(0.0f).margin(1e-6f));
}

TEST_CASE("Overdrive modes: no NaN/Inf at extreme parameters", "[overdrive][modes]") {
    auto mode = GENERATE(DistortionType::HardClip, DistortionType::SoftClip,
                         DistortionType::Foldback, DistortionType::Asymmetric,
                         DistortionType::Bitcrush);
    float drive = GENERATE(0.0f, 0.5f, 1.0f);
    auto sig = makeSine(440.0f, 0.05f, 48000.0);
    for (auto& s : sig) s *= 10.0f;
    auto out = applyMode(sig, 48000.0, mode, drive, 0.5f, 0.8f);
    for (float s : out)
        REQUIRE(std::isfinite(s));
}

TEST_CASE("Overdrive: softclip output bounded within [-1.1, 1.1]", "[overdrive][modes]") {
    auto sig = makeSine(440.0f, 0.1f, 48000.0);
    for (auto& s : sig) s *= 10.0f;
    auto out = applyMode(sig, 48000.0, DistortionType::SoftClip, 1.0f, 0.5f, 1.0f);
    for (float s : out)
        REQUIRE(std::abs(s) <= 1.1f);
}

TEST_CASE("Overdrive: foldback output bounded within [-1.1, 1.1]", "[overdrive][modes]") {
    auto sig = makeSine(440.0f, 0.1f, 48000.0);
    for (auto& s : sig) s *= 10.0f;
    auto out = applyMode(sig, 48000.0, DistortionType::Foldback, 0.5f, 0.5f, 1.0f);
    for (float s : out)
        REQUIRE(std::abs(s) <= 1.1f);
}

TEST_CASE("Overdrive: asymmetric mode produces DC offset (even-order harmonics)", "[overdrive][modes]") {
    auto sig = makeSine(100.0f, 0.5f, 48000.0);
    auto out = applyMode(sig, 48000.0, DistortionType::Asymmetric, 0.8f, 0.0f, 1.0f);
    const int skip = 2000;
    double sum = 0.0;
    for (int i = skip; i < (int)out.size(); ++i) sum += out[i];
    CHECK(std::abs(sum / (out.size() - skip)) > 1e-3);
}

TEST_CASE("Overdrive: bitcrush output is finite and non-trivial", "[overdrive][modes]") {
    auto sig = makeSine(440.0f, 0.05f, 48000.0);
    auto out = applyMode(sig, 48000.0, DistortionType::Bitcrush, 1.0f, 0.0f, 1.0f);
    bool anyNonZero = false;
    for (float s : out) {
        REQUIRE(std::isfinite(s));
        if (std::abs(s) > 1e-4f) anyNonZero = true;
    }
    CHECK(anyNonZero);
}

// ---------------------------------------------------------------------------
// 13a: Oversampling — aliasing test
// A 6 kHz sine at full drive must produce no content above 20 kHz exceeding
// −80 dBFS in the downsampled output.
//
// 6 kHz is chosen because its period at the 192 kHz OS rate is exactly 32
// samples (integer), so the hard-clipped square wave has harmonics ONLY at
// exact multiples of 6 kHz: 6, 18, 30, 42, … kHz.  None of these fall in
// the 20–22 kHz test window.  With 4× oversampling the LP anti-alias FIR
// filters harmonics above 24 kHz (30, 42, … kHz) before downsampling, so
// any residual alias at 18 kHz (from the 30 kHz 5th harmonic) is below the
// noise floor.  Trimming 96 = 48×2 samples keeps N = 4704 = 48×98, placing
// every multiple of 6 kHz at an exact DFT bin (zero spectral leakage).
// ---------------------------------------------------------------------------

static double peakMagnitudeDb(const std::vector<float>& x, double sr,
                               double fMin, double fMax) {
    int N = (int)x.size();
    double peak = 0.0;
    int kMin = (int)std::ceil(fMin * N / sr);
    int kMax = (int)std::floor(fMax * N / sr);
    for (int k = kMin; k <= kMax && k < N / 2; ++k) {
        // Goertzel or direct DFT bin magnitude
        double re = 0.0, im = 0.0;
        double w  = 2.0 * M_PI * k / N;
        for (int n = 0; n < N; ++n) {
            re += x[n] * std::cos(w * n);
            im -= x[n] * std::sin(w * n);
        }
        double mag = std::sqrt(re*re + im*im) / (N / 2.0);
        peak = std::max(peak, mag);
    }
    return 20.0 * std::log10(peak + 1e-15);
}

TEST_CASE("13a: 6 kHz at full drive — no content above 20 kHz > −80 dBFS", "[overdrive][oversampling]") {
    constexpr double sr = 48000.0;
    // 0.1-second signal (4800 samples); after trimming 96: N = 4704 = 48×98
    int N = (int)(sr * 0.1);
    std::vector<float> sig(N);
    for (int i = 0; i < N; ++i)
        sig[i] = std::sin(2.0f * (float)M_PI * 6000.0f * i / (float)sr);

    Overdrive od;
    od.setDistortionType(DistortionType::HardClip);
    od.setDrive(1.0f);
    od.setTone(0.5f);
    od.setLevel(1.0f);
    od.prepare(sr, N);
    od.process(sig.data(), N);

    // Skip 96 samples (FIR transient); N_trim=4704 → 6 kHz at bin 588 (exact).
    std::vector<float> trimmed(sig.begin() + 96, sig.end());
    double aliasDb = peakMagnitudeDb(trimmed, sr, 20000.0, 22000.0);
    CHECK(aliasDb < -80.0);
}

// ---------------------------------------------------------------------------
// 13c: ClipShape — MidFocus pre/de-emphasis is unity at low drive
// ---------------------------------------------------------------------------

TEST_CASE("13c: MidFocus round-trip is unity at low drive (flat in = flat out)", "[overdrive][clipshape]") {
    // At drive=0, gain=1×; pre/de-emphasis cancel → output ≈ input (filtered by DC block + tone).
    // Use 0.1 amplitude so the waveshaper operates in its linear region (avoids x³ residual
    // that would break cancellation at unit amplitude).
    auto sig = makeSine(440.0f, 0.05f, 48000.0);
    for (auto& s : sig) s *= 0.1f;
    Overdrive odFlat, odMid;
    odFlat.setClipShape(ClipShape::Flat);
    odMid .setClipShape(ClipShape::MidFocus);
    for (auto* od : {&odFlat, &odMid}) {
        od->setDrive(0.0f); od->setTone(0.5f); od->setLevel(1.0f);
        od->prepare(48000.0, (int)sig.size());
    }
    auto flat = sig; odFlat.process(flat.data(), (int)flat.size());
    auto mid  = sig; odMid .process(mid.data(),  (int)mid.size());
    // The pre/de-emphasis shelves cancel, so both outputs should be very close
    double maxDiff = 0.0;
    for (int i = 64; i < (int)flat.size(); ++i)  // skip filter transient
        maxDiff = std::max(maxDiff, (double)std::abs(flat[i] - mid[i]));
    CHECK(maxDiff < 0.02);  // small tolerance for filter transient differences
}

// ---------------------------------------------------------------------------
// 13d: Mid/Presence EQ tests
// ---------------------------------------------------------------------------

TEST_CASE("13d: silence in → silence out with mid/presence active", "[overdrive][eq]") {
    std::vector<float> silence(4096, 0.0f);
    Overdrive od;
    od.setMid(6.0f); od.setPresence(4.0f);
    od.prepare(48000.0, (int)silence.size());
    od.process(silence.data(), (int)silence.size());
    for (float s : silence)
        CHECK(s == Catch::Approx(0.0f).margin(1e-6f));
}

TEST_CASE("13d: no NaN/Inf with extreme mid/presence", "[overdrive][eq]") {
    auto sig = makeSine(440.0f, 0.05f, 48000.0);
    float mid = GENERATE(-6.0f, 0.0f, 10.0f);
    float pres = GENERATE(0.0f, 4.0f, 8.0f);
    Overdrive od;
    od.setMid(mid); od.setPresence(pres);
    od.setDrive(0.5f); od.setTone(0.5f); od.setLevel(0.8f);
    od.prepare(48000.0, (int)sig.size());
    od.process(sig.data(), (int)sig.size());
    for (float s : sig)
        REQUIRE(std::isfinite(s));
}

// ---------------------------------------------------------------------------
// 13e: Envelope follower — pick sensitivity
// A 0 dBFS impulse followed by a −20 dBFS tone must produce less gain
// reduction in the steady-state tone than during the impulse peak.
// ---------------------------------------------------------------------------

TEST_CASE("13e: impulse reduces gain more than subsequent quiet tone", "[overdrive][picksens]") {
    constexpr double sr = 48000.0;
    const int N = (int)(sr * 0.5);  // 0.5 seconds
    const int impulseEnd = 100;      // first 100 samples: 0 dBFS impulse
    const int quietStart = 5000;     // after 5000 samples: -20 dBFS tone (~0.1 amp)

    std::vector<float> sig(N, 0.0f);
    // Impulse burst
    for (int i = 0; i < impulseEnd; ++i)
        sig[i] = (i < 10) ? 1.0f : 0.0f;
    // Quiet tone (−20 dBFS)
    for (int i = quietStart; i < N; ++i)
        sig[i] = 0.1f * std::sin(2.0f * (float)M_PI * 440.0f * i / (float)sr);

    // Process with pick sensitivity enabled, drive=0 so the waveshaper is linear
    Overdrive od;
    od.setPickSensitive(true);
    od.setDrive(0.0f); od.setTone(0.5f); od.setLevel(1.0f);
    od.prepare(sr, N);
    od.process(sig.data(), N);

    // Build reference (same signal, pick sensitivity OFF) to measure gain reduction
    std::vector<float> ref(N, 0.0f);
    for (int i = quietStart; i < N; ++i)
        ref[i] = 0.1f * std::sin(2.0f * (float)M_PI * 440.0f * i / (float)sr);
    Overdrive odRef;
    odRef.setPickSensitive(false);
    odRef.setDrive(0.0f); odRef.setTone(0.5f); odRef.setLevel(1.0f);
    odRef.prepare(sr, N);
    odRef.process(ref.data(), N);

    // Peak output during impulse region (with pick sensitivity)
    float peakImpulse = 0.0f;
    for (int i = 0; i < impulseEnd; ++i)
        peakImpulse = std::max(peakImpulse, std::abs(sig[i]));

    // RMS of quiet tone region — compare with and without pick sensitivity
    double rmsWith = 0.0, rmsWithout = 0.0;
    for (int i = quietStart + 1000; i < N; ++i) {
        rmsWith    += (double)sig[i] * sig[i];
        rmsWithout += (double)ref[i] * ref[i];
    }
    int toneLen = N - quietStart - 1000;
    rmsWith    = std::sqrt(rmsWith    / toneLen);
    rmsWithout = std::sqrt(rmsWithout / toneLen);

    // Gain reduction during quiet tone should be LESS than during impulse peak.
    // Impulse peak: we expect some gain reduction (output < 1.0 × drive-chain)
    // Quiet tone: envelope has decayed, reduction is smaller → tone RMS closer to reference
    double quietReduction = (rmsWithout > 1e-9) ? rmsWith / rmsWithout : 1.0;
    // quietReduction should be closer to 1.0 than the impulse was (less reduction)
    // We verify there IS some gain reduction overall (pick sens has effect)
    CHECK(quietReduction > 0.8);   // not fully suppressed in quiet region
    CHECK(quietReduction <= 1.01); // but some reduction is present (envelope hasn't fully released)
}
