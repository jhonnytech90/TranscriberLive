#include "DisplayProcessor.h"
#include "DisplayEditor.h"
#include "Common/Log.h"

//==============================================================================
DisplayAudioProcessor::DisplayAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    TL_LOGI ("display", juce::String ("=== nova instancia === host: ")
             + juce::PluginHostType().getHostDescription()
             + "  |  formato: " + juce::AudioProcessor::getWrapperTypeDescription (wrapperType));
}

DisplayAudioProcessor::~DisplayAudioProcessor()
{
    TL_LOGI ("display", "instancia removida");
}

juce::AudioProcessorEditor* DisplayAudioProcessor::createEditor()
{
    return new DisplayAudioProcessorEditor (*this);
}

void DisplayAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("DISPLAY");
    state.setProperty ("fontSize", fontSize, nullptr);
    state.setProperty ("flashEnabled", flashEnabled, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DisplayAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.hasType ("DISPLAY")) return;
        fontSize     = (int)  state.getProperty ("fontSize", fontSize);
        flashEnabled = (bool) state.getProperty ("flashEnabled", flashEnabled);
    }
}

//==============================================================================
#ifndef TRANSCRIBER_DISPLAY_APP
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DisplayAudioProcessor();
}
#endif
