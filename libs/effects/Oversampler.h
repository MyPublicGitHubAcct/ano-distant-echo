#pragma once
#include "DelayLine.h"
#include <algorithm>
#include <cmath>
#include <vector>

// Compile-time oversampling factor for Overdrive (set to 1 to bypass).
#ifndef OVERDRIVE_OVERSAMPLING
#define OVERDRIVE_OVERSAMPLING 4
#endif

// 4× polyphase FIR upsample/downsample.
//
// Filter: 128-tap Kaiser-windowed LP FIR designed at runtime (matches scipy's
// firwin(128, 1.0/L, window=('kaiser', 8.0))):
//   - Cutoff at the original Nyquist (1/L of upsampled Nyquist)
//   - Kaiser β = 8.0 → ≈ 80 dB minimum stopband attenuation
//   - Linear phase (Type-I symmetric FIR)
//
// Polyphase structure: L phases × (N_TAPS/L) taps each.
// Upsample delay line is at the original rate (N_TAPS/L – 1 samples).
// Downsample uses direct FIR convolution on the oversampled stream.
class Oversampler {
public:
    static constexpr int L      = OVERDRIVE_OVERSAMPLING;
    static constexpr int N_TAPS = 128;
    static constexpr int M      = N_TAPS / L;   // taps per polyphase phase

    void prepare(double /*sampleRate*/, int /*blockSize*/) {
        designFilter();
        upDL.resize(M);
        dnDL.resize(N_TAPS);
    }

    void reset() {
        upDL.reset();
        dnDL.reset();
    }

    // Upsample numSamples input samples → L*numSamples output samples.
    // out must have capacity L*numSamples.
    void upsample(const float* in, float* out, int numSamples) {
        if constexpr (L == 1) {
            std::copy(in, in + numSamples, out);
            return;
        }
        for (int n = 0; n < numSamples; ++n) {
            upDL.push(in[n]);
            // Compute L output samples — one per polyphase phase
            for (int k = 0; k < L; ++k) {
                double acc = 0.0;
                const float* ph = phases[k].data();
                for (int m = 0; m < M; ++m)
                    acc += ph[m] * upDL.read(m + 1);
                out[n * L + k] = (float)(acc * L); // ×L compensates zero-insertion
            }
        }
    }

    // Downsample L*numSamples oversampled input → numSamples output.
    // in must have L*numSamples samples.
    //
    // Output at position m is computed immediately after inserting in[L*m],
    // matching scipy's upfirdn(h, x, down=L) which decimates at indices 0, L, 2L, …
    void downsample(const float* in, float* out, int numSamples) {
        if constexpr (L == 1) {
            std::copy(in, in + numSamples, out);
            return;
        }
        int outIdx = 0;
        const int total = numSamples * L;
        for (int i = 0; i < total && outIdx < numSamples; ++i) {
            dnDL.push(in[i]);

            // Output at every L-th input starting at i=0 (i.e. i % L == 0),
            // using the delay-line state that includes in[i].
            if (i % L == 0) {
                double acc = 0.0;
                for (int m = 0; m < N_TAPS; ++m)
                    acc += h[m] * dnDL.read(m + 1);
                out[outIdx++] = (float)acc;
            }
        }
    }

private:
    // Full filter coefficients (h[0..N_TAPS-1])
    float h[N_TAPS] = {};
    // Polyphase phases: phases[k][m] = h[k + L*m]
    std::vector<float> phases[L];

    // Upsample state: delay line at original rate (M samples)
    DelayLine<float> upDL;
    // Downsample state: delay line at oversampled rate (N_TAPS samples)
    DelayLine<float> dnDL;

    // Modified Bessel function I0(x) — series expansion (converges for all x).
    static double besselI0(double x) {
        double sum = 1.0, t = 1.0;
        for (int k = 1; k <= 25; ++k) {
            double half = x * 0.5 / k;
            t *= half * half;
            sum += t;
            if (t < 1e-15 * sum) break;
        }
        return sum;
    }

    // Design the LP FIR identical to scipy's
    // firwin(N_TAPS, 1.0/L, window=('kaiser', 8.0)).
    //
    // scipy's firwin with a single cutoff fc (normalized 0–1, where 1 = Nyquist):
    //   h_ideal[n] = sin(π·fc·(n-M/2)) / (π·(n-M/2))   [= fc for n=M/2]
    //   h[n]       = h_ideal[n] · kaiser(n, β)
    //   h          /= sum(h)                              [DC gain = 1]
    void designFilter() {
        const double fc   = 1.0 / L;     // cutoff normalized to Nyquist (1.0)
        const double beta = 8.0;
        const double M2   = N_TAPS - 1;  // M in scipy formula
        const double I0b  = besselI0(beta);

        double sum = 0.0;
        for (int n = 0; n < N_TAPS; ++n) {
            double t = n - M2 * 0.5;
            double ideal = (t == 0.0) ? fc
                                      : std::sin(M_PI * fc * t) / (M_PI * t);
            double arg = 1.0 - 4.0 * t * t / (M2 * M2);
            double win = (arg <= 0.0) ? 0.0 : besselI0(beta * std::sqrt(arg)) / I0b;
            h[n] = (float)(ideal * win);
            sum += h[n];
        }
        // Normalize to unit DC gain (matches scipy's firwin normalization)
        for (int n = 0; n < N_TAPS; ++n)
            h[n] = (float)(h[n] / sum);

        // Split into L polyphase phases
        for (int k = 0; k < L; ++k) {
            phases[k].resize(M);
            for (int m = 0; m < M; ++m)
                phases[k][static_cast<size_t>(m)] = h[k + L * m];
        }
    }
};
