#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Custom LookAndFeel for Distant-Echo.
// Renders rotary sliders as a dark-body knob with a 270° track arc and a filled
// accent-color arc from the start position to the current value.  ComboBoxes use
// a flat dark fill with a 1 px rounded-rect border.
//
// Accent color is driven per-control: call
//   slider.setColour(juce::Slider::rotarySliderFillColourId, accentColour)
// on each knob in the editor after setting this LAF.  OD section uses gold
// (0xffc8a96e); Delay section uses blue (0xff4477aa).
class AnoLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AnoLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnoLookAndFeel)
};
