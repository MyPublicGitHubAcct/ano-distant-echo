#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "AnoLookAndFeel.h"
#include "PluginProcessor.h"

class DistantEchoEditor : public juce::AudioProcessorEditor {
public:
    explicit DistantEchoEditor(DistantEchoProcessor&);
    ~DistantEchoEditor() override { setLookAndFeel(nullptr); }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    juce::ComboBox odType, odShape, odCabinetType;                         // 16g: odCabinetType
    juce::Slider   odDrive, odTone, odLevel;
    juce::Slider   odMid, odPresence;                                     // 13d
    juce::Slider   odBias;                                                // 16a
    juce::ToggleButton odPickSens;                                        // 13e
    juce::ToggleButton odCabinet;                                         // 16g
    juce::ToggleButton odBypass;                                          // 21c

    juce::Slider       dlTime, dlFeedback, dlMix;
    juce::Slider       dlDiffusion;                                       // 14f
    juce::Slider       dlModDepth;                                        // 14c
    juce::ToggleButton dlTapeSat;                                         // 14d
    juce::ToggleButton dlSelfOscillate;                                   // 14g
    juce::ToggleButton dlBypass;                                          // 21c
    juce::ToggleButton dlPingPong;                                        // 15
    juce::Slider       dlTapeAge;                                         // 14d
    juce::Slider       dlDuckThreshold, dlDuckDepth;                      // 14e

    juce::Label odDriveLabel, odToneLabel, odLevelLabel;
    juce::Label odMidLabel, odPresenceLabel, odBiasLabel;                 // 16a: odBiasLabel added
    juce::Label dlTimeLabel, dlFeedbackLabel, dlMixLabel;
    juce::Label dlDiffusionLabel, dlModDepthLabel;                        // 14c/f
    juce::Label dlTapeAgeLabel;                                           // 14d
    juce::Label dlDuckThresholdLabel, dlDuckDepthLabel;                   // 14e

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    ComboAttachment   odTypeAttach, odShapeAttach, odCabinetTypeAttach;    // 16g
    SliderAttachment  odDriveAttach, odToneAttach, odLevelAttach;
    SliderAttachment  odMidAttach, odPresenceAttach;
    SliderAttachment  odBiasAttach;                                       // 16a
    ButtonAttachment  odPickSensAttach;                                   // 13e
    ButtonAttachment  odCabinetAttach;                                    // 16g
    ButtonAttachment  odBypassAttach;                                     // 21c

    SliderAttachment  dlTimeAttach, dlFeedbackAttach, dlMixAttach;
    SliderAttachment  dlDiffusionAttach;                                  // 14f
    SliderAttachment  dlModDepthAttach;                                   // 14c
    ButtonAttachment  dlTapeSatAttach;                                    // 14d
    ButtonAttachment  dlSelfOscillateAttach;                              // 14g
    ButtonAttachment  dlBypassAttach;                                     // 21c
    ButtonAttachment  dlPingPongAttach;                                   // 15
    SliderAttachment  dlTapeAgeAttach;                                    // 14d
    SliderAttachment  dlDuckThresholdAttach, dlDuckDepthAttach;           // 14e

    AnoLookAndFeel anoLnf;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DistantEchoEditor)
};
