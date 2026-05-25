#pragma once
#include <cmath>

// First-order (one-pole) low-pass IIR filter.
//
//   alpha = 1 - exp(-2π·fc / fs)
//   state += alpha * (x - state)     ↔     state = (1-alpha)·state + alpha·x
//
// alpha = 0 freezes the output; alpha = 1 gives pass-through.
// Use setCutoff() to set the cutoff frequency and sample rate, or setAlpha()
// to supply the coefficient directly when you have already computed it.
class OnePoleLP {
public:
    void setCutoff(double sampleRate, double fc) {
        alpha = (float)(1.0 - std::exp(-2.0 * M_PI * fc / sampleRate));
        state = 0.0f;
    }

    void setAlpha(float a) { alpha = a; }

    float process(float x) {
        state += alpha * (x - state);
        return state;
    }

    float get() const { return state; }
    void  reset()     { state = 0.0f; }

private:
    float alpha = 0.0f;
    float state = 0.0f;
};
