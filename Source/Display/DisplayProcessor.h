#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Common/Hub.h"

//==============================================================================
/**
    Transcriber Live Display — mostra as frases de todos os Receivers como
    conversa. Os servidores (UDP + web) vivem no Hub compartilhado, que já sobe
    com qualquer instância da ferramenta. Não processa áudio (passthrough).
*/
class DisplayAudioProcessor : public juce::AudioProcessor
{
public:
    DisplayAudioProcessor();
    ~DisplayAudioProcessor() override = default;

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet();
    }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}   // áudio passa intacto

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override         { return "Transcriber Live Display"; }
    bool acceptsMidi() const override                   { return false; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 0.0; }
    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //-- API p/ a UI ----------------------------------------------------------------
    juce::SharedResourcePointer<tl::Hub> hub;
    tl::MessageStore& store() { return hub->store; }

    int  fontSize = 26;
    bool flashEnabled = true;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DisplayAudioProcessor)
};
