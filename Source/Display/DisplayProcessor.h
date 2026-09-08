#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Common/Protocol.h"
#include "Common/MessageBus.h"
#include "Common/MessageStore.h"
#include "Common/HttpServer.h"

//==============================================================================
/**
    Transcriber Live Display — recebe as frases de todos os Receivers (UDP) e
    mostra como conversa. Também serve a página web para celular/tablet.
    Não processa áudio (passthrough).
*/
class DisplayAudioProcessor : public juce::AudioProcessor
{
public:
    DisplayAudioProcessor();
    ~DisplayAudioProcessor() override;

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet();
    }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}   // áudio passa intacto

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override         { return JucePlugin_Name; }
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
    tl::MessageStore store;

    int  getBusPort() const noexcept   { return busPort; }
    int  getHttpPort() const noexcept  { return httpPort; }
    bool isBusListening() const        { return listener.isBound(); }
    bool isWebRunning() const          { return web.isRunning(); }
    int  getWebClients() const         { return web.getNumClients(); }
    bool isWebEnabled() const noexcept { return webEnabled; }

    void setPorts (int newBusPort, int newHttpPort, bool enableWeb);

    int  fontSize = 26;
    bool flashEnabled = true;

private:
    void restartServices();

    tl::BusListener listener;
    tl::HttpServer  web;
    int  busPort  = tl::kDefaultBusPort;
    int  httpPort = tl::kDefaultHttpPort;
    bool webEnabled = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DisplayAudioProcessor)
};
