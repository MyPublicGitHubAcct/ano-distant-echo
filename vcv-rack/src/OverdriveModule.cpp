#include "plugin.hpp"
#include "CabinetIR.h"
#include "Overdrive.h"

struct OverdriveModule : Module {
    enum ParamIds {
        TYPE_PARAM,
        SHAPE_PARAM,        // 13c: ClipShape
        DRIVE_PARAM,
        TONE_PARAM,
        LEVEL_PARAM,
        MID_PARAM,          // 13d: peaking EQ at 800 Hz
        PRESENCE_PARAM,     // 13d: high shelf at 4 kHz
        PICK_PARAM,         // 13e: pick sensitivity on/off
        BIAS_PARAM,         // 16a: DC offset before waveshaper
        CABINET_PARAM,      // 16g: speaker cabinet IR on/off (panel CKSS toggle)
        CABINET_TYPE_PARAM, // 16g: cabinet preset (context menu)
        NUM_PARAMS
    };
    enum InputIds {
        AUDIO_INPUT,
        DRIVE_CV_INPUT,
        TONE_CV_INPUT,
        LEVEL_CV_INPUT,
        MID_CV_INPUT,      // 13d
        PICK_CV_INPUT,     // 13e — center of the lower CV row
        PRESENCE_CV_INPUT, // 13d
        BIAS_CV_INPUT,     // 16a
        BYPASS_INPUT,      // 20h: gate > 1 V → audio passthrough + effect reset
        NUM_INPUTS
    };
    enum OutputIds {
        AUDIO_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightIds {
        SIGNAL_LIGHT,  // green — audio present (> 0.05 V)
        CLIP_LIGHT,    // red   — output clipping (> 4.75 V)
        NUM_LIGHTS
    };

    Overdrive effect;
    CabinetIR cabinet;       // 16g: post-overdrive speaker cabinet IR
    bool bypassHigh = false; // 20h: previous gate state for rising-edge detection

    OverdriveModule() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configSwitch(TYPE_PARAM,  0.f, 4.f, 0.f, "Distortion Type",
            {"Hard Clip", "Soft Clip", "Foldback", "Asymmetric", "Bitcrush"});
        configSwitch(SHAPE_PARAM, 0.f, 2.f, 0.f, "Clip Shape",
            {"Flat", "Mid Focus", "Bright Focus"});
        configParam(DRIVE_PARAM,   0.f, 1.f, 0.5f, "Drive");
        configParam(TONE_PARAM,    0.f, 1.f, 0.5f, "Tone");
        configParam(LEVEL_PARAM,   0.f, 1.f, 0.8f, "Level");
        configParam(MID_PARAM,    -6.f, 10.f, 0.f, "Mid", " dB");          // 13d
        configParam(PRESENCE_PARAM, 0.f, 8.f, 0.f, "Presence", " dB");    // 13d
        configSwitch(PICK_PARAM,  0.f, 1.f, 1.f, "Pick Sensitivity",
            {"Off", "On"});                                                 // 13e
        configParam(BIAS_PARAM, -0.5f, 0.5f, 0.f, "Bias");                // 16a
        configSwitch(CABINET_PARAM, 0.f, 1.f, 0.f, "Cabinet IR",
            {"Off", "On"});                                                // 16g
        configSwitch(CABINET_TYPE_PARAM, 0.f, 2.f, 0.f, "Cabinet Type",
            {"1x12 Open-Back", "4x12 Closed-Back", "1x12 Combo"});        // 16g
        configInput(AUDIO_INPUT,       "Audio");
        configInput(DRIVE_CV_INPUT,    "Drive CV");
        configInput(TONE_CV_INPUT,     "Tone CV");
        configInput(LEVEL_CV_INPUT,    "Level CV");
        configInput(MID_CV_INPUT,      "Mid CV");           // 13d
        configInput(PICK_CV_INPUT,     "Pick Sens CV");     // 13e
        configInput(PRESENCE_CV_INPUT, "Presence CV");      // 13d
        configInput(BIAS_CV_INPUT,     "Bias CV");           // 16a
        configInput(BYPASS_INPUT,      "Bypass");            // 20h
        configOutput(AUDIO_OUTPUT, "Audio");
        configBypass(AUDIO_INPUT, AUDIO_OUTPUT);            // 20k: pass audio through on bypass
    }

    void onSampleRateChange(const SampleRateChangeEvent& e) override {
        effect.prepare((double)e.sampleRate, 1);
        cabinet.prepare((double)e.sampleRate, 1);
    }

    void onReset(const ResetEvent& /*e*/) override {
        effect.prepare(APP->engine->getSampleRate(), 1);
        cabinet.prepare(APP->engine->getSampleRate(), 1);
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
                lights[SIGNAL_LIGHT].setBrightness(0.f);
                lights[CLIP_LIGHT].setBrightness(0.f);
                return;
            }
            bypassHigh = nowHigh;
        }

        effect.setDistortionType(
            static_cast<DistortionType>((int)params[TYPE_PARAM].getValue()));
        effect.setClipShape(
            static_cast<ClipShape>((int)params[SHAPE_PARAM].getValue()));

        // CV inputs are ±5V; map to ±0.5 offset on a 0–1 knob
        float drive = clamp(params[DRIVE_PARAM].getValue()
                          + inputs[DRIVE_CV_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
        float tone  = clamp(params[TONE_PARAM].getValue()
                          + inputs[TONE_CV_INPUT].getVoltage() * 0.1f,  0.f, 1.f);
        float level = clamp(params[LEVEL_PARAM].getValue()
                          + inputs[LEVEL_CV_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
        // Mid CV: ±5V → ±8 dB offset
        float mid = clamp(params[MID_PARAM].getValue()
                        + inputs[MID_CV_INPUT].getVoltage() * 1.6f, -6.f, 10.f);
        // Presence CV: ±5V → ±4 dB offset
        float presence = clamp(params[PRESENCE_PARAM].getValue()
                             + inputs[PRESENCE_CV_INPUT].getVoltage() * 0.8f, 0.f, 8.f);
        // Pick sensitivity: knob OR gate CV > 1V enables
        bool pickOn = params[PICK_PARAM].getValue() > 0.5f;
        if (inputs[PICK_CV_INPUT].isConnected())
            pickOn = inputs[PICK_CV_INPUT].getVoltage() > 1.0f;

        // Bias CV: ±5 V → ±0.5 offset on a −0.5..+0.5 knob
        float bias = clamp(params[BIAS_PARAM].getValue()
                         + inputs[BIAS_CV_INPUT].getVoltage() * 0.1f, -0.5f, 0.5f);

        effect.setDrive(drive);
        effect.setTone(tone);
        effect.setLevel(level);
        effect.setMid(mid);
        effect.setPresence(presence);
        effect.setPickSensitive(pickOn);   // 13e
        effect.setBias(bias);              // 16a

        // VCV Rack audio is ±5 V; normalise to ±1 for the effect
        float sample = inputs[AUDIO_INPUT].getVoltage() * 0.2f;
        effect.process(&sample, 1);
        cabinet.setEnabled(params[CABINET_PARAM].getValue() > 0.5f);   // 16g
        cabinet.setType(static_cast<CabinetType>(                     // 16g
            (int)params[CABINET_TYPE_PARAM].getValue()));
        cabinet.process(&sample, 1);                                  // 16g
        float outVoltage = clamp(sample * 5.f, -12.f, 12.f);
        outputs[AUDIO_OUTPUT].setVoltage(outVoltage);

        float absMag = std::abs(outVoltage);
        lights[SIGNAL_LIGHT].setSmoothBrightness(absMag > 0.05f ? 1.0f : 0.0f, args.sampleTime);
        lights[CLIP_LIGHT].setSmoothBrightness(absMag > 4.75f ? 1.0f : 0.0f, args.sampleTime);
    }
};

struct OverdriveWidget : ModuleWidget {
    explicit OverdriveWidget(OverdriveModule* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Overdrive.svg")));

        // Corner screws (10HP = 50.8 mm wide; right screws at 49.3 mm)
        addChild(createWidget<ScrewSilver>(mm2px(Vec(1.5,  0.5))));
        addChild(createWidget<ScrewSilver>(mm2px(Vec(49.3, 0.5))));
        addChild(createWidget<ScrewSilver>(mm2px(Vec(1.5,  123.0))));
        addChild(createWidget<ScrewSilver>(mm2px(Vec(49.3, 123.0))));

        // Left column: Drive/Tone/Level large knobs at x=15.24 mm
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(15.24, 22.0)), module, OverdriveModule::DRIVE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(15.24, 40.0)), module, OverdriveModule::TONE_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(15.24, 58.0)), module, OverdriveModule::LEVEL_PARAM));

        // Right column: Mid/Presence small knobs + Pick toggle at x=35.56 mm (13d/13e)
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(35.56, 22.0)), module, OverdriveModule::MID_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(35.56, 40.0)), module, OverdriveModule::PRESENCE_PARAM));
        addParam(createParamCentered<CKSS>(mm2px(Vec(35.56, 58.0)), module, OverdriveModule::PICK_PARAM));
        // 16a: Bias small knob below Pick, CV at reserved slot in CV row 2
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(35.56, 66.0)), module, OverdriveModule::BIAS_PARAM));

        // CV row 1 at y=76 mm: Drive, Tone, Level, Mid
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.16, 76.0)), module, OverdriveModule::DRIVE_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(20.32, 76.0)), module, OverdriveModule::TONE_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.48, 76.0)), module, OverdriveModule::LEVEL_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(40.64, 76.0)), module, OverdriveModule::MID_CV_INPUT));

        // CV row 2 at y=89 mm: Presence, Pick, Bias; x=40.64 reserved for TYPE/SHAPE CV (TODO 20d)
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.16, 89.0)), module, OverdriveModule::PRESENCE_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(20.32, 89.0)), module, OverdriveModule::PICK_CV_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.48, 89.0)), module, OverdriveModule::BIAS_CV_INPUT)); // 16a

        // Cabinet IR enable toggle (16g) — centred between CV rows and audio section
        addParam(createParamCentered<CKSS>(mm2px(Vec(25.4, 96.0)), module, OverdriveModule::CABINET_PARAM));

        // Audio in / bypass / out
        addInput(createInputCentered<PJ301MPort> (mm2px(Vec(10.16, 112.0)), module, OverdriveModule::AUDIO_INPUT));
        addInput(createInputCentered<PJ301MPort> (mm2px(Vec(25.4,  112.0)), module, OverdriveModule::BYPASS_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(40.64, 112.0)), module, OverdriveModule::AUDIO_OUTPUT));

        // Signal (green) and clip (red) indicator lights above the output jack
        addChild(createLightCentered<SmallLight<GreenLight>>(mm2px(Vec(40.64, 101.0)), module, OverdriveModule::SIGNAL_LIGHT));
        addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(40.64, 104.5)), module, OverdriveModule::CLIP_LIGHT));
    }

    void appendContextMenu(Menu* menu) override {
        ModuleWidget::appendContextMenu(menu);
        auto* m = dynamic_cast<OverdriveModule*>(module);
        if (!m) return;

        // Distortion Type
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Distortion Type"));
        static const std::vector<std::string> typeLabels = {
            "Hard Clip", "Soft Clip", "Foldback", "Asymmetric", "Bitcrush"
        };
        for (int i = 0; i < (int)typeLabels.size(); ++i) {
            menu->addChild(createCheckMenuItem(typeLabels[i], "",
                [=]() { return (int)m->params[OverdriveModule::TYPE_PARAM].getValue() == i; },
                [=]() { m->params[OverdriveModule::TYPE_PARAM].setValue((float)i); }
            ));
        }

        // Cabinet type preset (16g) — enable/disable via panel CKSS toggle
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Cabinet Type"));
        static const std::vector<std::string> cabLabels = {
            "1x12 Open-Back", "4x12 Closed-Back", "1x12 Combo"
        };
        for (int i = 0; i < (int)cabLabels.size(); ++i) {
            menu->addChild(createCheckMenuItem(cabLabels[i], "",
                [=]() { return (int)m->params[OverdriveModule::CABINET_TYPE_PARAM].getValue() == i; },
                [=]() { m->params[OverdriveModule::CABINET_TYPE_PARAM].setValue((float)i); }
            ));
        }

        // Clip Shape (13c)
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Clip Shape"));
        static const std::vector<std::string> shapeLabels = {
            "Flat", "Mid Focus", "Bright Focus"
        };
        for (int i = 0; i < (int)shapeLabels.size(); ++i) {
            menu->addChild(createCheckMenuItem(shapeLabels[i], "",
                [=]() { return (int)m->params[OverdriveModule::SHAPE_PARAM].getValue() == i; },
                [=]() { m->params[OverdriveModule::SHAPE_PARAM].setValue((float)i); }
            ));
        }
    }
};

Model* modelOverdrive = createModel<OverdriveModule, OverdriveWidget>("Overdrive");
