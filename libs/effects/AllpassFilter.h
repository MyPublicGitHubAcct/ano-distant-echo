#pragma once
#include "DelayLine.h"

// Single Schroeder allpass stage.
//
// Transfer function:
//   H(z) = (z^-D - g) / (1 - g·z^-D)   |H(jω)| = 1 for all ω
//
// Canonical single-delay-line implementation:
//   v[n] = x[n] + g·v[n-D]
//   y[n] = v[n-D] - g·v[n]
//
// Uses double precision internally to match Python float64 and prevent
// state drift accumulation through recursive feedback loops.
class AllpassFilter {
public:
    void prepare(int delaySamples, float coeff) {
        D = delaySamples;
        g = (double)coeff;
        dl.resize(D + 2);
    }

    // v[n-D] is read before the push so the allpass has exactly D samples of delay.
    float process(float x) {
        double xd    = (double)x;
        double state = dl.read(D);          // v[n-D]
        double vn    = xd + g * state;      // v[n]
        dl.push(vn);
        return (float)(state - g * vn);     // y[n] = v[n-D] - g·v[n]
    }

    void reset() { dl.reset(); }

private:
    DelayLine<double> dl;
    int    D = 0;
    double g = 0.5;
};
