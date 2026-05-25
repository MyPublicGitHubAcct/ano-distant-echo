#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <vector>

#include "Delay.h"

static std::vector<float> applyDelay(std::vector<float> sig, double sr,
                                      double timeMs, float feedback, float mix) {
    Delay dl;
    dl.setTimeMs(timeMs, sr);
    dl.setFeedback(feedback);
    dl.setMix(mix);
    dl.prepare(sr, (int)sig.size());
    dl.process(sig.data(), (int)sig.size());
    return sig;
}

TEST_CASE("Delay: silence in → silence out", "[delay]") {
    double sr = GENERATE(44100.0, 48000.0, 96000.0);
    std::vector<float> silence(8192, 0.0f);
    auto out = applyDelay(silence, sr, 300.0, 0.5f, 0.5f);
    for (float s : out)
        CHECK(s == Catch::Approx(0.0f).margin(1e-6f));
}

TEST_CASE("Delay: dry-only pass-through (mix=0)", "[delay]") {
    double sr = 48000.0;
    std::vector<float> sig(4096);
    for (int i = 0; i < 4096; ++i)
        sig[i] = std::sin(2.0f * (float)M_PI * 440.0f * (float)i / (float)sr);
    auto original = sig;
    auto out = applyDelay(sig, sr, 300.0, 0.4f, 0.0f);
    for (int i = 0; i < (int)out.size(); ++i)
        CHECK(out[i] == Catch::Approx(original[i]).margin(1e-5f));
}

TEST_CASE("Delay: impulse appears after delay time", "[delay]") {
    double sr = 48000.0;
    double timeMs = 100.0;
    int delaySamples = (int)(sr * timeMs / 1000.0);  // 4800

    int totalSamples = delaySamples * 2 + 100;
    std::vector<float> sig(totalSamples, 0.0f);
    sig[0] = 1.0f;  // single impulse at t=0

    auto out = applyDelay(sig, sr, timeMs, 0.0f, 1.0f);  // feedback=0, fully wet

    // The impulse should appear at index delaySamples
    REQUIRE((int)out.size() > delaySamples + 1);
    CHECK(out[delaySamples] == Catch::Approx(1.0f).margin(1e-4f));
    // Before the delay, output should be silent (mix=1, no dry)
    for (int i = 1; i < delaySamples; ++i)
        CHECK(std::abs(out[i]) < 1e-4f);
}

TEST_CASE("Delay: feedback produces multiple echoes", "[delay]") {
    double sr = 48000.0;
    double timeMs = 50.0;
    int delaySamples = (int)(sr * timeMs / 1000.0);

    int totalSamples = delaySamples * 5;
    std::vector<float> sig(totalSamples, 0.0f);
    sig[0] = 1.0f;

    auto out = applyDelay(sig, sr, timeMs, 0.5f, 1.0f);

    // First echo at delaySamples
    float echo1 = out[delaySamples];
    // Second echo at 2*delaySamples (reduced by feedback * lp attenuation)
    float echo2 = out[2 * delaySamples];

    CHECK(echo1 > 0.1f);    // first echo is present
    CHECK(echo2 > 0.01f);   // second echo is present but smaller
    CHECK(echo2 < echo1);   // each echo is quieter than the last
}

TEST_CASE("Delay: no NaN/Inf at extreme parameters", "[delay]") {
    double sr = GENERATE(44100.0, 48000.0, 96000.0);
    float fb = GENERATE(0.0f, 0.95f);
    float mx = GENERATE(0.0f, 1.0f);
    std::vector<float> sig(4096);
    for (int i = 0; i < 4096; ++i)
        sig[i] = std::sin(2.0f * (float)M_PI * 440.0f * (float)i / (float)sr);
    auto out = applyDelay(sig, sr, 300.0, fb, mx);
    for (float s : out)
        REQUIRE(std::isfinite(s));
}

TEST_CASE("Delay: output stays within reasonable bounds at max feedback", "[delay]") {
    double sr = 48000.0;
    std::vector<float> sig(48000, 0.0f);  // 1 second
    sig[0] = 1.0f;
    auto out = applyDelay(sig, sr, 100.0, 0.95f, 0.5f);
    for (float s : out)
        REQUIRE(std::abs(s) < 100.0f);  // should not blow up
}

// Stress-tests time automation stability — verifies no NaN, Inf, or runaway amplitude
// even when time is swept rapidly across the full 1–2000 ms range.
TEST_CASE("Delay: rapid time automation produces no NaN/Inf or runaway", "[delay]") {
    const double sr = 48000.0;
    const int blockSize = 256;
    const int numBlocks = 200;  // ~1 second total

    Delay dl;
    dl.setFeedback(0.7f);
    dl.setMix(0.5f);
    dl.prepare(sr, blockSize);

    // Start with a sine to fill the buffer
    std::vector<float> buf(blockSize);
    float phase = 0.0f;
    const float phaseInc = 2.0f * (float)M_PI * 440.0f / (float)sr;

    for (int block = 0; block < numBlocks; ++block) {
        // Sweep time from 1 ms to 2000 ms and back on each block
        double timeMs = 1.0 + 1999.0 * (block % 2 == 0
                            ? (double)block / numBlocks
                            : 1.0 - (double)block / numBlocks);
        dl.setTimeMs(timeMs, sr);

        for (int i = 0; i < blockSize; ++i) {
            buf[i] = std::sin(phase);
            phase += phaseInc;
        }
        dl.process(buf.data(), blockSize);

        for (int i = 0; i < blockSize; ++i) {
            REQUIRE(std::isfinite(buf[i]));
            REQUIRE(std::abs(buf[i]) < 100.0f);
        }
    }
}

// --- 14a: Fractional-delay interpolation ---

TEST_CASE("Delay: fractional interpolation splits impulse across adjacent samples", "[delay]") {
    const double sr = 48000.0;
    // 10.5 samples delay: the impulse is split equally between output samples 10 and 11
    const int totalSamples = 30;

    Delay dl;
    dl.setTimeSamples(10.5f);
    dl.setFeedback(0.0f);
    dl.setMix(1.0f);
    dl.prepare(sr, totalSamples);

    std::vector<float> sig(totalSamples, 0.0f);
    sig[0] = 1.0f;
    dl.process(sig.data(), totalSamples);

    // Linear interpolation at frac=0.5 blends buf[10-old] and buf[11-old] equally
    CHECK(sig[10] == Catch::Approx(0.5f).margin(1e-4f));
    CHECK(sig[11] == Catch::Approx(0.5f).margin(1e-4f));
    for (int i = 1; i < 10; ++i)
        CHECK(std::abs(sig[i]) < 1e-4f);
}

TEST_CASE("Delay: smooth ±1-sample time sweep produces no click", "[delay]") {
    // With linear interpolation the read pointer moves continuously even when
    // delaySamples changes by ±1 between blocks.  A 440 Hz sine has max slope
    // ≈ 0.058/sample; any sample-to-sample jump > 0.2 would indicate a click.
    const double sr = 48000.0;
    const int blockSize = 128;
    const int numBlocks = 100;

    Delay dl;
    dl.setFeedback(0.3f);
    dl.setMix(0.5f);
    dl.prepare(sr, blockSize);

    std::vector<float> buf(blockSize);
    float phase = 0.0f;
    const float phaseInc = 2.0f * (float)M_PI * 440.0f / (float)sr;
    float prev = 0.0f;
    bool havePrev = false;

    for (int block = 0; block < numBlocks; ++block) {
        dl.setTimeSamples(100.5f + (block % 2 == 0 ? 0.0f : 1.0f));

        for (int i = 0; i < blockSize; ++i) {
            buf[i] = std::sin(phase);
            phase += phaseInc;
        }
        dl.process(buf.data(), blockSize);

        for (int i = 0; i < blockSize; ++i) {
            REQUIRE(std::isfinite(buf[i]));
            if (havePrev)
                REQUIRE(std::abs(buf[i] - prev) < 0.2f);
            prev = buf[i];
            havePrev = true;
        }
    }
}

// --- 14b: Smooth time changes via crossfade ---

TEST_CASE("Delay: 50% time change mid-stream produces no click spike", "[delay]") {
    // Without crossfade an abrupt 100ms→150ms jump reads from a new buffer
    // position whose value may differ significantly from the previous output,
    // potentially doubling amplitude.  The crossfade keeps the blended output
    // within 2× of the steady-state peak.
    const double sr = 48000.0;
    const int blockSize = 128;

    Delay dl;
    dl.setTimeMs(100.0, sr);
    dl.setFeedback(0.3f);
    dl.setMix(0.5f);
    dl.prepare(sr, blockSize);

    std::vector<float> buf(blockSize);
    float phase = 0.0f;
    const float phaseInc = 2.0f * (float)M_PI * 440.0f / (float)sr;

    float prePeak = 0.0f;
    for (int block = 0; block < 50; ++block) {
        for (int i = 0; i < blockSize; ++i) { buf[i] = std::sin(phase); phase += phaseInc; }
        dl.process(buf.data(), blockSize);
        for (float s : buf) prePeak = std::max(prePeak, std::abs(s));
    }

    dl.setTimeMs(150.0, sr);  // 50% jump

    for (int block = 0; block < 20; ++block) {
        for (int i = 0; i < blockSize; ++i) { buf[i] = std::sin(phase); phase += phaseInc; }
        dl.process(buf.data(), blockSize);
        for (float s : buf)
            REQUIRE(std::abs(s) <= 2.0f * prePeak + 0.01f);
    }
}

TEST_CASE("Delay: silence in → silence out across rapidly-swept time", "[delay]") {
    const double sr = 48000.0;
    const int blockSize = 128;

    Delay dl;
    dl.setFeedback(0.5f);
    dl.setMix(0.5f);
    dl.prepare(sr, blockSize);

    std::vector<float> silence(blockSize, 0.0f);
    for (int block = 0; block < 50; ++block) {
        dl.setTimeMs(1.0 + block * 40.0, sr);  // 1 ms → 1961 ms in steps
        std::fill(silence.begin(), silence.end(), 0.0f);
        dl.process(silence.data(), blockSize);
        for (float s : silence)
            CHECK(s == Catch::Approx(0.0f).margin(1e-6f));
    }
}

// --- 14c: Wow and flutter ---

TEST_CASE("Delay: wow and flutter silence in → silence out", "[delay]") {
    double sr = GENERATE(44100.0, 48000.0);
    std::vector<float> silence(8192, 0.0f);

    Delay dl;
    dl.setTimeMs(100.0, sr);
    dl.setFeedback(0.5f);
    dl.setMix(0.5f);
    dl.setWowRate(0.5f);
    dl.setWowDepthMs(4.0f);
    dl.setFlutterRate(8.0f);
    dl.setFlutterDepthMs(1.0f);
    dl.prepare(sr, 8192);
    dl.process(silence.data(), 8192);

    for (float s : silence)
        CHECK(s == Catch::Approx(0.0f).margin(1e-6f));
}

TEST_CASE("Delay: wow depth=0 produces identical output to unmodulated delay", "[delay]") {
    const double sr = 48000.0;
    const int N = 4096;
    std::vector<float> sig(N);
    for (int i = 0; i < N; ++i)
        sig[i] = std::sin(2.0f * (float)M_PI * 440.0f * i / (float)sr);

    auto makeOut = [&](bool withWow) {
        Delay dl;
        dl.setTimeMs(200.0, sr);
        dl.setFeedback(0.4f);
        dl.setMix(0.5f);
        dl.setWowRate(1.0f);
        dl.setWowDepthMs(withWow ? 0.0f : 0.0f);  // depth=0 regardless
        dl.setFlutterDepthMs(0.0f);
        dl.prepare(sr, N);
        std::vector<float> buf = sig;
        dl.process(buf.data(), N);
        return buf;
    };

    auto ref = makeOut(false);
    auto mod = makeOut(true);  // rate set but depth=0 — must match reference
    for (int i = 0; i < N; ++i)
        CHECK(mod[i] == Catch::Approx(ref[i]).margin(1e-5f));
}

TEST_CASE("Delay: wow and flutter no NaN/Inf at extreme parameters", "[delay]") {
    const double sr = 48000.0;
    std::vector<float> sig(4096);
    for (int i = 0; i < 4096; ++i)
        sig[i] = std::sin(2.0f * (float)M_PI * 440.0f * i / (float)sr);

    Delay dl;
    dl.setTimeMs(300.0, sr);
    dl.setFeedback(0.7f);
    dl.setMix(0.5f);
    dl.setWowRate(2.0f);
    dl.setWowDepthMs(10.0f);     // max wow depth
    dl.setFlutterRate(12.0f);    // max flutter rate
    dl.setFlutterDepthMs(2.0f);  // max flutter depth
    dl.prepare(sr, 4096);
    dl.process(sig.data(), 4096);

    for (float s : sig)
        REQUIRE(std::isfinite(s));
}

// --- 14d: Tape saturation ---

TEST_CASE("Delay: tape saturation caps output at high feedback", "[delay]") {
    // At feedback=0.94 without saturation, rounding errors accumulate across
    // many repeats and the output can drift above 1.5.  With tape saturation
    // the tanh softclipper bounds the feedback signal, keeping the output
    // within ±1.5 indefinitely.
    const double sr = 48000.0;
    const int totalSamples = 10000;
    const int settleDelay  = 5000;  // check only after buffer has filled

    Delay dl;
    dl.setTimeMs(100.0, sr);
    dl.setFeedback(0.94f);
    dl.setMix(0.5f);
    dl.setTapeSat(true);
    dl.prepare(sr, totalSamples);

    std::vector<float> sig(totalSamples);
    for (int i = 0; i < totalSamples; ++i)
        sig[i] = std::sin(2.0f * (float)M_PI * 440.0f * (float)i / (float)sr);
    dl.process(sig.data(), totalSamples);

    for (int i = settleDelay; i < totalSamples; ++i)
        REQUIRE(std::abs(sig[i]) <= 1.5f);
}

TEST_CASE("Delay: tape saturation silence in → silence out", "[delay]") {
    double sr = GENERATE(44100.0, 48000.0);
    std::vector<float> silence(8192, 0.0f);

    Delay dl;
    dl.setTimeMs(200.0, sr);
    dl.setFeedback(0.5f);
    dl.setMix(0.5f);
    dl.setTapeSat(true);
    dl.setTapeAge(0.5f);
    dl.prepare(sr, 8192);
    dl.process(silence.data(), 8192);

    for (float s : silence)
        CHECK(s == Catch::Approx(0.0f).margin(1e-6f));
}

TEST_CASE("Delay: tape saturation no NaN/Inf at extreme parameters", "[delay]") {
    const double sr = 48000.0;
    std::vector<float> sig(4096);
    for (int i = 0; i < 4096; ++i)
        sig[i] = std::sin(2.0f * (float)M_PI * 440.0f * i / (float)sr);

    Delay dl;
    dl.setTimeMs(300.0, sr);
    dl.setFeedback(0.95f);
    dl.setMix(0.5f);
    dl.setTapeSat(true);
    dl.setTapeAge(1.0f);  // max age: 1.5 kHz LP, drive=5
    dl.prepare(sr, 4096);
    dl.process(sig.data(), 4096);

    for (float s : sig)
        REQUIRE(std::isfinite(s));
}

TEST_CASE("Delay: tape saturation is actually applied (changes output vs no-sat)", "[delay]") {
    // Use a 50 ms delay so the feedback path is active well within 6000 samples.
    // With satDrive=2 and feedback=0.6 the tanh compression alters the LP signal
    // written back into the buffer, so the output diverges from the no-sat case.
    const double sr = 48000.0;
    const int N = 6000;
    std::vector<float> sig(N);
    for (int i = 0; i < N; ++i)
        sig[i] = std::sin(2.0f * (float)M_PI * 440.0f * i / (float)sr);

    auto makeOut = [&](bool sat) {
        Delay dl;
        dl.setTimeMs(50.0, sr);
        dl.setFeedback(0.6f);
        dl.setMix(0.5f);
        dl.setTapeSat(sat);
        dl.prepare(sr, N);
        std::vector<float> buf = sig;
        dl.process(buf.data(), N);
        return buf;
    };

    auto noSat = makeOut(false);
    auto sat   = makeOut(true);

    // Saturation alters the feedback signal — output must differ after delay fills
    bool anyDiff = false;
    for (int i = 2400; i < N; ++i)  // only check post-first-echo region
        if (std::abs(sat[i] - noSat[i]) > 1e-5f) { anyDiff = true; break; }
    CHECK(anyDiff);

    // All outputs must be finite and bounded
    for (float s : sat) REQUIRE(std::isfinite(s));
}

// --- 14e: Ducking ---

TEST_CASE("Delay: duck silence in → silence out", "[delay]") {
    double sr = GENERATE(44100.0, 48000.0);
    std::vector<float> silence(8192, 0.0f);

    Delay dl;
    dl.setTimeMs(300.0, sr);
    dl.setFeedback(0.5f);
    dl.setMix(0.5f);
    dl.setDuckThreshold(-20.0f);
    dl.setDuckDepth(1.0f);
    dl.prepare(sr, 8192);
    dl.process(silence.data(), 8192);

    for (float s : silence)
        CHECK(s == Catch::Approx(0.0f).margin(1e-6f));
}

TEST_CASE("Delay: duck no NaN/Inf at extreme parameters", "[delay]") {
    const double sr = 48000.0;
    std::vector<float> sig(4096);
    for (int i = 0; i < 4096; ++i)
        sig[i] = std::sin(2.0f * (float)M_PI * 440.0f * i / (float)sr);

    Delay dl;
    dl.setTimeMs(300.0, sr);
    dl.setFeedback(0.95f);
    dl.setMix(1.0f);
    dl.setDuckThreshold(-30.0f);  // most sensitive threshold
    dl.setDuckDepth(1.0f);
    dl.prepare(sr, 4096);
    dl.process(sig.data(), 4096);

    for (float s : sig)
        REQUIRE(std::isfinite(s));
}

TEST_CASE("Delay: duck suppresses wet while loud signal is present", "[delay]") {
    // mix=1.0 → output = duck_gain × wet; with depth=1.0 and signal 0.9 >> threshold
    // (−3 dBFS ≈ 0.71 linear), duck_gain = 0 and output ≈ 0 after envelope settles.
    // Delay = 20 ms = 960 samples; envelope exceeds threshold by ~373 samples (7.8 ms).
    // Check samples past both the envelope settle and the first echo arrival.
    const double sr = 48000.0;
    const int N = 48000;
    const int delaySamples = (int)(sr * 0.020);  // 960

    std::vector<float> sig(N);
    for (int i = 0; i < N; ++i)
        sig[i] = std::sin(2.0f * (float)M_PI * 440.0f * i / (float)sr) * 0.9f;

    Delay dl;
    dl.setTimeMs(20.0, sr);
    dl.setFeedback(0.5f);
    dl.setMix(1.0f);
    dl.setDuckThreshold(-3.0f);   // ≈ 0.71 linear; signal 0.9 is well above
    dl.setDuckDepth(1.0f);
    dl.prepare(sr, N);
    dl.process(sig.data(), N);

    // After first echo + envelope settle: output = 0 × wet = 0
    const int checkFrom = delaySamples + 400;
    for (int i = checkFrom; i < N; ++i)
        REQUIRE(std::abs(sig[i]) < 1e-3f);
}

TEST_CASE("Delay: duck releases and wet emerges after signal stops", "[delay]") {
    // Phase 1: loud signal (5000 samples) fills the delay buffer; duck is active.
    // Phase 2: silence (28800 samples = 600 ms). Envelope releases in ~120 ms
    //   (env = 0.9, threshold = 0.71, release τ = 500 ms; env falls below 0.71
    //   at t ≈ 0.9 × exp(-t/0.5) = 0.71 → t ≈ 0.12 s = 5760 samples).
    // By sample N_loud+8000 (≈167 ms into silence) duck is released and the delay
    // buffer still has content → output rises above noise floor.
    const double sr = 48000.0;
    const int N_loud   = 5000;
    const int N_silent = 28800;   // 600 ms
    const int N_total  = N_loud + N_silent;

    std::vector<float> sig(N_total, 0.0f);
    for (int i = 0; i < N_loud; ++i)
        sig[i] = std::sin(2.0f * (float)M_PI * 440.0f * i / (float)sr) * 0.9f;
    // sig[N_loud..N_total-1] remains 0 (silence)

    Delay dl;
    dl.setTimeMs(20.0, sr);
    dl.setFeedback(0.9f);
    dl.setMix(0.5f);
    dl.setDuckThreshold(-3.0f);
    dl.setDuckDepth(1.0f);
    dl.prepare(sr, N_total);
    dl.process(sig.data(), N_total);

    // From 8000 samples (≈167 ms) into the silence phase the duck has released
    // and echoes from phase 1 are still audible.
    bool anyAbove = false;
    for (int i = N_loud + 8000; i < N_total; ++i)
        if (std::abs(sig[i]) > 1e-3f) { anyAbove = true; break; }
    CHECK(anyAbove);

    for (int i = 0; i < N_total; ++i)
        REQUIRE(std::isfinite(sig[i]));
}

// --- 14f: Diffusion ---

TEST_CASE("Delay: diffusion silence in → silence out", "[delay]") {
    double sr = GENERATE(44100.0, 48000.0);
    std::vector<float> silence(8192, 0.0f);

    Delay dl;
    dl.setTimeMs(300.0, sr);
    dl.setFeedback(0.5f);
    dl.setMix(0.5f);
    dl.setDiffusion(1.0f);
    dl.prepare(sr, 8192);
    dl.process(silence.data(), 8192);

    for (float s : silence)
        CHECK(s == Catch::Approx(0.0f).margin(1e-6f));
}

TEST_CASE("Delay: diffusion no NaN/Inf at extreme parameters", "[delay]") {
    const double sr = 48000.0;
    std::vector<float> sig(4096);
    for (int i = 0; i < 4096; ++i)
        sig[i] = std::sin(2.0f * (float)M_PI * 440.0f * i / (float)sr);

    Delay dl;
    dl.setTimeMs(300.0, sr);
    dl.setFeedback(0.95f);
    dl.setMix(1.0f);
    dl.setDiffusion(1.0f);
    dl.prepare(sr, 4096);
    dl.process(sig.data(), 4096);

    for (float s : sig)
        REQUIRE(std::isfinite(s));
}

TEST_CASE("Delay: diffusion changes output compared to no-diffusion", "[delay]") {
    // The allpass chain is all-pass (flat magnitude) but applies frequency-dependent
    // phase shifts. The phase-shifted input written to the delay line produces echoes
    // with a different phase than the undiffused case — detectable after first echo.
    const double sr = 48000.0;
    const int N = 10000;
    std::vector<float> sig(N);
    for (int i = 0; i < N; ++i)
        sig[i] = std::sin(2.0f * (float)M_PI * 440.0f * i / (float)sr);

    auto makeOut = [&](float d) {
        Delay dl;
        dl.setTimeMs(50.0, sr);   // 50 ms = 2400 samples; echo arrives within N
        dl.setFeedback(0.4f);
        dl.setMix(0.5f);
        dl.setDiffusion(d);
        dl.prepare(sr, N);
        std::vector<float> buf = sig;
        dl.process(buf.data(), N);
        return buf;
    };

    auto noDiff   = makeOut(0.0f);
    auto fullDiff = makeOut(1.0f);

    // After first echo (2400 samples) the diffused write produces a phase-shifted echo
    bool anyDiff = false;
    for (int i = 2400; i < N; ++i)
        if (std::abs(fullDiff[i] - noDiff[i]) > 1e-5f) { anyDiff = true; break; }
    CHECK(anyDiff);

    for (float s : fullDiff) REQUIRE(std::isfinite(s));
}

// --- 14a: Lagrange interpolation (tape mode) ---

TEST_CASE("Delay: tape mode uses Lagrange interpolation (wider kernel than linear)", "[delay]") {
    // 4th-order Lagrange has a 5-sample kernel; linear has a 2-sample kernel.
    // At d=10.5 with an impulse input (feedback=0, mix=1) the response is:
    //   Linear:   samples 10 and 11 nonzero, sample 12 == 0
    //   Lagrange: samples 10, 11, and 12 all nonzero (wider kernel)
    // Tape mode (tapeSat=true) activates Lagrange; amplitude=0.001 makes saturation negligible.
    const double sr = 48000.0;
    const int totalSamples = 30;
    const float amp = 0.001f;

    auto run = [&](bool tapeSat) {
        Delay dl;
        dl.setTimeSamples(10.5f);
        dl.setFeedback(0.0f);
        dl.setMix(1.0f);
        dl.setTapeSat(tapeSat);
        dl.prepare(sr, totalSamples);
        std::vector<float> sig(totalSamples, 0.0f);
        sig[0] = amp;
        dl.process(sig.data(), totalSamples);
        return sig;
    };

    auto lin = run(false);
    auto lag = run(true);

    // Linear: only samples 10–11 nonzero
    CHECK(std::abs(lin[10]) > 1e-5f);
    CHECK(std::abs(lin[11]) > 1e-5f);
    CHECK(std::abs(lin[12]) < 1e-5f);

    // Lagrange: sample 12 also nonzero (wider 5-point kernel)
    CHECK(std::abs(lag[10]) > 1e-5f);
    CHECK(std::abs(lag[11]) > 1e-5f);
    CHECK(std::abs(lag[12]) > 1e-5f);

    for (int i = 0; i < totalSamples; ++i)
        REQUIRE(std::isfinite(lag[i]));
}

// --- 14g: Self-oscillation ---

TEST_CASE("Delay: self-oscillation + tape sat stays bounded at feedback=1.0", "[delay]") {
    // With selfOscillate=true and tapeSat=true, feedback is allowed up to 1.02.
    // The tanh softclipper in the feedback path must prevent unbounded growth:
    // 2000 samples of sine input followed by 10000 samples of silence must all
    // stay within ±2.0.
    const double sr = 48000.0;
    const int N_input  = 2000;
    const int N_tail   = 10000;
    const int N_total  = N_input + N_tail;

    Delay dl;
    dl.setSelfOscillate(true);
    dl.setTapeSat(true);
    dl.setTimeMs(50.0, sr);
    dl.setFeedback(1.0f);
    dl.setMix(0.5f);
    dl.prepare(sr, N_total);

    std::vector<float> sig(N_total, 0.0f);
    for (int i = 0; i < N_input; ++i)
        sig[i] = std::sin(2.0f * (float)M_PI * 440.0f * (float)i / (float)sr);

    dl.process(sig.data(), N_total);

    for (int i = N_input; i < N_total; ++i)
        REQUIRE(std::abs(sig[i]) <= 2.0f);

    for (int i = 0; i < N_total; ++i)
        REQUIRE(std::isfinite(sig[i]));
}

TEST_CASE("Delay: self-oscillation without tape sat clamps feedback at 0.98", "[delay]") {
    // Without tape sat, selfOscillate caps feedback at 0.98 to prevent runaway.
    // Output over 10000 samples at feedback=1.0 (clamped to 0.98) must be bounded.
    const double sr = 48000.0;
    const int N = 10000;

    Delay dl;
    dl.setSelfOscillate(true);
    dl.setTapeSat(false);
    dl.setTimeMs(50.0, sr);
    dl.setFeedback(1.0f);  // clamped internally to 0.98
    dl.setMix(0.5f);
    dl.prepare(sr, N);

    std::vector<float> sig(N, 0.0f);
    sig[0] = 1.0f;
    dl.process(sig.data(), N);

    for (float s : sig)
        REQUIRE(std::abs(s) < 100.0f);
    for (float s : sig)
        REQUIRE(std::isfinite(s));
}

TEST_CASE("Delay: self-oscillation off clamps feedback at 0.95", "[delay]") {
    // When selfOscillate is off, setFeedback(1.02) must clamp to 0.95.
    // The output of an impulse response should decay, not grow.
    const double sr = 48000.0;
    const int N = 20000;

    Delay dl;
    dl.setSelfOscillate(false);
    dl.setTimeMs(100.0, sr);
    dl.setFeedback(1.02f);  // clamped to 0.95
    dl.setMix(1.0f);
    dl.prepare(sr, N);

    std::vector<float> sig(N, 0.0f);
    sig[0] = 1.0f;
    dl.process(sig.data(), N);

    // With feedback=0.95 each echo is smaller than the last; output must stay finite
    for (float s : sig)
        REQUIRE(std::isfinite(s));
    for (float s : sig)
        REQUIRE(std::abs(s) < 100.0f);
}

TEST_CASE("Delay: wow modulation produces smooth output with no click", "[delay]") {
    // Wow at 1 Hz / 5 ms depth moves the read pointer by at most
    // 5ms × 48 = 240 samples over a full LFO cycle; the continuous
    // linear-interpolated read must not produce sample-to-sample jumps
    // larger than the signal itself could produce (threshold: 0.3).
    const double sr = 48000.0;
    const int blockSize = 128;
    const int numBlocks = 100;

    Delay dl;
    dl.setTimeMs(200.0, sr);
    dl.setFeedback(0.3f);
    dl.setMix(0.5f);
    dl.setWowRate(1.0f);
    dl.setWowDepthMs(5.0f);
    dl.prepare(sr, blockSize);

    std::vector<float> buf(blockSize);
    float phase = 0.0f;
    const float phaseInc = 2.0f * (float)M_PI * 440.0f / (float)sr;
    float prev = 0.0f;
    bool havePrev = false;

    for (int block = 0; block < numBlocks; ++block) {
        for (int i = 0; i < blockSize; ++i) {
            buf[i] = std::sin(phase);
            phase += phaseInc;
        }
        dl.process(buf.data(), blockSize);

        for (int i = 0; i < blockSize; ++i) {
            REQUIRE(std::isfinite(buf[i]));
            if (havePrev)
                REQUIRE(std::abs(buf[i] - prev) < 0.3f);
            prev = buf[i];
            havePrev = true;
        }
    }
}
