#include "PluginEditor.h"

DistantEchoEditor::DistantEchoEditor(DistantEchoProcessor& p)
    : AudioProcessorEditor(&p),
      odTypeAttach         (p.apvts, "od_type",           odType),
      odShapeAttach        (p.apvts, "od_shape",           odShape),
      odCabinetTypeAttach  (p.apvts, "od_cabinet_type",    odCabinetType), // 16g
      odDriveAttach        (p.apvts, "od_drive",           odDrive),
      odToneAttach         (p.apvts, "od_tone",            odTone),
      odLevelAttach        (p.apvts, "od_level",           odLevel),
      odMidAttach          (p.apvts, "od_mid",             odMid),
      odPresenceAttach     (p.apvts, "od_presence",        odPresence),
      odBiasAttach         (p.apvts, "od_bias",            odBias),        // 16a
      odPickSensAttach     (p.apvts, "od_pick_sens",       odPickSens),    // 13e
      odCabinetAttach      (p.apvts, "od_cabinet",         odCabinet),     // 16g
      odBypassAttach       (p.apvts, "od_bypass",          odBypass),      // 21c
      dlTimeAttach         (p.apvts, "dl_time",            dlTime),
      dlFeedbackAttach     (p.apvts, "dl_feedback",        dlFeedback),
      dlMixAttach          (p.apvts, "dl_mix",             dlMix),
      dlDiffusionAttach    (p.apvts, "dl_diffusion",       dlDiffusion),   // 14f
      dlModDepthAttach     (p.apvts, "dl_mod_depth",       dlModDepth),    // 14c
      dlTapeSatAttach      (p.apvts, "dl_tape_sat",        dlTapeSat),     // 14d
      dlSelfOscillateAttach(p.apvts, "dl_self_oscillate",  dlSelfOscillate), // 14g
      dlBypassAttach       (p.apvts, "dl_bypass",          dlBypass),      // 21c
      dlPingPongAttach     (p.apvts, "dl_ping_pong",       dlPingPong),    // 15
      dlTapeAgeAttach      (p.apvts, "dl_tape_age",        dlTapeAge),     // 14d
      dlDuckThresholdAttach(p.apvts, "dl_duck_threshold",  dlDuckThreshold), // 14e
      dlDuckDepthAttach    (p.apvts, "dl_duck_depth",      dlDuckDepth)    // 14e
{
    // OD Type combo
    odType.addItem("Hard Clip",  1);
    odType.addItem("Soft Clip",  2);
    odType.addItem("Foldback",   3);
    odType.addItem("Asymmetric", 4);
    odType.addItem("Bitcrush",   5);
    addAndMakeVisible(odType);

    // OD Shape combo (13c)
    odShape.addItem("Flat",         1);
    odShape.addItem("Mid Focus",    2);
    odShape.addItem("Bright Focus", 3);
    addAndMakeVisible(odShape);

    // Cabinet Type combo (16g)
    odCabinetType.addItem("1x12 Open-Back",    1);
    odCabinetType.addItem("4x12 Closed-Back",  2);
    odCabinetType.addItem("1x12 Combo",        3);
    addAndMakeVisible(odCabinetType);

    auto setupKnob = [this](juce::Slider& s, juce::Label& l, const juce::String& text) {
        s.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
        addAndMakeVisible(s);
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
        addAndMakeVisible(l);
    };

    setupKnob(odDrive,    odDriveLabel,    "Drive");
    setupKnob(odTone,     odToneLabel,     "Tone");
    setupKnob(odLevel,    odLevelLabel,    "Level");
    setupKnob(odMid,      odMidLabel,      "Mid");            // 13d
    setupKnob(odPresence, odPresenceLabel, "Presence");       // 13d
    setupKnob(odBias,     odBiasLabel,     "Bias");           // 16a

    // Pick Sensitivity toggle (13e)
    odPickSens.setButtonText("Pick Sensitivity");
    addAndMakeVisible(odPickSens);

    // Cabinet IR toggle (16g)
    odCabinet.setButtonText("Cabinet IR");
    addAndMakeVisible(odCabinet);

    // Overdrive bypass (21c)
    odBypass.setButtonText("Bypass");
    addAndMakeVisible(odBypass);

    setupKnob(dlTime,     dlTimeLabel,     "Time");
    setupKnob(dlFeedback, dlFeedbackLabel, "Feedback");
    setupKnob(dlMix,      dlMixLabel,      "Mix");
    setupKnob(dlDiffusion,   dlDiffusionLabel,   "Diffusion");    // 14f
    setupKnob(dlModDepth,    dlModDepthLabel,    "Mod Depth");    // 14c
    setupKnob(dlTapeAge,     dlTapeAgeLabel,     "Tape Age");     // 14d
    setupKnob(dlDuckThreshold, dlDuckThresholdLabel, "Duck Thr"); // 14e
    setupKnob(dlDuckDepth,   dlDuckDepthLabel,   "Duck Depth");   // 14e

    // Tape Saturation toggle (14d)
    dlTapeSat.setButtonText("Tape Saturation");
    addAndMakeVisible(dlTapeSat);

    // Self Oscillate toggle (14g)
    dlSelfOscillate.setButtonText("Self Oscillate");
    addAndMakeVisible(dlSelfOscillate);

    // Delay bypass (21c)
    dlBypass.setButtonText("Bypass");
    addAndMakeVisible(dlBypass);

    // Ping-Pong stereo mode (15)
    dlPingPong.setButtonText("Ping-Pong");
    addAndMakeVisible(dlPingPong);

    // --- Custom LookAndFeel (21a) ---
    // Install the LAF on the editor so all child components inherit it.
    setLookAndFeel(&anoLnf);

    // OD section: gold accent on rotary fill and combo borders
    constexpr juce::uint32 kGold = 0xffc8a96e;
    for (auto* s : { &odDrive, &odTone, &odLevel, &odMid, &odPresence, &odBias })
        s->setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(kGold));
    for (auto* c : { &odType, &odShape, &odCabinetType })
        c->setColour(juce::ComboBox::outlineColourId, juce::Colour(kGold));

    // Delay section: blue accent on rotary fill (no combos in DL section)
    constexpr juce::uint32 kBlue = 0xff4477aa;
    for (auto* s : { &dlTime, &dlFeedback, &dlMix,
                     &dlDiffusion, &dlModDepth, &dlTapeAge,
                     &dlDuckThreshold, &dlDuckDepth })
        s->setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(kBlue));

    setSize(600, 400);
}

void DistantEchoEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a2e));

    // Section headers — gold for Overdrive, blue for Delay (21k)
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(13.0f).withStyle("Bold")));
    g.setColour(juce::Colour(0xffc8a96e));
    g.drawText("OVERDRIVE", juce::Rectangle<int>(0, 4, 300, 20),
               juce::Justification::centred);
    g.setColour(juce::Colour(0xff4477aa));
    g.drawText("DELAY",     juce::Rectangle<int>(300, 4, 300, 20),
               juce::Justification::centred);

    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
    g.setColour(juce::Colour(0xff666677));
    g.drawText("ANOESIS", juce::Rectangle<int>(0,   22, 300, 12),
               juce::Justification::centred);
    g.drawText("ANOESIS", juce::Rectangle<int>(300, 22, 300, 12),
               juce::Justification::centred);

    g.setColour(juce::Colour(0xff333355));
    g.drawLine(300.0f, 0.0f, 300.0f, (float)getHeight(), 1.5f);

    // Separator between row 1 and row 2 in the Delay section
    const int dlRow2Y = 186;
    g.setColour(juce::Colour(0xff222244));
    g.drawLine(304.0f, (float)(dlRow2Y - 2), 596.0f, (float)(dlRow2Y - 2), 0.5f);
}

void DistantEchoEditor::resized()
{
    constexpr int headerH = 36;
    constexpr int comboH  = 22;
    constexpr int labelH  = 16;
    constexpr int toggleH = 28;

    // Bypass buttons in section headers (21c)
    odBypass.setBounds(224, 8, 72, 20);
    dlBypass.setBounds(524, 8, 72, 20);

    // --- Overdrive section (x: 0..299) ---
    odType       .setBounds(4, headerH,                       292, comboH);
    odShape      .setBounds(4, headerH +   (comboH + 2),      292, comboH);
    odCabinetType.setBounds(4, headerH + 2*(comboH + 2),      292, comboH);  // 16g

    const int knobY = headerH + 3 * comboH + 3 * 2;             // = 110
    const int knobH = getHeight() - knobY - labelH - toggleH - 4; // = 242
    const int knobW = 300 / 6;                                   // = 50

    auto placeKnob = [&](juce::Slider& s, juce::Label& l, int col) {
        int x = col * knobW;
        s.setBounds(x, knobY, knobW, knobH);
        l.setBounds(x, knobY + knobH, knobW, labelH);
    };
    placeKnob(odDrive,    odDriveLabel,    0);
    placeKnob(odTone,     odToneLabel,     1);
    placeKnob(odLevel,    odLevelLabel,    2);
    placeKnob(odMid,      odMidLabel,      3);
    placeKnob(odPresence, odPresenceLabel, 4);
    placeKnob(odBias,     odBiasLabel,     5);                       // 16a

    // Pick Sensitivity and Cabinet IR side-by-side in the bottom toggle row (13e / 16g)
    const int pickY = knobY + knobH + labelH + 2;
    odPickSens.setBounds(4,   pickY, 144, toggleH);
    odCabinet .setBounds(150, pickY, 146, toggleH);

    // --- Delay section (x: 300..599) ---
    // Row 1: 3 main knobs (Time, Feedback, Mix)
    constexpr int dlRow1H = 140;
    constexpr int dlRow1Y = headerH;
    const     int dlRow1LabelY = dlRow1Y + dlRow1H;

    const int dlMainW = 300 / 3;  // 100px each
    auto placeDl = [&](juce::Slider& s, juce::Label& l, int col) {
        int x = 300 + col * dlMainW;
        s.setBounds(x, dlRow1Y, dlMainW, dlRow1H);
        l.setBounds(x, dlRow1LabelY, dlMainW, labelH);
    };
    placeDl(dlTime,     dlTimeLabel,     0);
    placeDl(dlFeedback, dlFeedbackLabel, 1);
    placeDl(dlMix,      dlMixLabel,      2);

    // Row 2: 4 secondary knobs (Diffusion, Tape Age, Duck Thr, Duck Depth)
    constexpr int dlRow2H = 90;
    constexpr int dlRow2Y = dlRow1LabelY + labelH + 4;   // = 186
    const     int dlRow2LabelY = dlRow2Y + dlRow2H;

    const int dlSecW = 300 / 5;  // 60px each (Mod Depth added as 5th)
    auto placeDl2 = [&](juce::Slider& s, juce::Label& l, int col) {
        int x = 300 + col * dlSecW;
        s.setBounds(x, dlRow2Y, dlSecW, dlRow2H);
        l.setBounds(x, dlRow2LabelY, dlSecW, labelH - 2);
    };
    placeDl2(dlModDepth,      dlModDepthLabel,      0);   // 14c
    placeDl2(dlDiffusion,     dlDiffusionLabel,     1);
    placeDl2(dlTapeAge,       dlTapeAgeLabel,       2);
    placeDl2(dlDuckThreshold, dlDuckThresholdLabel, 3);
    placeDl2(dlDuckDepth,     dlDuckDepthLabel,     4);

    // Tape Sat + Self Oscillate toggles side-by-side (14d / 14g)
    const int dlTapeSatY = dlRow2LabelY + (labelH - 2) + 4;
    dlTapeSat.setBounds(300,      dlTapeSatY, 150, toggleH);
    dlSelfOscillate.setBounds(450, dlTapeSatY, 150, toggleH);

    // Ping-Pong stereo toggle (15) — full Delay section width below the row above
    dlPingPong.setBounds(300, dlTapeSatY + toggleH + 4, 300, toggleH);
}
