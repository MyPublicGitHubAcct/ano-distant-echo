#pragma once
#include <cmath>

// Second-order IIR (biquad) filter with static design helpers.
//
// Coefficients are stored as {b0, b1, b2, a1, a2} (a0 normalised to 1).
// State is held internally; call reset() between test frames or on reset().
//
// Design functions are static — they return a Coeffs struct that can then be
// applied to any BiquadFilter instance via setCoeffs().  All take (fs, fc, ...)
// where fs is sample rate and fc is the corner frequency in Hz.
class BiquadFilter {
public:
    struct Coeffs {
        float b0=0, b1=0, b2=0, a1=0, a2=0;
    };

    void setCoeffs(const Coeffs& c) { coeffs = c; }
    const Coeffs& getCoeffs() const { return coeffs; }

    float process(float x) {
        float y = coeffs.b0*x + coeffs.b1*x1 + coeffs.b2*x2
                - coeffs.a1*y1 - coeffs.a2*y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }

    void reset() { x1 = x2 = y1 = y2 = 0.0f; }

    // -----------------------------------------------------------------------
    // Audio EQ Cookbook high shelf.  gain_db > 0 boosts, < 0 cuts.
    static Coeffs designHighShelf(double fs, double fc, double gain_db) {
        double A  = std::pow(10.0, gain_db / 40.0);
        double w0 = 2.0 * M_PI * fc / fs;
        double cw = std::cos(w0), sw = std::sin(w0);
        double alpha = sw / 2.0 * std::sqrt((A + 1.0/A) * (1.0/M_SQRT1_2 - 1.0) + 2.0);
        double sqA  = std::sqrt(A);
        double b0 =  A * ((A+1) + (A-1)*cw + 2*sqA*alpha);
        double b1 = -2*A* ((A-1) + (A+1)*cw);
        double b2 =  A * ((A+1) + (A-1)*cw - 2*sqA*alpha);
        double a0 =       (A+1) - (A-1)*cw + 2*sqA*alpha;
        double a1 =   2 * ((A-1) - (A+1)*cw);
        double a2 =       (A+1) - (A-1)*cw - 2*sqA*alpha;
        return { (float)(b0/a0), (float)(b1/a0), (float)(b2/a0),
                 (float)(a1/a0), (float)(a2/a0) };
    }

    // Biquad peaking EQ (Audio EQ Cookbook).
    // gain_db > 0 boosts, < 0 cuts.  bw_octaves sets the bandwidth.
    static Coeffs designPeaking(double fs, double fc, double gain_db, double bw_octaves) {
        double A  = std::pow(10.0, gain_db / 40.0);
        double w0 = 2.0 * M_PI * fc / fs;
        double alpha = std::sin(w0) * std::sinh(std::log(2.0) / 2.0 * bw_octaves * w0 / std::sin(w0));
        double b0 = 1.0 + alpha * A;
        double b1 = -2.0 * std::cos(w0);
        double b2 = 1.0 - alpha * A;
        double a0 = 1.0 + alpha / A;
        double a1 = -2.0 * std::cos(w0);
        double a2 = 1.0 - alpha / A;
        return { (float)(b0/a0), (float)(b1/a0), (float)(b2/a0),
                 (float)(a1/a0), (float)(a2/a0) };
    }

    // 2nd-order Butterworth low-pass at fc.
    static Coeffs designButterworthLP(double fs, double fc) {
        double omega = 2.0 * M_PI * fc / fs;
        double cosW  = std::cos(omega), sinW = std::sin(omega);
        double alpha = sinW / (2.0 * M_SQRT1_2);
        double a0    = 1.0 + alpha;
        Coeffs c;
        c.b0 = (float)(((1.0 - cosW) / 2.0) / a0);
        c.b1 = (float)((1.0 - cosW) / a0);
        c.b2 = c.b0;
        c.a1 = (float)((-2.0 * cosW) / a0);
        c.a2 = (float)((1.0 - alpha) / a0);
        return c;
    }

    // 2nd-order Butterworth high-pass at fc.
    // Shares denominator coefficients with designButterworthLP at same fc.
    static Coeffs designButterworthHP(double fs, double fc) {
        double omega = 2.0 * M_PI * fc / fs;
        double cosW  = std::cos(omega), sinW = std::sin(omega);
        double alpha = sinW / (2.0 * M_SQRT1_2);
        double a0    = 1.0 + alpha;
        Coeffs c;
        c.b0 = (float)(((1.0 + cosW) / 2.0) / a0);
        c.b1 = (float)((-(1.0 + cosW)) / a0);
        c.b2 = c.b0;
        c.a1 = (float)((-2.0 * cosW) / a0);
        c.a2 = (float)((1.0 - alpha) / a0);
        return c;
    }

private:
    Coeffs coeffs;
    float x1=0, x2=0, y1=0, y2=0;
};
