#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Common/TranscriptionEngine.h"
#include "Common/MessageBus.h"
#include "Common/Hub.h"
#include "Common/License.h"

//==============================================================================
/**
    Transcriber Live Receiver — um por canal de microfone. Transcreve a voz
    falada do sinal que o host entrega e manda as frases para o Display (hub
    local + UDP para um Display remoto, se configurado). O áudio passa intacto.
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
    juce::SharedResourcePointer<tl::Hub> hub;   // sobe UDP + web mesmo sem Display aberto

    float getInputLevelDb() const noexcept   { return inputLevelDb.load(); }

    /** Vazio = tudo certo. Preenchido = alguma etapa da inicializacao falhou
        (o plugin continua carregado e o audio passa, mas nao transcreve). */
    juce::String getInitError() const        { return initError; }

    //-- Licença (sem chave válida o plugin não transcreve; o áudio passa intacto) --
    bool            isLicensed() const noexcept   { return licensed.load(); }
    tl::LicenseInfo getLicenseInfo() const        { const juce::ScopedLock sl (licenseLock); return licenseInfo; }
    tl::LicenseInfo activateLicense (const juce::String& licenseText);
    void            removeLicense();

    //-- Modelos (pasta padrão: <dados do usuário>/TranscriberLive/models) ---------
    juce::StringArray getAvailableModels() const;        // nomes de arquivo ggml-*.bin (sem o VAD)
    juce::String      getSelectedModel() const           { return selectedModel; }
    void              selectModel (const juce::String& fileName);
    bool              hasVadModel() const                { return vadFile.existsAsFile(); }

    //-- Identidade do canal (Receiver -> Display) ---------------------------------
    struct Identity
    {
        juce::String channelId;          // UUID fixo desta instância
        juce::String name { "Cantor" };
        juce::Colour colour { 0xff3ddc84 };
        int  importance = 1;             // 1 normal, 2 importante, 3 urgente
        bool flash = true;               // Display pisca quando este canal fala
        juce::String remoteHost;         // vazio = só o Display desta máquina; ex.: 192.168.0.20
        int  remotePort = tl::kDefaultBusPort;
    };

    Identity getIdentity() const                 { const juce::ScopedLock sl (identityLock); return identity; }
    void     setIdentity (const Identity& id);

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static constexpr const char* kParamGate      = "gate";       // dBFS
    static constexpr const char* kParamHold      = "hold";       // ms
    static constexpr const char* kParamVadSens   = "vadsens";    // 0..1
    static constexpr const char* kParamPartials  = "partials";   // bool

private:
    void pushSettingsToEngine();
    void timerCallback() override;               // heartbeat "hello"
    void sendLine (const TranscriptionEngine::Line& line);
    void send (const tl::Message& m);
    tl::Message makeMessage (const juce::String& type) const;
    void autoSelectModel();
    void loadModels();
    void refreshLicense();

    juce::String initError;

    std::atomic<bool> licensed { false };
    mutable juce::CriticalSection licenseLock;
    tl::LicenseInfo licenseInfo;

    std::atomic<float>* gateParam     = nullptr;
    std::atomic<float>* holdParam     = nullptr;
    std::atomic<float>* vadSensParam  = nullptr;
    std::atomic<float>* partialsParam = nullptr;

    // Reamostragem host -> 16 kHz (lowpass + interpolação linear com fase contínua)
    double hostSampleRate = 48000.0;
    double resampleRatio  = 3.0;
    double resamplePhase  = 0.0;
    float  lastInputSample = 0.0f;
    juce::dsp::IIR::Filter<float> antiAlias1, antiAlias2;
    std::vector<float> monoBuffer, resampledBuffer;

    std::atomic<float> inputLevelDb { -100.0f };

    juce::String selectedModel;
    juce::File   modelFile, vadFile;

    mutable juce::CriticalSection identityLock;
    Identity identity;
    tl::BusSender localBus, remoteBus;

    int vitalsId = 0;   // registro na linha VITAIS do log

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranscriberLiveAudioProcessor)
};
