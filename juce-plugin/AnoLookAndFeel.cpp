#include "AnoLookAndFeel.h"

AnoLookAndFeel::AnoLookAndFeel()
{
    // Dark panel background for popup menus
    setColour (juce::PopupMenu::backgroundColourId,            juce::Colour (0xff1e1e38));
    setColour (juce::PopupMenu::textColourId,                  juce::Colour (0xffcccccc));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff2a2a50));
    setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colours::white);

    // ComboBox defaults — accent border is overridden per-combo in the editor
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff1a1a2e));
    setColour (juce::ComboBox::textColourId,       juce::Colour (0xffcccccc));
    setColour (juce::ComboBox::outlineColourId,    juce::Colour (0xff555566));
    setColour (juce::ComboBox::arrowColourId,      juce::Colour (0xffaaaaaa));

    // Default rotary fill — overridden per-slider in the editor
    setColour (juce::Slider::rotarySliderFillColourId,  juce::Colour (0xffc8a96e));
    setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff333344));
    setColour (juce::Slider::textBoxTextColourId,       juce::Colour (0xffaaaaaa));
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff1a1a2e));
    setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId,  juce::Colour (0xff333355));
}

void AnoLookAndFeel::drawRotarySlider (juce::Graphics& g,
                                       int x, int y, int width, int height,
                                       float sliderPosProportional,
                                       float rotaryStartAngle, float rotaryEndAngle,
                                       juce::Slider& slider)
{
    const float radius  = (float) juce::jmin (width / 2, height / 2) - 4.0f;
    const float centreX = (float) x + (float) width  * 0.5f;
    const float centreY = (float) y + (float) height * 0.5f;
    const float trackR  = radius - 3.0f;                // arc drawn inside the knob body
    const float angle   = rotaryStartAngle
                          + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    // Knob body — dark circle
    g.setColour (juce::Colour (0xff222233));
    g.fillEllipse (centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f);

    // Track arc — full sweep, dark gray
    {
        juce::Path track;
        track.addArc (centreX - trackR, centreY - trackR, trackR * 2.0f, trackR * 2.0f,
                      rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
        g.strokePath (track, juce::PathStrokeType (3.0f,
                              juce::PathStrokeType::curved,
                              juce::PathStrokeType::rounded));
    }

    // Value arc — accent color, from start to current angle
    if (sliderPosProportional > 0.0f)
    {
        juce::Path fill;
        fill.addArc (centreX - trackR, centreY - trackR, trackR * 2.0f, trackR * 2.0f,
                     rotaryStartAngle, angle, true);
        g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
        g.strokePath (fill, juce::PathStrokeType (3.0f,
                             juce::PathStrokeType::curved,
                             juce::PathStrokeType::rounded));
    }

    // Pointer line — white, from knob center outward
    {
        const float pointerLen = radius * 0.6f;
        const float pointerW   = 2.0f;
        juce::Path p;
        p.addRectangle (-pointerW * 0.5f, -(radius - 1.0f), pointerW, pointerLen);
        p.applyTransform (juce::AffineTransform::rotation (angle)
                                                .translated (centreX, centreY));
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillPath (p);
    }
}

void AnoLookAndFeel::drawComboBox (juce::Graphics& g,
                                   int width, int height,
                                   bool /*isButtonDown*/,
                                   int /*buttonX*/, int /*buttonY*/,
                                   int /*buttonW*/, int /*buttonH*/,
                                   juce::ComboBox& box)
{
    const float cornerSize = 3.0f;
    const juce::Rectangle<float> bounds (0.0f, 0.0f, (float) width, (float) height);

    // Flat dark fill
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, cornerSize);

    // Accent-colored border
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds.reduced (0.5f), cornerSize, 1.0f);

    // Dropdown arrow
    const float arrowH    = 5.0f;
    const float arrowW    = 8.0f;
    const float arrowRight = (float) width - 8.0f;
    const float arrowTop   = ((float) height - arrowH) * 0.5f;
    juce::Path arrow;
    arrow.addTriangle (arrowRight - arrowW, arrowTop,
                       arrowRight,          arrowTop,
                       arrowRight - arrowW * 0.5f, arrowTop + arrowH);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}
