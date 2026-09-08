#include "ReceiverProcessor.h"
#include "ReceiverEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout TranscriberLiveAudioProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { kParamGate, 1 }, "Gate (dBFS)",
        NormalisableRange<float> (-80.0f, -10.0f, 0.5f), -45.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { kParamHold, 1 }, "Fim de frase (ms)",
        NormalisableRange<float> (300.0f, 2000.0f, 10.0f), 700.0f,
        AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { kParamVadSens, 1 }, "Sensibilidade VAD",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { kParamPartials, 1 }, "Mostrar parciais", true));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { kParamListen, 1 }, "Transcrever", true));

    return { params.begin(), params.end() };
}

//==============================================================================
TranscriberLiveAudioProcessor::TranscriberLiveAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createParameterLayout())
{
    gateParam     = apvts.getRawParameterValue (kParamGate);
    holdParam     = apvts.getRawParameterValue (kParamHold);
    vadSensParam  = apvts.getRawParameterValue (kParamVadSens);
    partialsParam = apvts.getRawParameterValue (kParamPartials);
    listenParam   = apvts.getRawParameterValue (kParamListen);

    // procura modelos padrão ao lado do plugin / na pasta do usuário
    auto modelsDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("TranscriberLive").getChildFile ("models");

    if (modelsDir.isDirectory())
    {
        for (auto& f : modelsDir.findChildFiles (juce::File::findFiles, false, "ggml-*.bin"))
        {
            const auto name = f.getFileName();
            if (name.contains ("silero"))                { if (vadFile   == juce::File()) vadFile   = f; }
            else                                         { if (modelFile == juce::File()) modelFile = f; }
        }
    }

    if (modelFile.existsAsFile()) engine.loadModel (modelFile);
    if (vadFile.existsAsFile())   engine.loadVadModel (vadFile);

    pushSettingsToEngine();

    identity.channelId = juce::Uuid().toString();
    bus.setTarget (identity.busHost, identity.busPort);
    engine.onLine = [this] (const TranscriptionEngine::Line& l) { sendLine (l); };

    startTimer (2000);
}

TranscriberLiveAudioProcessor::~TranscriberLiveAudioProcessor()
{
    stopTimer();
    engine.onLine = nullptr;
}

//==============================================================================
void TranscriberLiveAudioProcessor::setIdentity (const Identity& id)
{
    {
        const juce::ScopedLock sl (identityLock);
        const auto keepId = identity.channelId;
        identity = id;
        if (identity.channelId.isEmpty()) identity.channelId = keepId;
    }
    bus.setTarget (id.busHost, id.busPort);
    timerCallback();   // anuncia a mudança já
}

tl::Message TranscriberLiveAudioProcessor::makeMessage (const juce::String& type) const
{
    const auto id = getIdentity();
    tl::Message m;
    m.type = type; m.channelId = id.channelId; m.name = id.name; m.colour = id.colour;
    m.importance = id.importance; m.flash = id.flash;
    m.timeMs = juce::Time::currentTimeMillis();
    return m;
}

void TranscriberLiveAudioProcessor::timerCallback()
{
    bus.send (makeMessage ("hello"));
}

void TranscriberLiveAudioProcessor::sendLine (const TranscriptionEngine::Line& line)
{
    if (line.text.isEmpty()) return;   // parcial descartada: o Display mantém a última parcial até o final
    auto m = makeMessage ("msg");
    m.utteranceId = line.utteranceId;
    m.text = line.text;
    m.isFinal = line.isFinal;
    m.timeMs = line.time.toMilliseconds();
    bus.send (m);
}

void TranscriberLiveAudioProcessor::sendClearToDisplay()
{
    bus.send (makeMessage ("clear"));
}

//==============================================================================
void TranscriberLiveAudioProcessor::setModelFile (const juce::File& f)
{
    modelFile = f;
    if (f.existsAsFile()) engine.loadModel (f);
}

void TranscriberLiveAudioProcessor::setVadFile (const juce::File& f)
{
    vadFile = f;
    if (f.existsAsFile()) engine.loadVadModel (f);
}

void TranscriberLiveAudioProcessor::pushSettingsToEngine()
{
    auto s = engine.getSettings();
    s.gateThresholdDb = gateParam->load();
    s.holdMs          = (int) holdParam->load();
    // sensibilidade 0..1  ->  limiar Silero 0.85..0.25 (mais sensível = limiar menor)
    s.vadThreshold    = juce::jmap (vadSensParam->load(), 0.0f, 1.0f, 0.85f, 0.25f);
    s.showPartials    = partialsParam->load() > 0.5f;
    s.numThreads      = juce::jlimit (2, 8, juce::SystemStats::getNumCpus() - 1);
    engine.setSettings (s);
}

//==============================================================================
void TranscriberLiveAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostSampleRate = sampleRate;
    resampleRatio  = sampleRate / (double) TranscriptionEngine::kSampleRate;
    resamplePhase  = 0.0;
    lastInputSample = 0.0f;

    // anti-alias: 2 biquadas Butterworth em ~7 kHz (banda de 16 kHz)
    auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, 7000.0f, 0.707f);
    antiAlias1.coefficients = coeffs;
    antiAlias2.coefficients = coeffs;
    antiAlias1.reset();
    antiAlias2.reset();

    monoBuffer.assign ((size_t) samplesPerBlock, 0.0f);
    resampledBuffer.assign ((size_t) samplesPerBlock + 16, 0.0f);
}

void TranscriberLiveAudioProcessor::releaseResources() {}

bool TranscriberLiveAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;

    return in == out;
}

void TranscriberLiveAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples == 0 || numChannels == 0)
        return;

    // O áudio passa intacto — este plugin só escuta.

    if (listenParam->load() < 0.5f)
    {
        inputLevelDb.store (-100.0f);
        return;
    }

    pushSettingsToEngine();

    if ((int) monoBuffer.size() < numSamples)
    {
        // host mandou bloco maior que o anunciado: evita alocar na thread de áudio
        // (só acontece em hosts mal-comportados); descarta o bloco.
        return;
    }

    // ---- entrada -> mono (o host já entrega o canal certo; se vier estéreo, soma) ----
    juce::FloatVectorOperations::copy (monoBuffer.data(), buffer.getReadPointer (0), numSamples);
    for (int ch = 1; ch < numChannels; ++ch)
        juce::FloatVectorOperations::add (monoBuffer.data(), buffer.getReadPointer (ch), numSamples);
    if (numChannels > 1)
        juce::FloatVectorOperations::multiply (monoBuffer.data(), 1.0f / (float) numChannels, numSamples);

    // ---- medidor ----------------------------------------------------------------
    {
        double sum = 0.0;
        for (int i = 0; i < numSamples; ++i) sum += (double) monoBuffer[(size_t) i] * monoBuffer[(size_t) i];
        const float db = (float) (10.0 * std::log10 (sum / numSamples + 1e-12));
        // ballistics simples: sobe rápido, desce devagar
        const float prev = inputLevelDb.load();
        inputLevelDb.store (db > prev ? db : prev - 1.5f);
    }

    // ---- anti-alias + reamostragem para 16 kHz ---------------------------------
    for (int i = 0; i < numSamples; ++i)
    {
        float x = monoBuffer[(size_t) i];
        x = antiAlias1.processSample (x);
        x = antiAlias2.processSample (x);
        monoBuffer[(size_t) i] = x;
    }

    int outCount = 0;
    const int outCapacity = (int) resampledBuffer.size();

    // fase contínua entre blocos: resamplePhase é a posição fracionária de leitura
    // relativa ao início do bloco atual (pode ser negativa: usa lastInputSample)
    // (a fase fica em [-1, ratio-1); idx == -1 usa a última amostra do bloco anterior)
    while (resamplePhase < (double) (numSamples - 1) && outCount < outCapacity)
    {
        const int    idx  = (int) std::floor (resamplePhase);
        const float  frac = (float) (resamplePhase - (double) idx);
        const float  a    = idx < 0 ? lastInputSample : monoBuffer[(size_t) idx];
        const float  b    = monoBuffer[(size_t) (idx + 1)];
        resampledBuffer[(size_t) outCount++] = a + (b - a) * frac;
        resamplePhase += resampleRatio;
    }

    resamplePhase -= (double) numSamples;
    lastInputSample = monoBuffer[(size_t) (numSamples - 1)];

    if (outCount > 0)
        engine.pushAudio (resampledBuffer.data(), outCount);
}

//==============================================================================
juce::AudioProcessorEditor* TranscriberLiveAudioProcessor::createEditor()
{
    return new TranscriberLiveAudioProcessorEditor (*this);
}

//==============================================================================
void TranscriberLiveAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("modelFile", modelFile.getFullPathName(), nullptr);
    state.setProperty ("vadFile",   vadFile.getFullPathName(),   nullptr);
    state.setProperty ("fontSize",  fontSize, nullptr);

    const auto id = getIdentity();
    state.setProperty ("channelId",  id.channelId, nullptr);
    state.setProperty ("name",       id.name, nullptr);
    state.setProperty ("colour",     id.colour.toString(), nullptr);
    state.setProperty ("importance", id.importance, nullptr);
    state.setProperty ("flash",      id.flash, nullptr);
    state.setProperty ("busHost",    id.busHost, nullptr);
    state.setProperty ("busPort",    id.busPort, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void TranscriberLiveAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (state);

            fontSize = (int) state.getProperty ("fontSize", fontSize);

            auto id = getIdentity();
            id.channelId  = state.getProperty ("channelId", id.channelId).toString();
            id.name       = state.getProperty ("name", id.name).toString();
            id.colour     = juce::Colour::fromString (state.getProperty ("colour", id.colour.toString()).toString());
            id.importance = juce::jlimit (1, 3, (int) state.getProperty ("importance", id.importance));
            id.flash      = (bool) state.getProperty ("flash", id.flash);
            id.busHost    = state.getProperty ("busHost", id.busHost).toString();
            id.busPort    = (int) state.getProperty ("busPort", id.busPort);
            setIdentity (id);

            const juce::File m (state.getProperty ("modelFile", "").toString());
            const juce::File v (state.getProperty ("vadFile", "").toString());
            if (m.existsAsFile() && m != modelFile) setModelFile (m);
            if (v.existsAsFile() && v != vadFile)   setVadFile (v);
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TranscriberLiveAudioProcessor();
}
