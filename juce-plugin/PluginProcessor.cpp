#include "PluginProcessor.h"
#include "PluginEditor.h"

DistantEchoProcessor::DistantEchoProcessor()
    : AudioProcessor(BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{}

juce::AudioProcessorValueTreeState::ParameterLayout
DistantEchoProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"od_type", 1}, "OD Type",
        juce::StringArray{"Hard Clip", "Soft Clip", "Foldback", "Asymmetric", "Bitcrush"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"od_shape", 1}, "Clip Shape",
        juce::StringArray{"Flat", "Mid Focus", "Bright Focus"}, 0));  // 13c
    auto pct = [](float v, int) { return juce::String(int(v * 100)) + "%"; };
    auto fromPct = [](const juce::String& s) { return s.getFloatValue() / 100.0f; };

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"od_drive", 1}, "OD Drive",
        juce::NormalisableRange<float>{0.0f, 1.0f}, 0.5f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction(pct)
                                             .withValueFromStringFunction(fromPct)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"od_tone", 1}, "OD Tone",
        juce::NormalisableRange<float>{0.0f, 1.0f}, 0.5f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction(pct)
                                             .withValueFromStringFunction(fromPct)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"od_level", 1}, "OD Level",
        juce::NormalisableRange<float>{0.0f, 1.0f}, 0.8f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction(pct)
                                             .withValueFromStringFunction(fromPct)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"od_mid", 1}, "OD Mid",
        juce::NormalisableRange<float>{-6.0f, 10.0f}, 0.0f,            // 13d
        juce::AudioParameterFloatAttributes{}
            .withStringFromValueFunction([](float v, int) {
                return (v >= 0.0f ? "+" : "") + juce::String(v, 1) + " dB"; })
            .withValueFromStringFunction([](const juce::String& s) { return s.getFloatValue(); })));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"od_presence", 1}, "OD Presence",
        juce::NormalisableRange<float>{0.0f, 8.0f}, 0.0f,              // 13d
        juce::AudioParameterFloatAttributes{}
            .withStringFromValueFunction([](float v, int) {
                return "+" + juce::String(v, 1) + " dB"; })
            .withValueFromStringFunction([](const juce::String& s) { return s.getFloatValue(); })));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"od_pick_sens", 1}, "Pick Sensitivity", true));  // 13e
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"od_bias", 1}, "OD Bias",
        juce::NormalisableRange<float>{-0.5f, 0.5f}, 0.0f,                 // 16a
        juce::AudioParameterFloatAttributes{}
            .withStringFromValueFunction([](float v, int) {
                return (v >= 0.0f ? "+" : "") + juce::String(v, 2); })
            .withValueFromStringFunction([](const juce::String& s) { return s.getFloatValue(); })));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"od_cabinet", 1}, "Cabinet IR", false));         // 16g
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"od_cabinet_type", 1}, "Cabinet Type",
        juce::StringArray{"1x12 Open-Back", "4x12 Closed-Back",
                          "1x12 Combo"}, 0));                             // 16g
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"od_bypass", 1}, "OD Bypass", false));           // 21c

    auto fromMs  = [](const juce::String& s) { return s.getFloatValue(); };
    auto fromDb  = [](const juce::String& s) { return s.getFloatValue(); };

    // Skewed range: finer control at shorter delay times.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"dl_time", 1}, "Delay Time",
        juce::NormalisableRange<float>{1.0f, 2000.0f, 0.1f, 0.4f}, 300.0f,
        juce::AudioParameterFloatAttributes{}
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 0) + " ms"; })
            .withValueFromStringFunction(fromMs)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"dl_feedback", 1}, "Feedback",
        juce::NormalisableRange<float>{0.0f, 1.02f}, 0.4f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction(pct)
                                             .withValueFromStringFunction(fromPct)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"dl_mix", 1}, "Delay Mix",
        juce::NormalisableRange<float>{0.0f, 1.0f}, 0.5f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction(pct)
                                             .withValueFromStringFunction(fromPct)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"dl_diffusion", 1}, "Diffusion",
        juce::NormalisableRange<float>{0.0f, 1.0f}, 0.0f,              // 14f
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction(pct)
                                             .withValueFromStringFunction(fromPct)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"dl_mod_depth", 1}, "Mod Depth",
        juce::NormalisableRange<float>{0.0f, 1.0f}, 0.0f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction(pct)
                                             .withValueFromStringFunction(fromPct)));  // 14c
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"dl_tape_sat", 1}, "Tape Saturation", false));               // 14d
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"dl_tape_age", 1}, "Tape Age",
        juce::NormalisableRange<float>{0.0f, 1.0f}, 0.0f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction(pct)
                                             .withValueFromStringFunction(fromPct)));  // 14d
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"dl_duck_threshold", 1}, "Duck Threshold",
        juce::NormalisableRange<float>{-30.0f, 0.0f}, 0.0f,
        juce::AudioParameterFloatAttributes{}
            .withStringFromValueFunction([](float v, int) { return juce::String(v, 0) + " dB"; })
            .withValueFromStringFunction(fromDb)));                                    // 14e
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"dl_duck_depth", 1}, "Duck Depth",
        juce::NormalisableRange<float>{0.0f, 1.0f}, 0.0f,
        juce::AudioParameterFloatAttributes{}.withStringFromValueFunction(pct)
                                             .withValueFromStringFunction(fromPct)));  // 14e
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"dl_self_oscillate", 1}, "Self Oscillate", false));      // 14g
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"dl_bypass", 1}, "Delay Bypass", false));                // 21c
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"dl_ping_pong", 1}, "Ping-Pong", false));                // 15

    return {params.begin(), params.end()};
}

bool DistantEchoProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void DistantEchoProcessor::releaseResources()
{
    for (auto& od  : overdrive) od.reset();
    for (auto& cab : cabinet)   cab.reset();
    for (auto& dl  : delay)     dl.reset();
}

void DistantEchoProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    for (auto& od : overdrive)
        od.prepare(sampleRate, samplesPerBlock);
    for (auto& cab : cabinet)
        cab.prepare(sampleRate, samplesPerBlock);
    for (auto& dl : delay)
        dl.prepare(sampleRate, samplesPerBlock);
}

void DistantEchoProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto  odType   = static_cast<DistortionType>(
                               (int)*apvts.getRawParameterValue("od_type"));
    const auto  odShape  = static_cast<ClipShape>(
                               (int)*apvts.getRawParameterValue("od_shape"));
    const float drive    = *apvts.getRawParameterValue("od_drive");
    const float tone     = *apvts.getRawParameterValue("od_tone");
    const float level    = *apvts.getRawParameterValue("od_level");
    const float mid      = *apvts.getRawParameterValue("od_mid");
    const float presence = *apvts.getRawParameterValue("od_presence");
    const float bias     = *apvts.getRawParameterValue("od_bias");          // 16a
    const bool        cabEnabled = *apvts.getRawParameterValue("od_cabinet") > 0.5f; // 16g
    const CabinetType cabType    = static_cast<CabinetType>(
                                     (int)*apvts.getRawParameterValue("od_cabinet_type")); // 16g
    const float timeMs    = *apvts.getRawParameterValue("dl_time");
    const float feedback  = *apvts.getRawParameterValue("dl_feedback");
    const float mix       = *apvts.getRawParameterValue("dl_mix");
    const float diffusion      = *apvts.getRawParameterValue("dl_diffusion");       // 14f
    const float modDepth       = *apvts.getRawParameterValue("dl_mod_depth");       // 14c
    const bool  pickSens       = *apvts.getRawParameterValue("od_pick_sens") > 0.5f; // 13e
    const bool  odBypass       = *apvts.getRawParameterValue("od_bypass")    > 0.5f; // 21c
    const bool  tapeSat        = *apvts.getRawParameterValue("dl_tape_sat")  > 0.5f; // 14d
    const float tapeAge        = *apvts.getRawParameterValue("dl_tape_age");          // 14d
    const float duckThreshold  = *apvts.getRawParameterValue("dl_duck_threshold");    // 14e
    const float duckDepth      = *apvts.getRawParameterValue("dl_duck_depth");        // 14e
    const bool  selfOscillate   = *apvts.getRawParameterValue("dl_self_oscillate") > 0.5f; // 14g
    const bool  dlBypass       = *apvts.getRawParameterValue("dl_bypass")    > 0.5f; // 21c
    const bool  pingPong       = *apvts.getRawParameterValue("dl_ping_pong") > 0.5f; // 15

    const int numChannels = std::min(buffer.getNumChannels(), 2);
    const int numSamples  = buffer.getNumSamples();

    // Overdrive + cabinet IR: independent per channel
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto i = (size_t)ch;
        cabinet[i].setEnabled(cabEnabled);   // 16g
        cabinet[i].setType(cabType);         // 16g
        if (odBypass) {                      // 21c
            overdrive[i].reset();
            cabinet[i].reset();
        } else {
            overdrive[i].setDistortionType(odType);
            overdrive[i].setClipShape(odShape);
            overdrive[i].setDrive(drive);
            overdrive[i].setTone(tone);
            overdrive[i].setLevel(level);
            overdrive[i].setMid(mid);
            overdrive[i].setPresence(presence);
            overdrive[i].setPickSensitive(pickSens);    // 13e
            overdrive[i].setBias(bias);                 // 16a
            overdrive[i].process(buffer.getWritePointer(ch), numSamples);
            cabinet[i].process(buffer.getWritePointer(ch), numSamples);  // 16g
        }
    }

    // Delay: stereo-aware processing through delay[0] only.
    // processStereo() handles ping-pong cross-feedback and independent-stereo time offset.
    if (dlBypass) {                                 // 21c
        for (auto& dl : delay) dl.reset();
    } else {
        delay[0].setSelfOscillate(selfOscillate);    // 14g
        delay[0].setTimeMs(timeMs, getSampleRate());
        delay[0].setFeedback(feedback);
        delay[0].setMix(mix);
        delay[0].setDiffusion(diffusion);            // 14f
        delay[0].setWowRate(0.5f);                   // 14c: fixed rate
        delay[0].setWowDepthMs(modDepth * 4.0f);     // 14c: 0–4 ms wow depth
        delay[0].setFlutterRate(8.0f);               // 14c: fixed rate
        delay[0].setFlutterDepthMs(modDepth * 1.0f); // 14c: 0–1 ms flutter depth
        delay[0].setTapeSat(tapeSat);                // 14d
        delay[0].setTapeAge(tapeAge);                // 14d
        delay[0].setDuckThreshold(duckThreshold);    // 14e
        delay[0].setDuckDepth(duckDepth);            // 14e
        delay[0].setPingPong(pingPong);              // 15

        if (numChannels == 2) {
            delay[0].processStereo(buffer.getWritePointer(0),
                                   buffer.getWritePointer(1), numSamples);
        } else {
            delay[0].process(buffer.getWritePointer(0), numSamples);
        }
    }

    for (int ch = 0; ch < numChannels; ++ch) {
        auto* data = buffer.getWritePointer(ch);
        for (int s = 0; s < numSamples; ++s)
            data[s] = std::clamp(data[s], -1.0f, 1.0f);
    }
}

void DistantEchoProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer,
                                                juce::MidiBuffer&)
{
    // True bypass: copy input to output and reset effect state so stale
    // filter/delay history does not bleed through when re-engaged.
    const int numChannels = std::min(buffer.getNumChannels(), 2);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto i = (size_t)ch;
        overdrive[i].reset();
        cabinet[i].reset();
        delay[i].reset();
    }

    // JUCE's default processBlockBypassed copies input→output for in-place
    // buses; call the base to handle that correctly.
    juce::MidiBuffer emptyMidi;
    AudioProcessor::processBlockBypassed(buffer, emptyMidi);
}

void DistantEchoProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto xml = apvts.copyState().createXml();
    copyXmlToBinary(*xml, destData);
}

void DistantEchoProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* DistantEchoProcessor::createEditor()
{
    return new DistantEchoEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DistantEchoProcessor();
}
