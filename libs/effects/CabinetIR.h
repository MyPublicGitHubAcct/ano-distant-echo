#pragma once
#include "CabinetIR_data.h"
#include "CabinetIR_data_4x12.h"
#include "CabinetIR_data_combo.h"
#include "Effect.h"
#include <algorithm>
#include <cmath>
#include <vector>

enum class CabinetType {
    OpenBack1x12  = 0,   // 1×12 open-back:   120 Hz peak,  −6 dB/oct above 4 kHz
    ClosedBack4x12 = 1,  // 4×12 closed-back:  80 Hz peak,  −3 dB/oct above 3 kHz
    Combo1x12      = 2,  // 1×12 combo:       180 Hz peak,  −6 dB/oct above 5 kHz
};

// Direct-form FIR speaker cabinet simulation.
//
// The base IRs are designed at 48 kHz (256 taps) via minimum-phase cepstrum.
// prepare() resamples the selected IR to the current sample rate using linear
// interpolation so that resonance and rolloff frequencies stay correct at
// 44.1 kHz, 48 kHz, and 96 kHz.
//
// When disabled, process() is a no-op.
class CabinetIR : public Effect {
public:
    void setEnabled(bool e) { enabled = e; }
    bool isEnabled() const  { return enabled; }

    // Change cabinet type mid-session.  Rebuilds the active IR immediately;
    // also resets the convolution history to avoid transients.
    void setType(CabinetType t) {
        if (t == irType) return;
        irType = t;
        if (currentSampleRate > 0.0) {
            buildActiveIR();
            history.assign(activeIR.size(), 0.0f);
            head = 0;
        }
    }

    CabinetType getType() const { return irType; }

    void prepare(double sampleRate, int /*blockSize*/) override {
        currentSampleRate = sampleRate;
        buildActiveIR();
        history.assign(activeIR.size(), 0.0f);
        head = 0;
    }

    void reset() override {
        std::fill(history.begin(), history.end(), 0.0f);
        head = 0;
    }

    void process(float* buffer, int numSamples) override {
        if (!enabled || activeIR.empty()) return;
        const int N = static_cast<int>(activeIR.size());
        for (int i = 0; i < numSamples; ++i) {
            history[(size_t)head] = buffer[i];
            float y = 0.0f;
            for (int k = 0; k < N; ++k)
                y += activeIR[(size_t)k] * history[(size_t)((head - k + N) % N)];
            head = (head + 1) % N;
            buffer[i] = y;
        }
    }

private:
    bool        enabled           = false;
    CabinetType irType            = CabinetType::OpenBack1x12;
    double      currentSampleRate = 0.0;

    std::vector<float> activeIR;  // IR resampled to currentSampleRate
    std::vector<float> history;
    int head = 0;

    // Resample the base 48 kHz IR for the currently selected type to
    // currentSampleRate using linear interpolation.
    void buildActiveIR() {
        const float* baseData = nullptr;
        int          baseLen  = 0;

        switch (irType) {
        case CabinetType::OpenBack1x12:
            baseData = cabinet_ir_data;
            baseLen  = cabinet_ir_len;
            break;
        case CabinetType::ClosedBack4x12:
            baseData = cabinet_ir_4x12_data;
            baseLen  = cabinet_ir_4x12_len;
            break;
        case CabinetType::Combo1x12:
            baseData = cabinet_ir_combo_data;
            baseLen  = cabinet_ir_combo_len;
            break;
        }

        constexpr double BASE_SR = 48000.0;
        if (std::abs(currentSampleRate - BASE_SR) < 1.0) {
            // Exact 48 kHz — use coefficients as-is.
            activeIR.assign(baseData, baseData + baseLen);
            return;
        }

        // New IR has the same physical duration as the base IR.
        // newLen / targetSR == baseLen / BASE_SR  →  newLen = baseLen * targetSR / BASE_SR
        const int newLen = static_cast<int>(
            std::round(baseLen * currentSampleRate / BASE_SR));
        activeIR.resize(static_cast<size_t>(std::max(1, newLen)));

        // For output sample i, the source index in the base IR is:
        //   srcIdx = i * (BASE_SR / currentSampleRate)
        const double ratio = BASE_SR / currentSampleRate;
        for (int i = 0; i < newLen; ++i) {
            const double srcIdx = i * ratio;
            const int    lo     = static_cast<int>(srcIdx);
            const double frac   = srcIdx - lo;
            const float  loVal  = (lo     < baseLen) ? baseData[lo]     : 0.0f;
            const float  hiVal  = (lo + 1 < baseLen) ? baseData[lo + 1] : 0.0f;
            activeIR[(size_t)i] = static_cast<float>(loVal * (1.0 - frac)
                                                     + hiVal * frac);
        }
    }
};
