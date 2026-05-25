#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

#include "CabinetIR.h"
#include "Overdrive.h"
#include "Delay.h"

class DistantEchoProcessor : public juce::AudioProcessor {
public:
    DistantEchoProcessor();
    ~DistantEchoProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Distant-Echo"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override
    {
        return (double)*apvts.getRawParameterValue("dl_time") / 1000.0;
    }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // One instance per channel (max stereo).
    std::array<Overdrive,  2> overdrive;
    std::array<CabinetIR,  2> cabinet;    // 16g: post-overdrive speaker cabinet IR
    std::array<Delay,      2> delay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DistantEchoProcessor)
};
