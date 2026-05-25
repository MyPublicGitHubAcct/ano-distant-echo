#pragma once
#include <cmath>

// One-pole parameter smoother. Settles in ~rampMs milliseconds.
class SmoothedValue {
public:
    void prepare(double sampleRate, double rampMs = 20.0) {
        coeff = std::exp(-1.0 / (sampleRate * rampMs * 0.001));
        current = target;
    }

    void setTarget(float v) { target = v; }
    void reset(float v) { target = current = v; }

    float next() {
        current = current * (float)coeff + target * (float)(1.0 - coeff);
        return current;
    }

    float get() const { return current; }
    bool isSmoothing() const { return std::abs(current - target) > 1e-6f; }

private:
    float current = 0.0f;
    float target  = 0.0f;
    double coeff  = 0.0;
};
