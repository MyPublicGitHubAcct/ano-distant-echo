#pragma once
#include <cmath>

// Asymmetric peak envelope follower (attack/release IIR).
//
// Tracks abs(x) with separate attack and release time constants:
//
//   coeff = exp(-1 / (sampleRate * timeMs * 0.001))
//   if abs(x) > state:  state = attackCoeff  * state + (1-attackCoeff)  * abs(x)
//   else:               state = releaseCoeff * state + (1-releaseCoeff) * abs(x)
//
// attackMs should be shorter than releaseMs (e.g. 1 ms attack / 100 ms release
// for pick sensitivity; 5 ms / 500 ms for ducking).
class EnvelopeFollower {
public:
    void prepare(double sampleRate, double attackMs, double releaseMs) {
        attackCoeff  = (float)std::exp(-1.0 / (sampleRate * attackMs  * 0.001));
        releaseCoeff = (float)std::exp(-1.0 / (sampleRate * releaseMs * 0.001));
        state = 0.0f;
    }

    void reset() { state = 0.0f; }

    // Process one sample; returns the new envelope level.
    float process(float x) {
        float absX = std::abs(x);
        float c = (absX > state) ? attackCoeff : releaseCoeff;
        state = c * state + (1.0f - c) * absX;
        return state;
    }

    float get() const { return state; }

private:
    float attackCoeff  = 0.0f;
    float releaseCoeff = 0.0f;
    float state        = 0.0f;
};
