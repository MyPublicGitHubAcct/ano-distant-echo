#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <vector>

#include "EnvelopeFollower.h"
#include "OnePoleFilter.h"

// ---------------------------------------------------------------------------
// EnvelopeFollower
// ---------------------------------------------------------------------------

TEST_CASE("EnvelopeFollower: zero input → zero output", "[envelope]") {
    EnvelopeFollower ef;
    ef.prepare(48000.0, 1.0, 100.0);
    for (int i = 0; i < 1024; ++i)
        CHECK(ef.process(0.0f) == Catch::Approx(0.0f).margin(1e-9f));
}

TEST_CASE("EnvelopeFollower: reset clears state", "[envelope]") {
    EnvelopeFollower ef;
    ef.prepare(48000.0, 1.0, 100.0);
    for (int i = 0; i < 512; ++i) ef.process(1.0f);
    ef.reset();
    CHECK(ef.get() == Catch::Approx(0.0f).margin(1e-9f));
    CHECK(ef.process(0.0f) == Catch::Approx(0.0f).margin(1e-9f));
}

TEST_CASE("EnvelopeFollower: tracks abs(x) — negative input raises envelope", "[envelope]") {
    EnvelopeFollower ef;
    ef.prepare(48000.0, 1.0, 100.0);
    float v = ef.process(-1.0f);
    CHECK(v > 0.0f);
}

TEST_CASE("EnvelopeFollower: attack rises toward unit step", "[envelope]") {
    // After 5× the attack time constant the envelope should be close to 1.
    const double sr = 48000.0;
    const double attackMs = 5.0;
    EnvelopeFollower ef;
    ef.prepare(sr, attackMs, 500.0);

    int attackSamples = (int)(sr * attackMs * 0.001 * 5.0);  // 5τ
    for (int i = 0; i < attackSamples; ++i) ef.process(1.0f);
    CHECK(ef.get() > 0.9f);
}

TEST_CASE("EnvelopeFollower: release decays from peak after signal ends", "[envelope]") {
    const double sr = 48000.0;
    const double releaseMs = 50.0;
    EnvelopeFollower ef;
    ef.prepare(sr, 1.0, releaseMs);

    // Saturate the envelope
    int prime = (int)(sr * 0.1);
    for (int i = 0; i < prime; ++i) ef.process(1.0f);
    CHECK(ef.get() > 0.95f);

    // Release for 5τ — should be close to 0
    int releaseSamples = (int)(sr * releaseMs * 0.001 * 5.0);
    for (int i = 0; i < releaseSamples; ++i) ef.process(0.0f);
    CHECK(ef.get() < 0.1f);
}

TEST_CASE("EnvelopeFollower: attack is faster than release", "[envelope]") {
    // Measure how many samples to reach 63% of steady state for each
    const double sr = 48000.0;
    EnvelopeFollower ef;
    ef.prepare(sr, 1.0, 100.0);  // 1 ms attack, 100 ms release

    // Attack: count samples to reach 0.63
    int attackCount = 0;
    while (ef.get() < 0.63f && attackCount < 100000) {
        ef.process(1.0f);
        ++attackCount;
    }

    // Prime, then release: count samples for level to fall to 0.37
    for (int i = 0; i < 10000; ++i) ef.process(1.0f);
    int releaseCount = 0;
    while (ef.get() > 0.37f && releaseCount < 100000) {
        ef.process(0.0f);
        ++releaseCount;
    }

    CHECK(attackCount < releaseCount);
}

TEST_CASE("EnvelopeFollower: output bounded in [0, 1] for unit-amplitude input", "[envelope]") {
    double sr = GENERATE(44100.0, 48000.0, 96000.0);
    EnvelopeFollower ef;
    ef.prepare(sr, 1.0, 100.0);
    for (int i = 0; i < 4096; ++i) {
        float x = std::sin(2.0f * (float)M_PI * 440.0f * i / (float)sr);
        float v = ef.process(x);
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= 1.0f + 1e-6f);
        REQUIRE(std::isfinite(v));
    }
}

// ---------------------------------------------------------------------------
// OnePoleLP
// ---------------------------------------------------------------------------

TEST_CASE("OnePoleLP: zero input → zero output", "[onepole]") {
    OnePoleLP f;
    f.setCutoff(48000.0, 1000.0);
    for (int i = 0; i < 1024; ++i)
        CHECK(f.process(0.0f) == Catch::Approx(0.0f).margin(1e-9f));
}

TEST_CASE("OnePoleLP: reset clears state", "[onepole]") {
    OnePoleLP f;
    f.setCutoff(48000.0, 1000.0);
    for (int i = 0; i < 512; ++i) f.process(1.0f);
    f.reset();
    CHECK(f.get() == Catch::Approx(0.0f).margin(1e-9f));
    CHECK(f.process(0.0f) == Catch::Approx(0.0f).margin(1e-9f));
}

TEST_CASE("OnePoleLP: DC input → DC output (unity gain at DC)", "[onepole]") {
    OnePoleLP f;
    f.setCutoff(48000.0, 1000.0);
    // Run enough samples for transient to decay
    for (int i = 0; i < 10000; ++i) f.process(1.0f);
    CHECK(f.get() == Catch::Approx(1.0f).margin(0.001f));
}

TEST_CASE("OnePoleLP: attenuates well above cutoff", "[onepole]") {
    // Measure gain at 10× fc — should be well below −20 dB for a 1st-order filter
    const double sr = 48000.0;
    const double fc = 1000.0;
    OnePoleLP f;
    f.setCutoff(sr, fc);

    int N = (int)(sr * 0.2);
    int tail = N / 4;
    double rmsIn = 0.0, rmsOut = 0.0;
    for (int i = 0; i < N; ++i) {
        float x = (float)std::sin(2.0 * M_PI * fc * 10.0 * i / sr);
        float y = f.process(x);
        if (i >= N - tail) {
            rmsIn  += (double)x * x;
            rmsOut += (double)y * y;
        }
    }
    double gainDb = 20.0 * std::log10(std::sqrt(rmsOut / tail) /
                                      (std::sqrt(rmsIn  / tail) + 1e-30));
    CHECK(gainDb < -18.0);  // 1st-order theory: −20 dB/decade; discrete warping gives ~−19.4 dB here
}

TEST_CASE("OnePoleLP: setAlpha(1) is pass-through, setAlpha(0) freezes at 0", "[onepole]") {
    OnePoleLP f;

    // alpha=1 → state tracks input exactly
    f.setAlpha(1.0f);
    for (int i = 0; i < 64; ++i) {
        float x = (float)i / 64.0f;
        CHECK(f.process(x) == Catch::Approx(x).margin(1e-6f));
    }

    // alpha=0 → state stays at 0
    f.reset();
    f.setAlpha(0.0f);
    for (int i = 0; i < 64; ++i)
        CHECK(f.process(1.0f) == Catch::Approx(0.0f).margin(1e-9f));
}

TEST_CASE("OnePoleLP: no NaN/Inf at various sample rates and cutoffs", "[onepole]") {
    double sr = GENERATE(44100.0, 48000.0, 96000.0);
    double fc = GENERATE(100.0, 1000.0, 4000.0);
    OnePoleLP f;
    f.setCutoff(sr, fc);
    for (int i = 0; i < 1024; ++i) {
        float x = std::sin(2.0f * (float)M_PI * (float)fc * i / (float)sr);
        REQUIRE(std::isfinite(f.process(x)));
    }
}
