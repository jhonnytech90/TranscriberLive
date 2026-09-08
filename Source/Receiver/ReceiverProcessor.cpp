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

    tl::Hub::getModelsDir().createDirectory();
    autoSelectModel();
    refreshLicense();          // só carrega o modelo se houver licença válida
    pushSettingsToEngine();

    identity.channelId = juce::Uuid().toString();
    localBus.setTarget ("127.0.0.1", hub->getBusPort());
    engine.onLine = [this] (const TranscriptionEngine::Line& l) { sendLine (l); };

    startTimer (2000);
}

TranscriberLiveAudioProcessor::~TranscriberLiveAudioProcessor()
{
    stopTimer();
    engine.onLine = nullptr;
}

//==============================================================================
static bool isVadModelName (const juce::String& n)
{
    return n.containsIgnoreCase ("silero") || n.containsIgnoreCase ("vad");
}

juce::StringArray TranscriberLiveAudioProcessor::getAvailableModels() const
{
    juce::StringArray out;
    for (auto& kv : tl::Hub::findModelFiles())
        if (! isVadModelName (kv.first))
            out.add (kv.first);
    out.sort (true);
    return out;
}

void TranscriberLiveAudioProcessor::autoSelectModel()
{
    if (selectedModel.isNotEmpty() && tl::Hub::findModelFiles().count (selectedModel) > 0)
        return;

    const auto models = getAvailableModels();
    selectedModel.clear();

    // preferência: small > base > medium > large-v3-turbo > qualquer
    for (auto* pref : { "small", "base", "medium", "turbo", "large", "tiny" })
    {
        for (auto& m : models)
            if (m.containsIgnoreCase (pref)) { selectedModel = m; break; }
        if (selectedModel.isNotEmpty()) break;
    }
    if (selectedModel.isEmpty() && ! models.isEmpty())
        selectedModel = models[0];
}

void TranscriberLiveAudioProcessor::loadModels()
{
    if (! licensed.load())
        return;

    const auto files = tl::Hub::findModelFiles();

    modelFile = juce::File();
    if (selectedModel.isNotEmpty())
        if (auto it = files.find (selectedModel); it != files.end())
            modelFile = it->second;
    if (modelFile.existsAsFile())
        engine.loadModel (modelFile);

    // VAD: automático, sem escolha do usuário (qualquer ggml-silero*.bin / *vad*.bin nas pastas)
    vadFile = juce::File();
    for (auto& kv : files)
        if (isVadModelName (kv.first)) { vadFile = kv.second; break; }

    if (vadFile.existsAsFile())
        engine.loadVadModel (vadFile);
}

void TranscriberLiveAudioProcessor::selectModel (const juce::String& fileName)
{
    if (fileName == selectedModel) return;
    selectedModel = fileName;
    loadModels();
}

//==============================================================================
void TranscriberLiveAudioProcessor::refreshLicense()
{
    auto info = tl::License::loadInstalled();
    {
        const juce::ScopedLock sl (licenseLock);
        licenseInfo = info;
    }
    licensed.store (info.valid);

    if (info.valid)
        loadModels();
    else
        engine.unloadModels();
}

tl::LicenseInfo TranscriberLiveAudioProcessor::activateLicense (const juce::String& licenseText)
{
    const auto info = tl::License::install (licenseText);
    if (info.valid)
        refreshLicense();
    else
    {
        const juce::ScopedLock sl (licenseLock);
        licenseInfo = info;
    }
    return info;
}

void TranscriberLiveAudioProcessor::removeLicense()
{
    tl::License::uninstall();
    refreshLicense();
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
    remoteBus.setTarget (id.remoteHost, id.remotePort);
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

void TranscriberLiveAudioProcessor::send (const tl::Message& m)
{
    if (! licensed.load())
        return;

    localBus.setTarget ("127.0.0.1", hub->getBusPort());
    localBus.send (m);

    if (getIdentity().remoteHost.isNotEmpty())
        remoteBus.send (m);
}

void TranscriberLiveAudioProcessor::timerCallback()
{
    send (makeMessage ("hello"));
}

void TranscriberLiveAudioProcessor::sendLine (const TranscriptionEngine::Line& line)
{
    if (line.text.isEmpty()) return;
    auto m = makeMessage ("msg");
    m.utteranceId = line.utteranceId;
    m.text = line.text;
    m.isFinal = line.isFinal;
    m.timeMs = line.time.toMilliseconds();
    send (m);
}

void TranscriberLiveAudioProcessor::pushSettingsToEngine()
{
    auto s = engine.getSettings();
    s.gateThresholdDb = gateParam->load();
    s.holdMs          = (int) holdParam->load();
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

    auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, 7000.0f, 0.707f);
    antiAlias1.coefficients = coeffs;
    antiAlias2.coefficients = coeffs;
    antiAlias1.reset();
    antiAlias2.reset();

    monoBuffer.assign ((size_t) juce::jmax (samplesPerBlock, 64), 0.0f);
    resampledBuffer.assign ((size_t) juce::jmax (samplesPerBlock, 64) + 16, 0.0f);
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

    if (! licensed.load())
    {
        inputLevelDb.store (-100.0f);
        return;
    }

    pushSettingsToEngine();

    if ((int) monoBuffer.size() < numSamples)
        return;   // bloco maior que o anunciado (host mal-comportado): não aloca na thread de áudio

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
    state.setProperty ("model", selectedModel, nullptr);

    const auto id = getIdentity();
    state.setProperty ("channelId",  id.channelId, nullptr);
    state.setProperty ("name",       id.name, nullptr);
    state.setProperty ("colour",     id.colour.toString(), nullptr);
    state.setProperty ("importance", id.importance, nullptr);
    state.setProperty ("flash",      id.flash, nullptr);
    state.setProperty ("remoteHost", id.remoteHost, nullptr);
    state.setProperty ("remotePort", id.remotePort, nullptr);

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

            const auto model = state.getProperty ("model", "").toString();
            if (model.isNotEmpty() && model != selectedModel && tl::Hub::findModelFiles().count (model) > 0)
                selectModel (model);

            auto id = getIdentity();
            id.channelId  = state.getProperty ("channelId", id.channelId).toString();
            id.name       = state.getProperty ("name", id.name).toString();
            id.colour     = juce::Colour::fromString (state.getProperty ("colour", id.colour.toString()).toString());
            id.importance = juce::jlimit (1, 3, (int) state.getProperty ("importance", id.importance));
            id.flash      = (bool) state.getProperty ("flash", id.flash);
            id.remoteHost = state.getProperty ("remoteHost", id.remoteHost).toString();
            id.remotePort = (int) state.getProperty ("remotePort", id.remotePort);
            setIdentity (id);
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TranscriberLiveAudioProcessor();
}
