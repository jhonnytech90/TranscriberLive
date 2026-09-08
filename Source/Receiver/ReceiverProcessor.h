#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Common/TranscriptionEngine.h"
#include "Common/MessageBus.h"

//==============================================================================
/**
    Transcriber Live — plugin VST3/AU que transcreve em tempo real a voz falada
    do canal onde está inserido. O áudio passa intacto (bypass) — o plugin só
    "escuta".
*/
class TranscriberLiveAudioProcessor : public juce::AudioProcessor,
                                      private juce::Timer
{
public:
    TranscriberLiveAudioProcessor();
    ~TranscriberLiveAudioProcessor() override;

    //-- AudioProcessor -----------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

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

    //-- API para a UI -------------------------------------------------------------
    juce::AudioProcessorValueTreeState apvts;
    TranscriptionEngine engine;

    float getInputLevelDb() const noexcept   { return inputLevelDb.load(); }

    juce::File getModelFile() const          { return modelFile; }
    juce::File getVadFile() const            { return vadFile; }
    void setModelFile (const juce::File& f);
    void setVadFile (const juce::File& f);

    int  fontSize = 34;   // preferência de UI, salva no estado

    //-- Identidade do canal (Receiver -> Display) ---------------------------------
    struct Identity
    {
        juce::String channelId;          // UUID fixo desta instância
        juce::String name { "Cantor" };
        juce::Colour colour { 0xff3ddc84 };
        int  importance = 1;             // 1 normal, 2 importante, 3 urgente
        bool flash = true;               // Display pisca quando este canal fala
        juce::String busHost { "127.0.0.1" };
        int  busPort = tl::kDefaultBusPort;
    };

    Identity getIdentity() const                 { const juce::ScopedLock sl (identityLock); return identity; }
    void     setIdentity (const Identity& id);
    void     sendClearToDisplay();

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // IDs dos parâmetros
    static constexpr const char* kParamGate      = "gate";       // dBFS
    static constexpr const char* kParamHold      = "hold";       // ms
    static constexpr const char* kParamVadSens   = "vadsens";    // 0..1
    static constexpr const char* kParamPartials  = "partials";   // bool
    static constexpr const char* kParamListen    = "listen";     // bool (liga/desliga transcrição)

private:
    void pushSettingsToEngine();
    void timerCallback() override;               // heartbeat "hello" p/ o Display
    void sendLine (const TranscriptionEngine::Line& line);
    tl::Message makeMessage (const juce::String& type) const;

    mutable juce::CriticalSection identityLock;
    Identity identity;
    tl::BusSender bus;

    std::atomic<float>* gateParam     = nullptr;
    std::atomic<float>* holdParam     = nullptr;
    std::atomic<float>* vadSensParam  = nullptr;
    std::atomic<float>* partialsParam = nullptr;
    std::atomic<float>* listenParam   = nullptr;

    // Reamostragem host -> 16 kHz (lowpass + interpolação linear com fase contínua)
    double hostSampleRate = 48000.0;
    double resampleRatio  = 3.0;        // host / 16k
    double resamplePhase  = 0.0;
    float  lastInputSample = 0.0f;
    juce::dsp::IIR::Filter<float> antiAlias1, antiAlias2;
    std::vector<float> monoBuffer, resampledBuffer;

    std::atomic<float> inputLevelDb { -100.0f };

    juce::File modelFile, vadFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranscriberLiveAudioProcessor)
};
