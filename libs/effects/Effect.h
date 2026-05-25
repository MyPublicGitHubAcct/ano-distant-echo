#pragma once
#include <algorithm>

class Effect {
public:
    virtual ~Effect() = default;
    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void process(float* buffer, int numSamples) = 0;

    // Stereo processing: default copies left channel to right (mono-to-stereo passthrough).
    // Stereo effects override this to implement channel interaction.
    virtual void processStereo(float* left, float* right, int numSamples) {
        process(left, numSamples);
        std::copy(left, left + numSamples, right);
    }

    virtual void reset() {}
};
