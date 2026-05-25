#include "plugin.hpp"
#include "Delay.h"

struct DelayModule : Module {
    enum ParamIds {
        TIME_PARAM,
        FEEDBACK_PARAM,
        MIX_PARAM,
        DIFFUSION_PARAM,      // 14f
        MOD_DEPTH_PARAM,      // 14c: wow+flutter depth at fixed rates
        TAPE_SAT_PARAM,       // 14d: tape saturation on/off
        TAPE_AGE_PARAM,       // 14d: tape age (darkens and saturates)
        DUCK_THRESHOLD_PARAM, // 14e: ducking threshold in dBFS
        DUCK_DEPTH_PARAM,     // 14e: ducking depth
        SELF_OSC_PARAM,       // 14g: self-oscillation (unlocks feedback > 0.95)
        NUM_PARAMS
    };
    enum InputIds {
        AUDIO_INPUT,
        TIME_CV_INPUT,
        FEEDBACK_CV_INPUT,
        MIX_CV_INPUT,
        TAP_INPUT,        // 20e: tap tempo
        CLK_INPUT,        // 20f: clock/BPM gate — rising-edge interval → delay time
        PITCH_INPUT,      // 20f: V/oct pitch → delay time (1000 / (440 * 2^v) ms)
        BYPASS_INPUT,     // 20h: gate > 1 V → audio passthrough + effect reset
        R_AUDIO_INPUT,    // 15: right channel — when connected, enables stereo processing
        NUM_INPUTS
    };
    enum OutputIds {
        AUDIO_OUTPUT,
        R_AUDIO_OUTPUT,   // 15: right channel output
        NUM_OUTPUTS
    };
    enum LightIds {
        SIGNAL_LIGHT,  // green — audio present (> 0.05 V)
        CLIP_LIGHT,    // red   — output clipping (> 4.75 V)
        NUM_LIGHTS
    };

    Delay effect;
    bool bypassHigh = false; // 20h: previous gate state for rising-edge detection

    // Tap tempo state (20e)
    float tapTimeMs      = 0.f;
    bool  tapHigh        = false;
    bool  tapPrimed      = false;
    int   tapSampleCount = 0;

    // Clock input state (20f) — same rising-edge mechanism as tap tempo
    float clkTimeMs      = 0.f;
    bool  clkHigh        = false;
    bool  clkPrimed      = false;
    int   clkSampleCount = 0;

    DelayModule() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(TIME_PARAM,         1.f, 2000.f, 300.f, "Time",      " ms");
        configParam(FEEDBACK_PARAM,     0.f,  1.02f,  0.4f, "Feedback");
        configParam(MIX_PARAM,          0.f,   1.0f,  0.5f, "Mix");
        configParam(DIFFUSION_PARAM,    0.f,   1.0f,  0.0f, "Diffusion");          // 14f
        configParam(MOD_DEPTH_PARAM,   0.f,   1.0f,  0.0f, "Mod Depth", "%",
            0.f, 100.f);                                                           // 14c
        configSwitch(TAPE_SAT_PARAM,    0.f,   1.f,   0.f,  "Tape Saturation",
            {"Off", "On"});                                                         // 14d
        configParam(TAPE_AGE_PARAM,     0.f,   1.0f,  0.0f, "Tape Age", "%",
            0.f, 100.f);                                                            // 14d
        configParam(DUCK_THRESHOLD_PARAM, -30.f, 0.f, 0.f,  "Duck Threshold",
            " dB");                                                                 // 14e
        configParam(DUCK_DEPTH_PARAM,   0.f,   1.0f,  0.0f, "Duck Depth", "%",
            0.f, 100.f);                                                            // 14e
        configSwitch(SELF_OSC_PARAM,    0.f,   1.f,   0.f,  "Self Oscillate",
            {"Off", "On"});                                                         // 14g
        configInput(AUDIO_INPUT,        "Audio");
        configInput(TIME_CV_INPUT,      "Time CV");
        configInput(FEEDBACK_CV_INPUT,  "Feedback CV");
        configInput(MIX_CV_INPUT,       "Mix CV");
        configInput(TAP_INPUT,          "Tap Tempo");
        configInput(CLK_INPUT,          "Clock");
        configInput(PITCH_INPUT,        "V/Oct Pitch");
        configInput(BYPASS_INPUT,       "Bypass");          // 20h
        configInput(R_AUDIO_INPUT,      "Right Audio");     // 15
        configOutput(AUDIO_OUTPUT,      "Left Audio");
        configOutput(R_AUDIO_OUTPUT,    "Right Audio");     // 15
        configBypass(AUDIO_INPUT,   AUDIO_OUTPUT);          // 20k: pass L through on bypass
        configBypass(R_AUDIO_INPUT, R_AUDIO_OUTPUT);        // 15: pass R through on bypass
    }

    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        effect.prepare((double)e.sampleRate, 1);
    }

    void onReset(const ResetEvent& /*e*/) override {
        bypassHigh     = false;
        tapPrimed      = false;
        tapHigh        = false;
        tapTimeMs      = 0.f;
        tapSampleCount = 0;
        clkPrimed      = false;
        clkHigh        = false;
        clkTimeMs      = 0.f;
        clkSampleCount = 0;
        effect.prepare(APP->engine->getSampleRate(), 1);
    }

    void process(const ProcessArgs& args) override {
        if (!outputs[AUDIO_OUTPUT].isConnected()) {
            lights[SIGNAL_LIGHT].setBrightness(0.f);
            lights[CLIP_LIGHT].setBrightness(0.f);
            return;
        }

        // Bypass gate (20h): gate > 1 V passes audio through and resets effect state
        if (inputs[BYPASS_INPUT].isConnected()) {
            bool nowHigh = inputs[BYPASS_INPUT].getVoltage() > 1.0f;
            if (nowHigh) {
                if (!bypassHigh) effect.reset();
                bypassHigh = true;
                outputs[AUDIO_OUTPUT].setVoltage(inputs[AUDIO_INPUT].getVoltage());
                outputs[R_AUDIO_OUTPUT].setVoltage(inputs[R_AUDIO_INPUT].getVoltage());
                lights[SIGNAL_LIGHT].setBrightness(0.f);
                lights[CLIP_LIGHT].setBrightness(0.f);
                return;
            }
            bypassHigh = nowHigh;
        }

        // Tap tempo (20e)
        if (!inputs[TAP_INPUT].isConnected()) {
            tapPrimed = false; tapHigh = false;
        } else {
            bool tapNowHigh = inputs[TAP_INPUT].getVoltage() >= 1.0f;
            if (tapNowHigh && !tapHigh) {
                if (tapPrimed) {
                    float iv = args.sampleTime * tapSampleCount * 1000.f;
                    if (iv >= 10.f && iv <= 4000.f) tapTimeMs = iv;
                }
                tapPrimed = true; tapSampleCount = 0;
            }
            tapHigh = tapNowHigh;
            ++tapSampleCount;
        }

        // Clock input (20f) — same mechanism as tap tempo
        if (!inputs[CLK_INPUT].isConnected()) {
            clkPrimed = false; clkHigh = false;
        } else {
            bool clkNowHigh = inputs[CLK_INPUT].getVoltage() >= 1.0f;
            if (clkNowHigh && !clkHigh) {
                if (clkPrimed) {
                    float iv = args.sampleTime * clkSampleCount * 1000.f;
                    if (iv >= 10.f && iv <= 4000.f) clkTimeMs = iv;
                }
                clkPrimed = true; clkSampleCount = 0;
            }
            clkHigh = clkNowHigh;
            ++clkSampleCount;
        }

        // Time priority: V/OCT > CLK > TAP > knob.
        // TIME CV applies as an offset except when V/OCT is connected (would detune it).
        float baseTime;
        float timeOffset = inputs[TIME_CV_INPUT].getVoltage() * 200.f;
        if (inputs[PITCH_INPUT].isConnected()) {
            float freq = 440.f * std::exp2(inputs[PITCH_INPUT].getVoltage());
            baseTime   = 1000.f / freq;
            timeOffset = 0.f;  // V/OCT defines pitch exactly; CV offset would detune
        } else if (inputs[CLK_INPUT].isConnected() && clkTimeMs > 0.f) {
            baseTime = clkTimeMs;
        } else if (inputs[TAP_INPUT].isConnected() && tapTimeMs > 0.f) {
            baseTime = tapTimeMs;
        } else {
            baseTime = params[TIME_PARAM].getValue();
        }
        float timeMs = clamp(baseTime + timeOffset, 1.f, 2000.f);

        bool  selfOsc       = params[SELF_OSC_PARAM].getValue() > 0.5f;             // 14g
        bool  tapeSat       = params[TAPE_SAT_PARAM].getValue() > 0.5f;             // 14d

        // Feedback/Mix CV: ±5 V → ±0.51 offset on 0–1.02 feedback range
        float fbMax    = selfOsc ? (tapeSat ? 1.02f : 0.98f) : 0.95f;
        float feedback = clamp(params[FEEDBACK_PARAM].getValue()
                             + inputs[FEEDBACK_CV_INPUT].getVoltage() * 0.102f,
                             0.f, fbMax);
        float mix      = clamp(params[MIX_PARAM].getValue()
                             + inputs[MIX_CV_INPUT].getVoltage() * 0.1f,
                             0.f, 1.f);

        float diffusion     = clamp(params[DIFFUSION_PARAM].getValue(),    0.f, 1.f);
        float modDepth      = clamp(params[MOD_DEPTH_PARAM].getValue(),  0.f, 1.f); // 14c
        float tapeAge       = clamp(params[TAPE_AGE_PARAM].getValue(),     0.f, 1.f); // 14d
        float duckThresh    = clamp(params[DUCK_THRESHOLD_PARAM].getValue(), -30.f, 0.f); // 14e
        float duckDepth     = clamp(params[DUCK_DEPTH_PARAM].getValue(),   0.f, 1.f); // 14e

        effect.setSelfOscillate(selfOsc);              // 14g
        effect.setTimeMs((double)timeMs, args.sampleRate);
        effect.setFeedback(feedback);
        effect.setMix(mix);
        effect.setDiffusion(diffusion);
        effect.setWowRate(0.5f);                    // 14c: fixed wow rate
        effect.setWowDepthMs(modDepth * 4.0f);      // 14c: 0–4 ms wow depth
        effect.setFlutterRate(8.0f);                // 14c: fixed flutter rate
        effect.setFlutterDepthMs(modDepth * 1.0f);  // 14c: 0–1 ms flutter depth
        effect.setTapeSat(tapeSat);           // 14d
        effect.setTapeAge(tapeAge);           // 14d
        effect.setDuckThreshold(duckThresh);  // 14e
        effect.setDuckDepth(duckDepth);       // 14e

        // VCV Rack audio is ±5 V; normalise to ±1 for the effect.
        // When R_AUDIO_INPUT is connected, use processStereo() for ping-pong / wide stereo.
        float sampleL = inputs[AUDIO_INPUT].getVoltage() * 0.2f;
        if (inputs[R_AUDIO_INPUT].isConnected()) {
            float sampleR = inputs[R_AUDIO_INPUT].getVoltage() * 0.2f;
            effect.processStereo(&sampleL, &sampleR, 1);
            float outL = clamp(sampleL * 5.f, -12.f, 12.f);
            float outR = clamp(sampleR * 5.f, -12.f, 12.f);
            outputs[AUDIO_OUTPUT].setVoltage(outL);
            outputs[R_AUDIO_OUTPUT].setVoltage(outR);
            float absMag = std::max(std::abs(outL), std::abs(outR));
            lights[SIGNAL_LIGHT].setSmoothBrightness(absMag > 0.05f ? 1.0f : 0.0f, args.sampleTime);
            lights[CLIP_LIGHT].setSmoothBrightness(absMag > 4.75f ? 1.0f : 0.0f, args.sampleTime);
        } else {
            effect.process(&sampleL, 1);
            float outVoltage = clamp(sampleL * 5.f, -12.f, 12.f);
            outputs[AUDIO_OUTPUT].setVoltage(outVoltage);
            outputs[R_AUDIO_OUTPUT].setVoltage(0.f);
            float absMag = std::abs(outVoltage);
            lights[SIGNAL_LIGHT].setSmoothBrightness(absMag > 0.05f ? 1.0f : 0.0f, args.sampleTime);
            lights[CLIP_LIGHT].setSmoothBrightness(absMag > 4.75f ? 1.0f : 0.0f, args.sampleTime);
        }
    }
};

struct DelayWidget : ModuleWidget {
    explicit DelayWidget(DelayModule* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Delay.svg")));

        // Corner screws (10HP = 50.8 mm wide; right screws at 49.3 mm)
        addChild(createWidget<ScrewSilver>(mm2px(Vec(1.5,  0.5))));
        addChild(createWidget<ScrewSilver>(mm2px(Vec(49.3, 0.5))));
        addChild(createWidget<ScrewSilver>(mm2px(Vec(1.5,  123.0))));
        addChild(createWidget<ScrewSilver>(mm2px(Vec(49.3, 123.0))));

        // Three main knobs — centred at x=25.4 mm (panel centre)
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(25.4, 22.0)), module, DelayModule::TIME_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(25.4, 44.0)), module, DelayModule::FEEDBACK_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(25.4, 66.0)), module, DelayModule::MIX_PARAM));

        // Mod Depth and Diffusion small knobs (14c/14f) — flanking large knobs at x=10.16/40.64
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(10.16, 76.0)), module, DelayModule::MOD_DEPTH_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(40.64, 76.0)), module, DelayModule::DIFFUSION_PARAM));

        // Tape controls (14d): Sat toggle left, Age small knob right
        addParam(createParamCentered<CKSS>              (mm2px(Vec(10.16, 83.0)), module, DelayModule::TAPE_SAT_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(40.64, 83.0)), module, DelayModule::TAPE_AGE_PARAM));

        // Duck controls (14e): Threshold left, Depth right; Self Osc toggle centre (14g)
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(10.16, 93.0)), module, DelayModule::DUCK_THRESHOLD_PARAM));
        addParam(createParamCentered<CKSS>              (mm2px(Vec(25.4,  93.0)), module, DelayModule::SELF_OSC_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(40.64, 93.0)), module, DelayModule::DUCK_DEPTH_PARAM));

        // CV row: Time, Feedback, Mix at y = 104 mm
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.16, 104.0)), module, DelayModule::TIME_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(20.32, 104.0)), module, DelayModule::FEEDBACK_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.48, 104.0)), module, DelayModule::MIX_CV_INPUT));

        // Timing inputs row (20e/20f): TAP | CLK | V/OCT at y = 111 mm
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.16, 111.0)), module, DelayModule::TAP_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(20.32, 111.0)), module, DelayModule::CLK_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.48, 111.0)), module, DelayModule::PITCH_INPUT));

        // Audio in / bypass / out (L channel at y=118, R channel at y=118/123)
        addInput(createInputCentered<PJ301MPort> (mm2px(Vec(10.16, 118.0)), module, DelayModule::AUDIO_INPUT));
        addInput(createInputCentered<PJ301MPort> (mm2px(Vec(20.32, 118.0)), module, DelayModule::BYPASS_INPUT));
        addInput(createInputCentered<PJ301MPort> (mm2px(Vec(30.48, 118.0)), module, DelayModule::R_AUDIO_INPUT));   // 15
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(40.64, 118.0)), module, DelayModule::AUDIO_OUTPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(40.64, 123.0)), module, DelayModule::R_AUDIO_OUTPUT)); // 15

        // Signal (green) and clip (red) indicator lights above the output jack
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(40.64, 109.5)), module, DelayModule::SIGNAL_LIGHT));
        addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(40.64, 112.0)), module, DelayModule::CLIP_LIGHT));

        // Panel text labels (nanosvg does not render SVG <text> elements)
        const NVGcolor blue = nvgRGB(0x6e, 0x9a, 0xc8);
        const NVGcolor prim = nvgRGB(0xaa, 0xaa, 0xaa);
        const NVGcolor sec  = nvgRGB(0x88, 0x88, 0x88);
        const NVGcolor dim  = nvgRGB(0x55, 0x55, 0x55);
        const NVGcolor sub  = nvgRGB(0x66, 0x66, 0x66);

        addChild(panelLabel(mm2px(Vec(25.40,  5.93)), "DELAY",   blue, 9.f));
        addChild(panelLabel(mm2px(Vec(25.40,  9.65)), "ANOESIS", sub,  5.f));

        addChild(panelLabel(mm2px(Vec(25.40, 16.09)), "TIME",     prim, 7.f));
        addChild(panelLabel(mm2px(Vec(25.40, 38.09)), "FEEDBACK", prim, 7.f));
        addChild(panelLabel(mm2px(Vec(25.40, 60.11)), "MIX",      prim, 7.f));

        addChild(panelLabel(mm2px(Vec(10.16, 70.10)), "MOD",   prim, 6.f));
        addChild(panelLabel(mm2px(Vec(40.64, 70.10)), "DIFF",  prim, 6.f));
        addChild(panelLabel(mm2px(Vec(10.16, 77.21)), "SAT",   prim, 6.f));
        addChild(panelLabel(mm2px(Vec(40.64, 77.21)), "AGE",   prim, 6.f));
        addChild(panelLabel(mm2px(Vec(10.16, 87.37)), "DUCK",  prim, 6.f));
        addChild(panelLabel(mm2px(Vec(25.40, 87.37)), "OSC",   blue, 6.f));
        addChild(panelLabel(mm2px(Vec(40.64, 87.37)), "DEPTH", prim, 6.f));

        addChild(panelLabel(mm2px(Vec(20.32,  98.08)), "CV",    dim, 5.f));
        addChild(panelLabel(mm2px(Vec(10.16, 100.68)), "TIM",   sec, 5.5f));
        addChild(panelLabel(mm2px(Vec(20.32, 100.68)), "FB",    sec, 5.5f));
        addChild(panelLabel(mm2px(Vec(30.48, 100.68)), "MIX",   sec, 5.5f));

        addChild(panelLabel(mm2px(Vec(10.16, 107.44)), "TAP",   sec, 5.5f));
        addChild(panelLabel(mm2px(Vec(20.32, 107.44)), "CLK",   sec, 5.5f));
        addChild(panelLabel(mm2px(Vec(30.48, 107.44)), "V/OCT", sec, 5.5f));

        addChild(panelLabel(mm2px(Vec(10.16, 121.36)), "L IN",  sec,  5.5f));
        addChild(panelLabel(mm2px(Vec(20.32, 121.36)), "BYP",   sec,  5.5f));
        addChild(panelLabel(mm2px(Vec(30.48, 121.36)), "R IN",  sec,  5.5f));
        addChild(panelLabel(mm2px(Vec(40.64, 121.36)), "L OUT", blue, 5.5f));
        addChild(panelLabel(mm2px(Vec(40.64, 125.45)), "R OUT", blue, 5.5f));
    }

    void appendContextMenu(Menu* menu) override {
        ModuleWidget::appendContextMenu(menu);
        menu->addChild(new MenuSeparator);

        // Interpolation mode display (read-only; Lagrange auto-selected when Tape Sat is on)
        bool tapeSatOn = module
            && module->params[DelayModule::TAPE_SAT_PARAM].getValue() > 0.5f;
        menu->addChild(createMenuLabel(
            std::string("Interpolation: ") + (tapeSatOn ? "Lagrange (tape)" : "Linear (digital)")));

        menu->addChild(new MenuSeparator);

        // Reset delay buffer without resetting knob positions
        menu->addChild(createMenuItem("Reset delay buffer", "", [this]() {
            if (module)
                static_cast<DelayModule*>(module)->effect.reset();
        }));
    }
};

Model* modelDelay = createModel<DelayModule, DelayWidget>("Delay");
