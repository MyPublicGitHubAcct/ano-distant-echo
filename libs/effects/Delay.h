#pragma once
#include "Effect.h"
#include "AllpassFilter.h"
#include "DelayLine.h"
#include "EnvelopeFollower.h"
#include "OnePoleFilter.h"
#include "SmoothedValue.h"
#include <algorithm>
#include <cmath>

// Single-tap feedback delay (tape/BBD emulation).
//
// Algorithm
// ---------
// A circular buffer implements a single delay line. Each output sample is
// a mix of the dry input and the delayed ("wet") signal:
//
//   out[n] = (1 − mix) · in[n]  +  mix · wet[n]
//
// Linear interpolation produces sub-sample accuracy for fractional delay times:
//
//   wet = (1 − frac) · buf[n0]  +  frac · buf[n1]
//
// where n0 is floor(delaySamples) samples old and n1 is floor+1 samples old.
// The wet signal is fed back through a 1st-order LP at 4 kHz:
//
//   buf[n] = in[n]  +  feedback · LP(wet[n])
//
// The LP in the feedback path mimics the high-frequency roll-off of tape and
// bucket-brigade (BBD) analog delays, making each repeat progressively darker.
//
// When tape saturation is enabled, the LP output is passed through a tanh
// softclipper before being written back into the buffer:
//
//   fbSig = tanh(satDrive · lpState) / tanh(satDrive)
//   buf[n] = in[n] + feedback · fbSig
//
// with satDrive = 2.0 + tapeAge·3.0  and  LP fc = 4000 − tapeAge·2500 Hz.
// This limits runaway build-up at high feedback and adds warmth.
//
// Time changes are smoothed via a 10 ms crossfade: the old read pointer fades
// out while the new one fades in, eliminating pitch glitches on abrupt edits.
// State: activeTimeSamples (current source) + pendingTimeSamples (target) +
// crossfadeCounter (samples remaining).
//
// Wow/flutter (tape speed modulation) offsets the fractional read pointer
// each sample via LFO:
//   wow:     single sine at wowRate Hz, ±wowDepthSamples peak deviation
//   flutter: sum of two sines at flutterRate and flutterRate×1.07 Hz,
//            scaled so peak = ±flutterDepthSamples (band-limited noise LFO)
//
// Parameters
// ----------
// time_ms         [1, 2000]   delay time in milliseconds
// feedback        [0, 0.95]   fraction of delayed signal fed back; upper limit raised when
//                             selfOscillate=true (see below)
// mix             [0, 1]      wet/dry ratio (0 = fully dry, 1 = fully wet)
// wowRate         [0, 2]      wow LFO rate in Hz (0 = disabled)
// wowDepthMs      [0, 10]     wow peak delay deviation in ms
// flutterRate     [3, 12]     flutter LFO rate in Hz (default 8 Hz)
// flutterDepthMs  [0, 2]      flutter peak delay deviation in ms (0 = disabled)
// tapeSat         false       enable tape saturation stage in feedback path
// tapeAge         [0, 1]      ages the tape: darkens LP cutoff and raises saturation
// duckThreshold   [-30, 0]    ducking threshold in dBFS; envelope follower on dry input
// duckDepth       [0, 1]      0 = no duck; 1 = wet fully muted while input exceeds threshold
// diffusion       [0, 1]      pre-delay allpass diffusion (0 = off, 1 = full chain)
// selfOscillate   false       unlocks feedback above 0.95: up to 1.02 with tape saturation
//                             (tanh softclipper prevents runaway), or 0.98 without it
//
// Ducking: a 5 ms attack / 500 ms release EnvelopeFollower tracks abs(dry input). When
// the envelope exceeds the linear threshold, the wet/mix gain is scaled by (1 − duckDepth).
// The feedback path is unaffected — the delay buffer continues to accumulate. When the
// dry signal drops below threshold the envelope releases and the wet signal re-emerges.
//
// Diffusion: four Schroeder AllpassFilter stages (delays 11, 17, 23, 31 ms; coefficient 0.5)
// in series before the main delay write. Each allpass has the transfer function
//   H(z) = (z^-D - g) / (1 - g·z^-D)   (all-pass, |H|=1 at all frequencies)
// The chain smears input transients while preserving the magnitude spectrum, giving
// repeats a "bloom" character. At diffusion=0 the chain is bypassed entirely.

class Delay : public Effect {
public:
    void setTimeSamples(float samples) {
        float clamped = std::max(1.0f, samples);
        if (crossfadeSamples == 0) {
            // prepare() not yet called — store directly, no crossfade possible
            activeTimeSamples = clamped;
            return;
        }
        if (crossfadeCounter > 0) {
            // Mid-crossfade: retarget; cancel if landing back at activeTimeSamples
            pendingTimeSamples = clamped;
            if (std::abs(pendingTimeSamples - activeTimeSamples) < 0.001f)
                crossfadeCounter = 0;
        } else if (std::abs(clamped - activeTimeSamples) > 0.001f) {
            pendingTimeSamples = clamped;
            crossfadeCounter   = crossfadeSamples;
        }
    }
    void setTimeMs(double ms, double sampleRate) {
        setTimeSamples((float)(sampleRate * ms / 1000.0));
    }
    void setFeedback(float v)      { feedback.setTarget(std::clamp(v, 0.0f, maxFeedback())); }
    void setMix(float v)           { mix.setTarget(std::clamp(v, 0.0f, 1.0f)); }
    void setSelfOscillate(bool v)  { selfOscillate = v; }

    // Stereo modes: ping-pong (cross-channel feedback) or independent (right time × 1.02).
    void setPingPong(bool v) { pingPong = v; }

    void setDiffusion(float v) { diffusion = std::clamp(v, 0.0f, 1.0f); }

    void setDuckThreshold(float dBFS) {
        duckThreshold = std::clamp(dBFS, -30.0f, 0.0f);
        duckThreshLin = std::pow(10.0f, duckThreshold / 20.0f);
    }
    void setDuckDepth(float depth) {
        duckDepth = std::clamp(depth, 0.0f, 1.0f);
    }

    void setTapeSat(bool enabled) {
        tapeSat = enabled;
        recomputeSatCoeffs();
    }
    void setTapeAge(float age) {
        tapeAge = std::clamp(age, 0.0f, 1.0f);
        recomputeSatCoeffs();
        recomputeLpAlpha();
    }

    void setWowRate(float hz) {
        wowRate = std::clamp(hz, 0.0f, 2.0f);
        recomputeLfoCoeffs();
    }
    void setWowDepthMs(float ms) {
        wowDepthMs = std::clamp(ms, 0.0f, 10.0f);
        if (sr > 0.0) wowDepthSamples = wowDepthMs * (float)sr / 1000.0f;
    }
    void setFlutterRate(float hz) {
        flutterRate = std::clamp(hz, 3.0f, 12.0f);
        recomputeLfoCoeffs();
    }
    void setFlutterDepthMs(float ms) {
        flutterDepthMs = std::clamp(ms, 0.0f, 2.0f);
        if (sr > 0.0) flutterDepthSamples = flutterDepthMs * (float)sr / 1000.0f;
    }

    void reset() override {
        dl.reset();
        dlR.reset();
        feedbackLP.reset();
        feedbackLPR.reset();
        duckFollower.reset();
        duckFollowerR.reset();
        wowPhase = flutterPhase1 = flutterPhase2 = 0.0f;
        for (auto& ap : diffAp)  ap.reset();
        for (auto& ap : diffApR) ap.reset();
    }

    void prepare(double sampleRate, int /*blockSize*/) override {
        sr = sampleRate;
        feedback.prepare(sampleRate);
        mix.prepare(sampleRate);

        // Allocate for maximum possible delay (2000 ms)
        int maxSamples = (int)(sampleRate * 2.0) + 2;
        dl.resize(maxSamples);

        recomputeLpAlpha();
        recomputeSatCoeffs();

        crossfadeSamples = (int)(sampleRate * 0.010);  // 10 ms
        crossfadeCounter = 0;

        if (activeTimeSamples <= 0.0f)
            activeTimeSamples = (float)(sampleRate * 0.3);  // default 300 ms

        // Duck envelope follower: 5 ms attack / 500 ms release
        duckFollower.prepare(sampleRate, 5.0, 500.0);
        duckFollowerR.prepare(sampleRate, 5.0, 500.0);

        recomputeLfoCoeffs();

        // Allpass stages for pre-delay diffusion (11, 17, 23, 31 ms; coeff 0.5)
        static constexpr float AP_DELAYS_MS[4] = {11.0f, 17.0f, 23.0f, 31.0f};
        for (int k = 0; k < 4; ++k) {
            int D = std::max(1, (int)std::round(AP_DELAYS_MS[k] * sampleRate / 1000.0));
            diffAp[k].prepare(D, 0.5f);
            diffApR[k].prepare(D, 0.5f);
        }

        // Right-channel delay line (stereo support)
        dlR.resize(maxSamples);
        feedbackLPR.reset();
        duckFollowerR.reset();
    }

    void process(float* buffer, int numSamples) override {
        // Lagrange needs read(di+3) to be valid; cap accordingly.
        // Linear needs read(di+1); cap at size-2.
        const float maxRead = tapeSat ? (float)(dl.size() - 5)
                                      : (float)(dl.size() - 2);
        for (int i = 0; i < numSamples; ++i) {
            float fb = feedback.next();
            float mx = mix.next();
            float x  = buffer[i];

            // Wow/flutter LFO: offset the read position each sample
            float modOffset = 0.0f;
            if (wowDepthSamples > 0.0f) {
                modOffset += wowDepthSamples * std::sin(wowPhase);
                wowPhase += wowPhaseInc;
                if (wowPhase >= 6.283185f) wowPhase -= 6.283185f;
            }
            if (flutterDepthSamples > 0.0f) {
                modOffset += flutterDepthSamples * 0.5f
                           * (std::sin(flutterPhase1) + std::sin(flutterPhase2));
                flutterPhase1 += flutterPhaseInc1;
                if (flutterPhase1 >= 6.283185f) flutterPhase1 -= 6.283185f;
                flutterPhase2 += flutterPhaseInc2;
                if (flutterPhase2 >= 6.283185f) flutterPhase2 -= 6.283185f;
            }

            float wet;
            if (crossfadeCounter > 0) {
                float fadeOut = (float)crossfadeCounter / (float)crossfadeSamples;
                float fadeIn  = 1.0f - fadeOut;
                float dA = std::max(1.0f, std::min(activeTimeSamples  + modOffset, maxRead));
                float dP = std::max(1.0f, std::min(pendingTimeSamples + modOffset, maxRead));
                if (tapeSat) {
                    wet = fadeOut * dl.readLagrange(dA) + fadeIn * dl.readLagrange(dP);
                } else {
                    wet = fadeOut * dl.readLerp(dA) + fadeIn * dl.readLerp(dP);
                }
                if (--crossfadeCounter == 0)
                    activeTimeSamples = pendingTimeSamples;
            } else {
                float d = std::max(1.0f, std::min(activeTimeSamples + modOffset, maxRead));
                wet = tapeSat ? dl.readLagrange(d) : dl.readLerp(d);
            }

            float lpOut = feedbackLP.process(wet);
            float fbSig = tapeSat
                ? std::tanh(satDrive * lpOut) * satScale
                : lpOut;

            // Pre-diffusion: run dry input through allpass chain before writing
            float pre = x;
            if (diffusion > 0.001f) {
                float ap = x;
                for (int k = 0; k < 4; ++k)
                    ap = diffAp[k].process(ap);
                pre = (1.0f - diffusion) * x + diffusion * ap;
            }
            dl.push(pre + fb * fbSig);

            // Duck: envelope follower on dry input attenuates wet when above threshold
            float duckGain = 1.0f;
            if (duckDepth > 0.0f) {
                float duckLevel = duckFollower.process(x);
                if (duckLevel > duckThreshLin)
                    duckGain = 1.0f - duckDepth;
            }
            buffer[i] = (1.0f - mx) * x + mx * duckGain * wet;
        }
    }

    // Stereo processing. Two modes controlled by setPingPong():
    //
    // Independent (pingPong=false): each channel is its own delay line; right
    // channel time is left × 1.02 for a lush stereo width effect. Cross-channel
    // feedback paths are independent.
    //
    // Ping-pong (pingPong=true): left and right delay lines exchange feedback —
    // left delay is fed by the right's LP output and vice versa. Dry input on
    // each channel enters its own delay. First repeats emerge on the same channel
    // as the input, then alternate between L and R on successive passes.
    void processStereo(float* left, float* right, int numSamples) override {
        const float maxRead = tapeSat ? (float)(dl.size() - 5)
                                      : (float)(dl.size() - 2);
        // In independent mode, right delay time is slightly longer for stereo width.
        const float stereoRatio = pingPong ? 1.0f : 1.02f;

        for (int i = 0; i < numSamples; ++i) {
            float fb = feedback.next();
            float mx = mix.next();
            float xL = left[i];
            float xR = right[i];

            // Wow/flutter LFO offset — shared between both channels
            float modOffset = 0.0f;
            if (wowDepthSamples > 0.0f) {
                modOffset += wowDepthSamples * std::sin(wowPhase);
                wowPhase += wowPhaseInc;
                if (wowPhase >= 6.283185f) wowPhase -= 6.283185f;
            }
            if (flutterDepthSamples > 0.0f) {
                modOffset += flutterDepthSamples * 0.5f
                           * (std::sin(flutterPhase1) + std::sin(flutterPhase2));
                flutterPhase1 += flutterPhaseInc1;
                if (flutterPhase1 >= 6.283185f) flutterPhase1 -= 6.283185f;
                flutterPhase2 += flutterPhaseInc2;
                if (flutterPhase2 >= 6.283185f) flutterPhase2 -= 6.283185f;
            }

            // Read wet signal from both delay lines, with crossfade if active
            float wetL, wetR;
            if (crossfadeCounter > 0) {
                float fadeOut = (float)crossfadeCounter / (float)crossfadeSamples;
                float fadeIn  = 1.0f - fadeOut;
                float dAL = std::max(1.0f, std::min(activeTimeSamples + modOffset, maxRead));
                float dPL = std::max(1.0f, std::min(pendingTimeSamples + modOffset, maxRead));
                float dAR = std::max(1.0f, std::min(activeTimeSamples  * stereoRatio + modOffset, maxRead));
                float dPR = std::max(1.0f, std::min(pendingTimeSamples * stereoRatio + modOffset, maxRead));
                if (tapeSat) {
                    wetL = fadeOut * dl.readLagrange(dAL)  + fadeIn * dl.readLagrange(dPL);
                    wetR = fadeOut * dlR.readLagrange(dAR) + fadeIn * dlR.readLagrange(dPR);
                } else {
                    wetL = fadeOut * dl.readLerp(dAL)  + fadeIn * dl.readLerp(dPL);
                    wetR = fadeOut * dlR.readLerp(dAR) + fadeIn * dlR.readLerp(dPR);
                }
                if (--crossfadeCounter == 0)
                    activeTimeSamples = pendingTimeSamples;
            } else {
                float dL = std::max(1.0f, std::min(activeTimeSamples + modOffset, maxRead));
                float dR = std::max(1.0f, std::min(activeTimeSamples * stereoRatio + modOffset, maxRead));
                wetL = tapeSat ? dl.readLagrange(dL)  : dl.readLerp(dL);
                wetR = tapeSat ? dlR.readLagrange(dR) : dlR.readLerp(dR);
            }

            // LP filters in feedback path — one per channel
            float lpOutL = feedbackLP.process(wetL);
            float lpOutR = feedbackLPR.process(wetR);
            float fbSigL = tapeSat ? std::tanh(satDrive * lpOutL) * satScale : lpOutL;
            float fbSigR = tapeSat ? std::tanh(satDrive * lpOutR) * satScale : lpOutR;

            // Pre-diffusion allpass chain — independent per channel
            float preL = xL, preR = xR;
            if (diffusion > 0.001f) {
                float apL = xL, apR = xR;
                for (int k = 0; k < 4; ++k) {
                    apL = diffAp[k].process(apL);
                    apR = diffApR[k].process(apR);
                }
                preL = (1.0f - diffusion) * xL + diffusion * apL;
                preR = (1.0f - diffusion) * xR + diffusion * apR;
            }

            // Write to delay lines: cross-feed for ping-pong, independent otherwise.
            // Ping-pong: L delay fed by R's feedback, R delay fed by L's feedback.
            if (pingPong) {
                dl.push(preL  + fb * fbSigR);
                dlR.push(preR + fb * fbSigL);
            } else {
                dl.push(preL  + fb * fbSigL);
                dlR.push(preR + fb * fbSigR);
            }

            // Duck: per-channel envelope followers attenuate wet when input is loud
            float duckGainL = 1.0f, duckGainR = 1.0f;
            if (duckDepth > 0.0f) {
                if (duckFollower.process(xL) > duckThreshLin)
                    duckGainL = 1.0f - duckDepth;
                if (duckFollowerR.process(xR) > duckThreshLin)
                    duckGainR = 1.0f - duckDepth;
            }

            left[i]  = (1.0f - mx) * xL + mx * duckGainL * wetL;
            right[i] = (1.0f - mx) * xR + mx * duckGainR * wetR;
        }
    }

    void recomputeLfoCoeffs() {
        float fsr = (float)sr;
        wowPhaseInc      = 6.283185f * wowRate        / fsr;
        flutterPhaseInc1 = 6.283185f * flutterRate    / fsr;
        flutterPhaseInc2 = 6.283185f * flutterRate * 1.07f / fsr;
        wowDepthSamples     = wowDepthMs     * fsr / 1000.0f;
        flutterDepthSamples = flutterDepthMs * fsr / 1000.0f;
    }

    void recomputeLpAlpha() {
        // fc = 4000 Hz at age=0, 1500 Hz at age=1
        float fc    = 4000.0f - tapeAge * 2500.0f;
        float alpha = (float)(1.0 - std::exp(-2.0 * M_PI * fc / sr));
        feedbackLP.setAlpha(alpha);
        feedbackLPR.setAlpha(alpha);
    }

    void recomputeSatCoeffs() {
        satDrive = 2.0f + tapeAge * 3.0f;  // 2.0 @ age=0, 5.0 @ age=1
        satScale = tapeSat ? (1.0f / std::tanh(satDrive)) : 1.0f;
    }

    double sr = 48000.0;
    float activeTimeSamples  = 0.0f;
    float pendingTimeSamples = 0.0f;
    int   crossfadeCounter   = 0;
    int   crossfadeSamples   = 0;
    DelayLine<float> dl;
    SmoothedValue feedback, mix;

    // Feedback LP filter — OnePoleLP per channel
    OnePoleLP feedbackLP, feedbackLPR;

    // Ducking — EnvelopeFollower on dry input gates the wet gain
    float duckThreshold    = 0.0f;   // dBFS (0 = full scale, −30 = most sensitive)
    float duckDepth        = 0.0f;   // 0 = no duck; 1 = fully mute wet when ducked
    float duckThreshLin    = 1.0f;   // linear equivalent of duckThreshold
    EnvelopeFollower duckFollower, duckFollowerR;

    // Tape saturation
    bool  tapeSat  = false;
    float tapeAge  = 0.0f;
    float satDrive = 2.0f;
    float satScale = 1.0f;

    // Wow LFO — sine at wowRate Hz
    float wowRate         = 0.0f;
    float wowDepthMs      = 0.0f;
    float wowDepthSamples = 0.0f;
    float wowPhase        = 0.0f;
    float wowPhaseInc     = 0.0f;

    // Flutter LFO — sum of two sines at flutterRate and flutterRate×1.07
    float flutterRate         = 8.0f;
    float flutterDepthMs      = 0.0f;
    float flutterDepthSamples = 0.0f;
    float flutterPhase1       = 0.0f;
    float flutterPhase2       = 0.0f;
    float flutterPhaseInc1    = 0.0f;
    float flutterPhaseInc2    = 0.0f;

    // Pre-delay diffusion — four AllpassFilter stages (11, 17, 23, 31 ms)
    AllpassFilter diffAp[4];
    float         diffusion = 0.0f;

    // Self-oscillation — unlocks feedback above the normal 0.95 ceiling
    bool selfOscillate = false;

    // Stereo right-channel state — used by processStereo() only
    DelayLine<float> dlR;
    AllpassFilter    diffApR[4];
    bool             pingPong = false;

private:
    float maxFeedback() const {
        if (!selfOscillate) return 0.95f;
        return tapeSat ? 1.02f : 0.98f;
    }
};
