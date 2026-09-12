#include "ReceiverProcessor.h"
#include "ReceiverEditor.h"
#include "Common/Trace.h"
#include "Common/Log.h"

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
    // O cabeçalho do log já traz SO, CPU e SIMD. Aqui entra o que só o plugin
    // sabe: quem é o host e em que formato fomos carregados.
    TL_LOGI ("receiver", juce::String ("=== nova instancia === host: ")
             + juce::PluginHostType().getHostDescription()
             + "  |  formato: " + juce::AudioProcessor::getWrapperTypeDescription (wrapperType));

    gateParam     = apvts.getRawParameterValue (kParamGate);
    holdParam     = apvts.getRawParameterValue (kParamHold);
    vadSensParam  = apvts.getRawParameterValue (kParamVadSens);
    partialsParam = apvts.getRawParameterValue (kParamPartials);

    // Nenhuma etapa daqui pode derrubar o host: um plugin que joga excecao no
    // construtor faz o host fechar a janela (ou fechar junto).
    try
    {
        { TL_TRACE_STEP ("pasta de modelos");   tl::Hub::getModelsDir().createDirectory(); }
        { TL_TRACE_STEP ("varredura de modelos"); autoSelectModel(); }
        { TL_TRACE_STEP ("licenca");            refreshLicense(); }
        { TL_TRACE_STEP ("settings do motor");  pushSettingsToEngine(); }

        identity.channelId = juce::Uuid().toString();

        { TL_TRACE_STEP ("hub (rede)");         localBus.setTarget ("127.0.0.1", hub->getBusPort()); }

        engine.onLine = [this] (const TranscriptionEngine::Line& l) { sendLine (l); };
        startTimer (2000);
    }
    catch (const std::exception& e)
    {
        TL_LOGE ("receiver", juce::String ("EXCECAO no construtor: ") + e.what());
        initError = e.what();
    }
    catch (...)
    {
        TL_LOGE ("receiver", "EXCECAO desconhecida no construtor");
        initError = "erro desconhecido na inicializacao";
    }

    // Vitais do lado do áudio: nível de entrada e taxa do host. Junto com os
    // vitais do motor, uma linha por segundo conta a sessão inteira.
    vitalsId = tl::Log::get().addVitals ([this] (juce::String& linha)
    {
        linha << juce::String::formatted ("  entrada=%.0fdB %.0fHz licenca=%d modelo=",
                                          inputLevelDb.load(), hostSampleRate,
                                          licensed.load() ? 1 : 0)
              << (selectedModel.isNotEmpty() ? selectedModel : juce::String ("nenhum"));
    });

    TL_LOGI ("receiver", "instancia criada"
             + juce::String (initError.isEmpty() ? "" : " COM ERRO: " + initError));
}

TranscriberLiveAudioProcessor::~TranscriberLiveAudioProcessor()
{
    tl::Log::get().removeVitals (vitalsId);
    TL_LOGI ("receiver", "instancia removida");
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

// Do mais leve para o mais pesado. Serve tanto para escolher o padrao quanto
// para saber para onde descer quando a maquina nao acompanha.
static const char* const kModelosPorPeso[] = { "tiny", "base", "small", "medium", "turbo", "large" };

/*  Por que NAO ha mais palpite por hardware aqui.

    Havia: "Apple Silicon ou 4+ nucleos fisicos leva small, o resto comeca no
    base". Parecia razoavel e estava errado -- eu calibrei aquele corte olhando
    medicoes feitas com o ggml compilado em modo generico (sem AVX2/NEON). Ou
    seja, calibrei em cima de um bug: as maquinas nao eram lentas, o build era.

    Corrigido o build, qualquer palpite que eu fizesse agora seria chute, e um
    chute que custa QUALIDADE -- porque o base reconhece pior que o small em
    portugues, e comecar no base "por precaucao" entrega texto pior a quem
    aguentava o modelo bom.

    Entao: comeca sempre no melhor modelo disponivel e deixa a MEDICAO decidir.
    Se o RTF mostrar que a maquina nao acompanha, o plugin desce um degrau (e
    avisa). Erra uma vez, no maximo, e erra para o lado da qualidade.         */

juce::String TranscriberLiveAudioProcessor::escolherModeloMaisLeve (const juce::String& atual) const
{
    const auto models = getAvailableModels();
    if (models.isEmpty())
        return {};

    // posicao do atual na escala de peso
    int pesoAtual = -1;
    for (int i = 0; i < (int) juce::numElementsInArray (kModelosPorPeso); ++i)
        if (atual.containsIgnoreCase (kModelosPorPeso[i])) { pesoAtual = i; break; }

    if (pesoAtual <= 0)
        return {};   // desconhecido, ou ja e o mais leve

    // o mais pesado que ainda seja mais leve que o atual
    for (int i = pesoAtual - 1; i >= 0; --i)
        for (auto& m : models)
            if (m.containsIgnoreCase (kModelosPorPeso[i]))
                return m;

    return {};
}

void TranscriberLiveAudioProcessor::autoSelectModel()
{
    if (selectedModel.isNotEmpty() && tl::Hub::findModelFiles().count (selectedModel) > 0)
        return;

    const auto models = getAvailableModels();
    selectedModel.clear();

    // preferencia por QUALIDADE; a medicao rebaixa depois, se precisar
    for (auto* pref : { "small", "medium", "turbo", "large", "base", "tiny" })
    {
        for (auto& m : models)
            if (m.containsIgnoreCase (pref)) { selectedModel = m; break; }
        if (selectedModel.isNotEmpty()) break;
    }
    if (selectedModel.isEmpty() && ! models.isEmpty())
        selectedModel = models[0];

    TL_LOGI ("modelo", juce::String ("padrao: ")
             + (selectedModel.isEmpty() ? juce::String ("nenhum") : selectedModel)
             + juce::String::formatted ("  (%d nucleos fisicos; a medicao do RTF "
                                        "rebaixa se nao acompanhar)",
                                        juce::SystemStats::getNumPhysicalCpus()));
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

    // Metade dos chamados de suporte é "não transcreve" por falta de modelo na
    // pasta. Esta linha responde isso de imediato.
    if (! modelFile.existsAsFile())
    {
        juce::StringArray onde;
        for (auto& d : tl::Hub::getModelSearchDirs())
            onde.add (d.getFullPathName() + (d.isDirectory() ? "" : " (nao existe)"));

        TL_LOGE ("modelo", "nenhum modelo encontrado. Procurei em: " + onde.joinIntoString ("  |  "));
    }
    if (! vadFile.existsAsFile())
        TL_LOGW ("vad", "nenhum ggml-silero*.bin encontrado -- o corte de frase vai usar so o gate de nivel");
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
    {
        TL_LOGI ("licenca", "valida -- " + info.name + " (" + info.machineId + ")"
                 + (info.expires.isEmpty() ? juce::String (", perpetua")
                                           : ", vence em " + info.expires));
        loadModels();
    }
    else
    {
        TL_LOGW ("licenca", "sem licenca ativa: " + info.error
                 + "  |  arquivo: " + tl::License::getLicenseFile().getFullPathName());
        engine.unloadModels();
    }
}

tl::LicenseInfo TranscriberLiveAudioProcessor::activateLicense (const juce::String& licenseText)
{
    TL_LOGI ("licenca", "tentando ativar (" + juce::String (licenseText.length()) + " caracteres colados)");
    const auto info = tl::License::install (licenseText);
    if (info.valid)
    {
        TL_LOGI ("licenca", "ativada com sucesso");
        refreshLicense();
    }
    else
    {
        TL_LOGE ("licenca", "ativacao recusada: " + info.error);
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

    /*  Maquina nao acompanha: desce de modelo, uma vez.

        O motor so LEVANTA a bandeira; a troca acontece aqui, no timer, que
        roda na thread de mensagens. Trocar de dentro do worker seria mexer no
        estado do processador durante a transcricao.

        Uma vez por sessao (o proprio motor so avisa uma vez). Sem isso, uma
        maquina no limite ficaria pulando entre modelos no meio do show, que e
        pior do que qualquer um dos dois.

        A troca vale mesmo que o cliente tenha escolhido o modelo na mao: num
        palco, texto que chega minutos atrasado nao serve para nada, entao
        manter a preferencia dele seria respeitar a escolha e entregar algo
        inutil. O status e o log dizem claramente o que houve e por que.      */
    if (engine.consumirAvisoDeLentidao())
    {
        const auto leve = escolherModeloMaisLeve (selectedModel);

        if (leve.isNotEmpty())
        {
            TL_LOGW ("modelo", "trocando " + selectedModel + " -> " + leve
                     + " (esta maquina nao acompanha o tempo real)");
            trocaAutomatica = selectedModel + " -> " + leve;
            selectModel (leve);
        }
        else
        {
            // Fim da escada de modelos. Sobra o modo economico: encurta a
            // janela do encoder, corta o reprocessamento e desliga as
            // parciais. Reconhece pior -- mas texto ruim no tempo certo ainda
            // serve num palco; texto bom sete segundos atrasado nao serve.
            TL_LOGW ("modelo", selectedModel + " ja e o modelo mais leve instalado "
                     "-- ligando o modo economico (mais rapido, reconhece pior)");
            engine.ativarModoEconomico();
            trocaAutomatica = juce::String (juce::CharPointer_UTF8 (
                "modo econÃ´mico (esta mÃ¡quina nÃ£o acompanha nem o modelo mais leve)"));
        }
    }
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
    // Threads = nucleos FISICOS, nao logicos.
    //
    // Estava usando getNumCpus()-1, que num Air de 2 nucleos com HyperThreading
    // da 3 threads para 2 nucleos reais. Em carga de matriz densa como a do
    // whisper, HyperThreading nao acrescenta capacidade -- as duas threads
    // irmas disputam a mesma unidade de execucao -- e o excesso ainda compete
    // com a thread de audio do host.
    s.numThreads      = juce::jlimit (2, 8, juce::SystemStats::getNumPhysicalCpus());
    engine.setSettings (s);
}

//==============================================================================
void TranscriberLiveAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    TL_LOGI ("audio", juce::String::formatted (
        "prepareToPlay: %.0f Hz, blocos de %d amostras (%.1f ms), entradas=%d saidas=%d",
        sampleRate, samplesPerBlock, samplesPerBlock * 1000.0 / juce::jmax (1.0, sampleRate),
        getTotalNumInputChannels(), getTotalNumOutputChannels()));

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
    {
        // bloco maior que o anunciado (host mal-comportado): não aloca na
        // thread de áudio. Antes isso era silencioso — e um host assim fazia a
        // transcrição sumir sem explicação nenhuma.
        TL_LOGRT (rtBlocoGrande, numSamples, (int) monoBuffer.size());
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
    TL_TRACE_STEP ("createEditor");
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
