#pragma once
#include "Effect.h"
#include "BiquadFilter.h"
#include "EnvelopeFollower.h"
#include "Oversampler.h"
#include "SmoothedValue.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

enum class DistortionType {
    HardClip,   // Chebyshev polynomial knee + hard limit at ±1 (symmetric)
    SoftClip,   // Normalised tanh: tanh(g·x)/tanh(g) — smooth musical saturation
    Foldback,   // Wave folding — harsh, ring-mod-like tones
    Asymmetric, // Silicon(+) tanh / germanium(−) atan — strong even-order harmonics
    Bitcrush,   // Hard clip then quantize — lo-fi digital artifact
};

enum class ClipShape {
    Flat,        // No pre/de-emphasis (original flat response)   (13c)
    MidFocus,    // +6 dB high shelf at 700 Hz before clip, −6 dB after
    BrightFocus, // +6 dB high shelf at 3 kHz before clip, −6 dB after
};

// Diode hard-clipping overdrive with oversampling and extended controls.
//
// Signal chain
// ------------
//   (original rate)
//   1. Envelope follower — tracks input; reduces gain up to −3 dB on loud picks (13e)
//   2. Upsample ×OVERDRIVE_OVERSAMPLING (4×)   (13a)
//   (oversampled rate)
//   3. 1st-order HP at 100 Hz — DC block
//   4. ClipShape pre-emphasis shelf              (13c)
//   5. Pre-amp gain (1×–100×) × envelope scalar
//   6. Waveshaper (DistortionType)               (13b)
//   7. ClipShape de-emphasis shelf               (13c)
//   8. Downsample ×OVERDRIVE_OVERSAMPLING (4×)   (13a)
//   (original rate)
//   9. 2nd-order Butterworth tone LP/HP blend at 3.5 kHz
//  10. Parametric peaking EQ at 800 Hz (mid)     (13d)
//  11. High-shelf presence EQ at 4 kHz           (13d)
//  12. Output level scalar
//
// Parameters
// ----------
// drive    [0, 1]    mapped to gain [1×, 100×]
// tone     [0, 1]    LP/HP blend at 3.5 kHz
// level    [0, 1]    post-clip volume
// type     DistortionType
// shape    ClipShape       pre/de-emphasis mode                (13c)
// mid      [−6, 10] dB    peaking EQ at 800 Hz, 1.5 oct BW   (13d)
// presence [0, 8] dB      high shelf at 4 kHz                 (13d)
// pickSens bool            enable envelope-based gain reduction (13e)
// bias     [−0.5, 0.5]    DC offset applied before waveshaper  (16a)
class Overdrive : public Effect {
public:
    void setDrive(float v)            { drive.setTarget(std::clamp(v, 0.0f, 1.0f)); }
    void setTone(float v)             { tone.setTarget(std::clamp(v, 0.0f, 1.0f)); }
    void setLevel(float v)            { level.setTarget(std::clamp(v, 0.0f, 1.0f)); }
    void setDistortionType(DistortionType t) { distType = t; }
    void setClipShape(ClipShape s)    { clipShape = s; }
    void setMid(float db)             { midSmoothed.setTarget(std::clamp(db, -6.0f, 10.0f)); }
    void setPresence(float db)        { presSmoothed.setTarget(std::clamp(db, 0.0f, 8.0f)); }
    void setPickSensitive(bool on)    { pickSens = on; }
    void setBias(float v)             { biasSmoothed.setTarget(std::clamp(v, -0.5f, 0.5f)); } // 16a

    void reset() override {
        osHP_x1 = osHP_y1 = 0.0f;
        preEmph700.reset(); deEmph700.reset();
        preEmph3k.reset();  deEmph3k.reset();
        toneLP.reset(); toneHP.reset();
        midFilter.reset();
        presFilter.reset();
        pickEnv.reset();
        osIn.reset();
        osOut.reset();
    }

    void prepare(double sampleRate, int blockSize) override {
        sr = sampleRate;
        const double srOs = sr * Oversampler::L;

        drive.prepare(sampleRate);
        tone.prepare(sampleRate);
        level.prepare(sampleRate);
        midSmoothed.prepare(sampleRate);
        presSmoothed.prepare(sampleRate);
        biasSmoothed.prepare(sampleRate); // 16a

        // Envelope follower: 1 ms attack / 100 ms release at original rate (13e)
        pickEnv.prepare(sampleRate, 1.0, 100.0);

        // DC-block HP at 100 Hz designed for OVERSAMPLED rate
        {
            double wa = 2.0 * srOs * std::tan(M_PI * 100.0 / srOs);
            double k  = 2.0 * srOs / (2.0 * srOs + wa);
            double p  = (2.0 * srOs - wa) / (2.0 * srOs + wa);
            osHP_b0 =  (float)k;
            osHP_b1 = -(float)k;
            osHP_a1 = -(float)p;
        }

        // Pre/de-emphasis shelves at oversampled rate (13c)
        preEmph700.setCoeffs(BiquadFilter::designHighShelf(srOs, 700.0,   6.0));
        deEmph700 .setCoeffs(BiquadFilter::designHighShelf(srOs, 700.0,  -6.0));
        preEmph3k .setCoeffs(BiquadFilter::designHighShelf(srOs, 3000.0,  6.0));
        deEmph3k  .setCoeffs(BiquadFilter::designHighShelf(srOs, 3000.0, -6.0));
        preEmph700.reset(); deEmph700.reset();
        preEmph3k.reset();  deEmph3k.reset();

        // Tone LP/HP at 3.5 kHz (original rate)
        toneLP.setCoeffs(BiquadFilter::designButterworthLP(sampleRate, 3500.0));
        toneHP.setCoeffs(BiquadFilter::designButterworthHP(sampleRate, 3500.0));
        toneLP.reset(); toneHP.reset();

        // Mid peaking EQ at 800 Hz — redesigned when mid changes during process()
        if (std::abs(mid) > 0.01f)
            midFilter.setCoeffs(BiquadFilter::designPeaking(sampleRate, 800.0, mid, 1.5));
        midFilter.reset();

        // Presence high shelf at 4 kHz
        if (presence > 0.01f)
            presFilter.setCoeffs(BiquadFilter::designHighShelf(sampleRate, 4000.0, presence));
        presFilter.reset();

        // Oversampler (both up and down)
        osIn.prepare(sampleRate, blockSize);
        osOut.prepare(sampleRate, blockSize);
        osBuf.resize(static_cast<size_t>(blockSize * Oversampler::L));
    }

    void process(float* buffer, int numSamples) override {
        // Advance mid/presence smoothers block-by-block and redesign biquads when the
        // smoothed value moves.  State is intentionally NOT reset — the biquad settles
        // naturally on gradual coefficient changes, eliminating the one-sample glitch
        // that the previous "redesign + resetMidState()" pattern produced.
        {
            float v = mid;
            for (int i = 0; i < numSamples; ++i) v = midSmoothed.next();
            if (std::bit_cast<uint32_t>(v) != std::bit_cast<uint32_t>(mid)) {
                mid = v;
                if (std::abs(mid) > 0.01f)
                    midFilter.setCoeffs(BiquadFilter::designPeaking(sr, 800.0, mid, 1.5));
            }
        }
        {
            float v = presence;
            for (int i = 0; i < numSamples; ++i) v = presSmoothed.next();
            if (std::bit_cast<uint32_t>(v) != std::bit_cast<uint32_t>(presence)) {
                presence = v;
                if (presence > 0.01f)
                    presFilter.setCoeffs(BiquadFilter::designHighShelf(sr, 4000.0, presence));
            }
        }

        // Ensure internal buffers are large enough for this block
        const auto ns    = static_cast<size_t>(numSamples);
        const auto osLen = static_cast<size_t>(numSamples * Oversampler::L);
        if (osBuf.size()      < osLen) osBuf.resize(osLen);
        if (driveBuf.size()   < ns)    driveBuf.resize(ns);
        if (gainEnvBuf.size() < ns)    gainEnvBuf.resize(ns);
        if (toneBuf.size()    < ns)    toneBuf.resize(ns);
        if (levelBuf.size()   < ns)    levelBuf.resize(ns);
        if (scaleBuf.size()   < ns)    scaleBuf.resize(ns);
        if (biasBuf.size()    < ns)    biasBuf.resize(ns); // 16a

        for (size_t i = 0; i < ns; ++i) {
            // Advance parameter smoothers at original rate
            const float driveVal = drive.next();
            const float g        = 1.0f + driveVal * 99.0f;
            const float t        = tone.next();
            const float lv       = level.next();

            // 13e: Envelope follower (peak detector, original rate)
            const float envLevel = pickEnv.process(buffer[i]);
            // Gain reduction: 0 dB at envLevel=0, up to −3 dB at envLevel=1
            const float gainEnv = pickSens ? (1.0f - 0.292f * std::min(envLevel, 1.0f))
                                           : 1.0f;

            // Store per-sample values for use in oversampled and post loops
            driveBuf[i]   = g;
            gainEnvBuf[i] = gainEnv;
            toneBuf[i]    = t;
            levelBuf[i]   = lv;
            scaleBuf[i]   = fastTanh(g); // tanh(g) precomputed once per original-rate sample
            biasBuf[i]    = biasSmoothed.next(); // 16a
        }

        // 13a: Upsample block
        osIn.upsample(buffer, osBuf.data(), numSamples);

        for (size_t i = 0; i < osLen; ++i) {
            // Use drive/env from the corresponding original-rate sample
            const size_t orig = i / static_cast<size_t>(Oversampler::L);
            const float g     = driveBuf[orig];
            const float gEnv  = gainEnvBuf[orig];
            const float scale = scaleBuf[orig];

            float x = osBuf[i];

            // DC block (oversampled rate)
            float y = osHP_b0 * x + osHP_b1 * osHP_x1 - osHP_a1 * osHP_y1;
            osHP_x1 = x; osHP_y1 = y; x = y;

            // 13c: pre-emphasis
            switch (clipShape) {
                case ClipShape::MidFocus:    x = preEmph700.process(x); break;
                case ClipShape::BrightFocus: x = preEmph3k.process(x);  break;
                case ClipShape::Flat: break;
            }

            // Gain × envelope
            x *= g * gEnv;

            // 16a: bias — DC offset applied immediately before the waveshaper
            x += biasBuf[orig];

            // 13b: waveshaper
            switch (distType) {
                case DistortionType::HardClip: {
                    // Chebyshev polynomial knee + hard limit
                    float cx = std::clamp(x, -1.0f, 1.0f);
                    x = (3.0f * cx - cx * cx * cx) * 0.5f;
                    break;
                }
                case DistortionType::SoftClip: {
                    // Normalised tanh: tanh(x)/tanh(g); scale precomputed at original rate
                    x = (scale > 1e-6f) ? fastTanh(x) / scale : x;
                    break;
                }
                case DistortionType::Foldback:
                    x = wavefold(x, 1.0f);
                    break;
                case DistortionType::Asymmetric: {
                    // scale precomputed at original rate; atan uses float overload
                    if (x >= 0.0f)
                        x = (scale > 1e-6f) ? fastTanh(x) / scale : x;
                    else
                        x = std::atan(x) * (float)(2.0 / M_PI);
                    break;
                }
                case DistortionType::Bitcrush:
                    x = bitcrush(x, g);
                    break;
            }

            // 13c: de-emphasis
            switch (clipShape) {
                case ClipShape::MidFocus:    x = deEmph700.process(x); break;
                case ClipShape::BrightFocus: x = deEmph3k.process(x);  break;
                case ClipShape::Flat: break;
            }

            osBuf[i] = x;
        }

        // 13a: Downsample back to original rate
        osOut.downsample(osBuf.data(), buffer, numSamples);

        // Tone blend + mid/presence EQ + level (all at original rate)
        for (size_t i = 0; i < ns; ++i) {
            const float t  = toneBuf[i];
            const float lv = levelBuf[i];

            float lp = toneLP.process(buffer[i]);
            float hp = toneHP.process(buffer[i]);
            float s = (1.0f - t) * lp + t * hp;

            // 13d: mid peaking EQ
            if (std::abs(mid) > 0.01f)
                s = midFilter.process(s);

            // 13d: presence high shelf
            if (presence > 0.01f)
                s = presFilter.process(s);

            buffer[i] = s * lv;
        }
    }

private:
    double sr = 48000.0;
    DistortionType distType  = DistortionType::HardClip;
    ClipShape      clipShape = ClipShape::Flat;
    float mid      = 0.0f;   // current smoothed value, updated each process() block
    float presence = 0.0f;   // current smoothed value, updated each process() block
    bool  pickSens = true;

    // Per-sample scratch buffers — resized dynamically in prepare() and process().
    std::vector<float> driveBuf, gainEnvBuf, toneBuf, levelBuf, scaleBuf, biasBuf; // 16a: biasBuf added

    // Oversamplers
    Oversampler osIn, osOut;
    std::vector<float> osBuf;

    // DC-block HP at oversampled rate (1st-order — not a biquad)
    float osHP_b0 = 0, osHP_b1 = 0, osHP_a1 = 0;
    float osHP_x1 = 0, osHP_y1 = 0;

    // ClipShape shelves (pre/de-emphasis) at oversampled rate (13c)
    BiquadFilter preEmph700, deEmph700, preEmph3k, deEmph3k;

    // Tone LP/HP biquads at original rate
    BiquadFilter toneLP, toneHP;

    // Mid peaking EQ (13d)
    BiquadFilter midFilter;

    // Presence high shelf (13d)
    BiquadFilter presFilter;

    // Pick-sensitivity envelope follower (13e)
    EnvelopeFollower pickEnv;

    SmoothedValue drive, tone, level;
    SmoothedValue midSmoothed, presSmoothed;
    SmoothedValue biasSmoothed; // 16a

    // -----------------------------------------------------------------------
    // Sample-level processing helpers
    // -----------------------------------------------------------------------

    // [7/6] rational Padé approximant for tanh(x).
    // Max error < 5e-4 for |x| ≤ 5 (clamped beyond that).  ~3× faster than
    // std::tanh((double)x) because it avoids the double-precision call.
    static float fastTanh(float x) {
        if (x >  5.0f) return  1.0f;
        if (x < -5.0f) return -1.0f;
        const float x2 = x * x;
        return x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)))
                 / (135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f)));
    }

    // Triangle-wave fold
    static float wavefold(float x, float T) {
        float xn = x + T;
        float period = 4.0f * T;
        float m = std::fmod(xn, period);
        if (m < 0.0f) m += period;
        return T - std::abs(m - 2.0f * T);
    }

    // Hard-clip to [-1,1] then quantize; bits = 16 − round(drive*14 / 99)
    // drive here is the linear gain (1–100), map back to 0–1 for bit count.
    static float bitcrush(float x, float g) {
        x = std::clamp(x, -1.0f, 1.0f);
        float driveNorm = (g - 1.0f) / 99.0f;
        int bits = std::max(2, (int)std::round(16.0f - driveNorm * 14.0f));
        float step = 2.0f / (float)(1 << bits);
        return std::clamp(std::round(x / step) * step, -1.0f, 1.0f);
    }
};
