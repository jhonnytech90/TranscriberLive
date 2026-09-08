#include "DisplayProcessor.h"
#include "DisplayEditor.h"
#include "BinaryData.h"

//==============================================================================
DisplayAudioProcessor::DisplayAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      listener ([this] (const tl::Message& m) { store.handle (m); }),
      web (store, BinaryData::display_html, BinaryData::display_htmlSize)
{
    restartServices();
}

DisplayAudioProcessor::~DisplayAudioProcessor()
{
    web.stop();
    listener.stop();
}

void DisplayAudioProcessor::setPorts (int newBusPort, int newHttpPort, bool enableWeb)
{
    busPort    = newBusPort  > 0 ? newBusPort  : tl::kDefaultBusPort;
    httpPort   = newHttpPort > 0 ? newHttpPort : tl::kDefaultHttpPort;
    webEnabled = enableWeb;
    restartServices();
}

void DisplayAudioProcessor::restartServices()
{
    listener.start (busPort);
    web.stop();
    if (webEnabled) web.start (httpPort);
}

//==============================================================================
juce::AudioProcessorEditor* DisplayAudioProcessor::createEditor()
{
    return new DisplayAudioProcessorEditor (*this);
}

void DisplayAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("DISPLAY");
    state.setProperty ("busPort",  busPort,  nullptr);
    state.setProperty ("httpPort", httpPort, nullptr);
    state.setProperty ("webEnabled", webEnabled, nullptr);
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
        setPorts ((int) state.getProperty ("busPort", busPort),
                  (int) state.getProperty ("httpPort", httpPort),
                  (bool) state.getProperty ("webEnabled", webEnabled));
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DisplayAudioProcessor();
}
