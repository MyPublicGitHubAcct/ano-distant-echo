#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <vector>

#include "AllpassFilter.h"
#include "BiquadFilter.h"

// Helper: measure steady-state gain at fc by running a sine long enough for
// transients to decay, then computing RMS of the last quarter of the signal.
static double measureGainDb(BiquadFilter& f, double fc, double fs,
                             double durationSec = 0.2) {
    int N = (int)(fs * durationSec);
    double rmsOut = 0.0, rmsIn = 0.0;
    int tail = N / 4;
    for (int i = 0; i < N; ++i) {
        float x = (float)std::sin(2.0 * M_PI * fc * i / fs);
        float y = f.process(x);
        if (i >= N - tail) {
            rmsOut += (double)y * y;
            rmsIn  += (double)x * x;
        }
    }
    rmsOut = std::sqrt(rmsOut / tail);
    rmsIn  = std::sqrt(rmsIn  / tail);
    return 20.0 * std::log10(rmsOut / (rmsIn + 1e-30));
}

// ---------------------------------------------------------------------------
// BiquadFilter basic behaviour
// ---------------------------------------------------------------------------

TEST_CASE("BiquadFilter: silence in → silence out", "[biquad]") {
    BiquadFilter f;
    f.setCoeffs(BiquadFilter::designButterworthLP(48000.0, 1000.0));
    for (int i = 0; i < 1024; ++i)
        CHECK(f.process(0.0f) == Catch::Approx(0.0f).margin(1e-9f));
}

TEST_CASE("BiquadFilter: reset clears state", "[biquad]") {
    BiquadFilter f;
    f.setCoeffs(BiquadFilter::designButterworthLP(48000.0, 1000.0));
    // Prime with non-zero signal
    for (int i = 0; i < 256; ++i)
        f.process(std::sin(2.0f * (float)M_PI * 100.0f * i / 48000.0f));
    f.reset();
    // After reset, silence in → silence out
    for (int i = 0; i < 64; ++i)
        CHECK(f.process(0.0f) == Catch::Approx(0.0f).margin(1e-9f));
}

TEST_CASE("BiquadFilter: no NaN/Inf from any design", "[biquad]") {
    const double fs = 48000.0;
    BiquadFilter f;

    auto check = [&]() {
        float x = 0.5f;
        for (int i = 0; i < 10; ++i) {
            float y = f.process(x);
            REQUIRE(std::isfinite(y));
            x = y;
        }
        f.reset();
    };

    f.setCoeffs(BiquadFilter::designButterworthLP(fs, 1000.0)); check();
    f.setCoeffs(BiquadFilter::designButterworthHP(fs, 1000.0)); check();
    f.setCoeffs(BiquadFilter::designHighShelf(fs, 4000.0,  6.0)); check();
    f.setCoeffs(BiquadFilter::designHighShelf(fs, 4000.0, -6.0)); check();
    f.setCoeffs(BiquadFilter::designPeaking  (fs,  800.0, 6.0, 1.5)); check();
    f.setCoeffs(BiquadFilter::designPeaking  (fs,  800.0,-6.0, 1.5)); check();
}

// ---------------------------------------------------------------------------
// designButterworthLP
// ---------------------------------------------------------------------------

TEST_CASE("BiquadFilter::designButterworthLP: passes DC (0 dB at dc)", "[biquad][lp]") {
    const double fs = 48000.0;
    BiquadFilter f;
    f.setCoeffs(BiquadFilter::designButterworthLP(fs, 3500.0));
    // Run an impulse and sum — sums to DC gain
    float sum = 0.0f;
    float x = 1.0f;
    for (int i = 0; i < 4096; ++i) {
        sum += f.process(x);
        x = 0.0f;
    }
    CHECK(std::abs(sum - 1.0f) < 0.01f);  // DC gain ≈ 1
}

TEST_CASE("BiquadFilter::designButterworthLP: attenuates above fc", "[biquad][lp]") {
    const double fs = 48000.0;
    const double fc = 3500.0;
    BiquadFilter f;
    f.setCoeffs(BiquadFilter::designButterworthLP(fs, fc));
    // Well above cutoff (10×) → at least −20 dB (1st-order equivalent; 2nd-order gives ~−40)
    double gainHigh = measureGainDb(f, fc * 4.0, fs);
    CHECK(gainHigh < -20.0);
}

TEST_CASE("BiquadFilter::designButterworthLP: gain at fc is near −3 dB", "[biquad][lp]") {
    const double fs = 48000.0;
    const double fc = 3500.0;
    BiquadFilter f;
    f.setCoeffs(BiquadFilter::designButterworthLP(fs, fc));
    double gainAtFc = measureGainDb(f, fc, fs);
    CHECK(gainAtFc == Catch::Approx(-3.01).margin(0.5));
}

// ---------------------------------------------------------------------------
// designButterworthHP
// ---------------------------------------------------------------------------

TEST_CASE("BiquadFilter::designButterworthHP: blocks DC", "[biquad][hp]") {
    const double fs = 48000.0;
    BiquadFilter f;
    f.setCoeffs(BiquadFilter::designButterworthHP(fs, 3500.0));
    // DC gain — impulse response sum should be ≈ 0
    float sum = 0.0f;
    float x = 1.0f;
    for (int i = 0; i < 4096; ++i) {
        sum += f.process(x);
        x = 0.0f;
    }
    CHECK(std::abs(sum) < 0.05f);
}

TEST_CASE("BiquadFilter::designButterworthHP: attenuates below fc", "[biquad][hp]") {
    const double fs = 48000.0;
    const double fc = 3500.0;
    BiquadFilter f;
    f.setCoeffs(BiquadFilter::designButterworthHP(fs, fc));
    double gainLow = measureGainDb(f, fc / 4.0, fs);
    CHECK(gainLow < -20.0);
}

TEST_CASE("BiquadFilter::designButterworthHP: gain at fc is near −3 dB", "[biquad][hp]") {
    const double fs = 48000.0;
    const double fc = 3500.0;
    BiquadFilter f;
    f.setCoeffs(BiquadFilter::designButterworthHP(fs, fc));
    double gainAtFc = measureGainDb(f, fc, fs);
    CHECK(gainAtFc == Catch::Approx(-3.01).margin(0.5));
}

// ---------------------------------------------------------------------------
// designHighShelf
// ---------------------------------------------------------------------------

TEST_CASE("BiquadFilter::designHighShelf: applies correct boost above shelf fc", "[biquad][shelf]") {
    const double fs = 48000.0;
    const double fc = 4000.0;
    const double gain_db = 6.0;
    BiquadFilter f;
    f.setCoeffs(BiquadFilter::designHighShelf(fs, fc, gain_db));
    // Well above shelf: expect ≈ +6 dB
    double gainHigh = measureGainDb(f, fc * 4.0, fs);
    CHECK(gainHigh == Catch::Approx(gain_db).margin(1.0));
}

TEST_CASE("BiquadFilter::designHighShelf: unity gain well below shelf fc", "[biquad][shelf]") {
    const double fs = 48000.0;
    const double fc = 4000.0;
    BiquadFilter f;
    f.setCoeffs(BiquadFilter::designHighShelf(fs, fc, 6.0));
    // Well below shelf → gain ≈ 0 dB
    double gainLow = measureGainDb(f, fc / 8.0, fs);
    CHECK(gainLow == Catch::Approx(0.0).margin(0.5));
}

TEST_CASE("BiquadFilter::designHighShelf: cut (-6 dB) above shelf fc", "[biquad][shelf]") {
    const double fs = 48000.0;
    const double fc = 700.0;
    BiquadFilter f;
    f.setCoeffs(BiquadFilter::designHighShelf(fs, fc, -6.0));
    double gainHigh = measureGainDb(f, fc * 6.0, fs);
    CHECK(gainHigh == Catch::Approx(-6.0).margin(1.0));
}

// ---------------------------------------------------------------------------
// designPeaking
// ---------------------------------------------------------------------------

TEST_CASE("BiquadFilter::designPeaking: boosts at fc", "[biquad][peaking]") {
    const double fs = 48000.0;
    const double fc = 800.0;
    const double gain_db = 6.0;
    BiquadFilter f;
    f.setCoeffs(BiquadFilter::designPeaking(fs, fc, gain_db, 1.5));
    double gainAtFc = measureGainDb(f, fc, fs);
    CHECK(gainAtFc == Catch::Approx(gain_db).margin(1.0));
}

TEST_CASE("BiquadFilter::designPeaking: unity gain far from fc", "[biquad][peaking]") {
    const double fs = 48000.0;
    const double fc = 800.0;
    BiquadFilter f;
    f.setCoeffs(BiquadFilter::designPeaking(fs, fc, 6.0, 1.5));
    // Far above and below fc → near 0 dB
    double gainHigh = measureGainDb(f, 10000.0, fs);
    double gainLow  = measureGainDb(f, 100.0, fs);
    CHECK(gainHigh == Catch::Approx(0.0).margin(1.0));
    CHECK(gainLow  == Catch::Approx(0.0).margin(1.0));
}

TEST_CASE("BiquadFilter::designPeaking: cuts at fc with negative gain", "[biquad][peaking]") {
    const double fs = 48000.0;
    const double fc = 800.0;
    BiquadFilter f;
    f.setCoeffs(BiquadFilter::designPeaking(fs, fc, -6.0, 1.5));
    double gainAtFc = measureGainDb(f, fc, fs);
    CHECK(gainAtFc == Catch::Approx(-6.0).margin(1.0));
}

// ---------------------------------------------------------------------------
// AllpassFilter
// ---------------------------------------------------------------------------

TEST_CASE("AllpassFilter: magnitude is unity (all-pass property)", "[biquad][allpass]") {
    // An allpass must have |H(e^jω)| = 1 at all frequencies.
    // D=8, g=0.5 → poles at |z|=0.5^(1/8)≈0.917, settling time ≈ 424 samples.
    // N=16384 with tail=N/2 → lead-in of 8192 >> 424 samples, so measurement
    // is fully in steady state at all tested frequencies.
    AllpassFilter ap;
    ap.prepare(8, 0.5f);

    double freqs[] = {440.0, 1000.0, 5000.0, 10000.0};
    const double fs = 48000.0;
    const int N    = 16384;
    const int tail = N / 2;

    for (double fc : freqs) {
        ap.reset();
        double rmsIn = 0.0, rmsOut = 0.0;
        for (int i = 0; i < N; ++i) {
            float x = (float)std::sin(2.0 * M_PI * fc * i / fs);
            float y = ap.process(x);
            if (i >= N - tail) {
                rmsIn  += (double)x * x;
                rmsOut += (double)y * y;
            }
        }
        rmsIn  = std::sqrt(rmsIn  / tail);
        rmsOut = std::sqrt(rmsOut / tail);
        double gainDb = 20.0 * std::log10(rmsOut / (rmsIn + 1e-30));
        CHECK(gainDb == Catch::Approx(0.0).margin(0.1));
    }
}

TEST_CASE("AllpassFilter: silence in → silence out", "[biquad][allpass]") {
    AllpassFilter ap;
    ap.prepare(256, 0.5f);
    for (int i = 0; i < 512; ++i)
        CHECK(ap.process(0.0f) == Catch::Approx(0.0f).margin(1e-9f));
}

TEST_CASE("AllpassFilter: reset clears state", "[biquad][allpass]") {
    AllpassFilter ap;
    ap.prepare(128, 0.5f);
    for (int i = 0; i < 256; ++i)
        ap.process(std::sin(2.0f * (float)M_PI * 440.0f * i / 48000.0f));
    ap.reset();
    for (int i = 0; i < 32; ++i)
        CHECK(ap.process(0.0f) == Catch::Approx(0.0f).margin(1e-9f));
}
