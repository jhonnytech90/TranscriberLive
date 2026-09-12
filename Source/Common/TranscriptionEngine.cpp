#include "TranscriptionEngine.h"
#include "Log.h"
#include "whisper.h"
#include <cmath>
#include <cstdio>

//==============================================================================
TranscriptionEngine::TranscriptionEngine()
    : juce::Thread ("TranscriberLive Worker")
{
    fifoBuffer.resize ((size_t) fifo.getTotalSize(), 0.0f);
    window.resize (kVadWindow, 0.0f);
    preRoll.resize ((size_t) (kSampleRate * settings.preRollMs / 1000), 0.0f);
    segment.reserve ((size_t) (kSampleRate * 15));

    // O whisper/ggml não escreve mais no console do host — vai para o nosso log,
    // como detalhe. É lá que aparece qual backend ele escolheu (Metal, BLAS,
    // CPU) e com quantas threads, que é metade do diagnóstico de lentidão.
    whisper_log_set ([] (ggml_log_level nivel, const char* texto, void*)
    {
        if (texto == nullptr) return;
        auto t = juce::String::fromUTF8 (texto).trimEnd();
        if (t.isEmpty()) return;

        if (nivel == GGML_LOG_LEVEL_ERROR)       TL_LOGE ("whisper", t);
        else if (nivel == GGML_LOG_LEVEL_WARN)   TL_LOGW ("whisper", t);
        else                                     TL_LOGD ("whisper", t);
    }, nullptr);

    // Vitais: uma linha por segundo com o estado real do motor. É daqui que sai
    // a resposta para "por que está atrasando" — o RTF diz, em número, se a CPU
    // dá conta do modelo escolhido (RTF > 1 = não dá, o atraso só cresce).
    vitalsId = tl::Log::get().addVitals ([this] (juce::String& linha)
    {
        const auto atrasoSeg = fifo.getNumReady() / (double) kSampleRate;
        linha << juce::String::formatted (
            "fila=%.2fs rtf=%.2f ultima_transcricao=%dms falas=%d descartadas=%d "
            "transcrevendo=%d fala_ativa=%d vad=%.2f",
            atrasoSeg,
            ultimoRtf.load(),
            ultimoTempoMs.load(),
            totalSegmentos.load(),
            totalDescartados.load(),
            transcribing.load() ? 1 : 0,
            speechActive.load() ? 1 : 0,
            lastVadProb.load());

        if (atrasoSeg > 3.0)
            linha << "  << ATRASADO";
    });

    startThread (juce::Thread::Priority::normal);
}

TranscriptionEngine::~TranscriptionEngine()
{
    tl::Log::get().removeVitals (vitalsId);
    TL_LOGI ("motor", "encerrando o motor");

    // whisper_full pode demorar; o abort_callback faz ele sair rápido quando a thread é encerrada
    stopThread (15000);

    if (ctx  != nullptr) whisper_free (ctx);
    if (vctx != nullptr) whisper_vad_free (vctx);
}

//==============================================================================
void TranscriptionEngine::loadModel (const juce::File& modelFile)
{
    // Modelo novo, medicao nova: desliga o modo economico e volta a avaliar.
    // Sem isto, uma vez rebaixado o plugin continuaria com a qualidade
    // reduzida para sempre, mesmo tendo trocado para um modelo que a maquina
    // aguenta folgado.
    modoEconomico.store (false);
    frasesLentas = 0;
    jaAvisouLento = false;

    pendingModel = modelFile;
    hasPendingModel.store (true);
    notify();
}

void TranscriptionEngine::loadVadModel (const juce::File& vadFile)
{
    pendingVad = vadFile;
    hasPendingVad.store (true);
    notify();
}

void TranscriptionEngine::unloadModels()
{
    hasPendingUnload.store (true);
    notify();
}

juce::String TranscriptionEngine::getStatus() const
{
    const juce::ScopedLock sl (statusLock);
    return status;
}

void TranscriptionEngine::setSettings (const Settings& s)
{
    const juce::ScopedLock sl (settingsLock);
    settings = s;
}

TranscriptionEngine::Settings TranscriptionEngine::getSettings() const
{
    const juce::ScopedLock sl (settingsLock);
    return settings;
}

//==============================================================================
void TranscriptionEngine::pushAudio (const float* samples, int numSamples) noexcept
{
    int start1, size1, start2, size2;
    fifo.prepareToWrite (numSamples, start1, size1, start2, size2);

    if (size1 > 0) juce::FloatVectorOperations::copy (fifoBuffer.data() + start1, samples, size1);
    if (size2 > 0) juce::FloatVectorOperations::copy (fifoBuffer.data() + start2, samples + size1, size2);

    fifo.finishedWrite (size1 + size2);

    // Se o FIFO encheu, o worker não está acompanhando o tempo real e estas
    // amostras se perdem. Registrar isso é o que transforma "está estranho" em
    // um número: aqui é EXATAMENTE onde a transcrição começa a atrasar.
    // TL_LOGRT não aloca, não bloqueia e não toca no disco — pode ficar aqui.
    if (const int perdidas = numSamples - (size1 + size2); perdidas > 0)
        TL_LOGRT (rtFifoCheio, perdidas, 0);
}

//==============================================================================
void TranscriptionEngine::run()
{
    while (! threadShouldExit())
    {
        handleModelRequests();
        processPendingAudio();
        wait (10);
    }
}

void TranscriptionEngine::handleModelRequests()
{
    if (hasPendingUnload.exchange (false))
    {
        TL_LOGI ("modelo", "descarregando (sem licenca ou a pedido)");
        if (ctx  != nullptr) { whisper_free (ctx); ctx = nullptr; }
        if (vctx != nullptr) { whisper_vad_free (vctx); vctx = nullptr; }
        modelLoaded.store (false);
        vadLoaded.store (false);
        const juce::ScopedLock sl (statusLock);
        status = "Nenhum modelo carregado";
    }

    if (hasPendingModel.exchange (false))
    {
        const auto file = pendingModel;

        TL_LOGI ("modelo", "carregando " + file.getFileName()
                 + juce::String::formatted (" (%.0f MB) de ", file.getSize() / 1048576.0)
                 + file.getParentDirectory().getFullPathName());

        { const juce::ScopedLock sl (statusLock); status = "Carregando modelo..."; }
        modelLoaded.store (false);

        if (ctx != nullptr) { whisper_free (ctx); ctx = nullptr; }

        // CPU velha demais: avisar em vez de derrubar o host.
        //
        // No macOS o ggml e compilado COM AVX2/FMA de proposito (desligar
        // custaria quase o dobro da velocidade em todo Mac Intel). O preco e
        // que uma CPU anterior a 2013 nao tem essas instrucoes — e sem esta
        // checagem o resultado e uma instrucao ilegal no meio do ggml, que mata
        // o processo do host inteiro, sem dialogo e sem log. Com ela, o
        // Receiver simplesmente diz o que houve e o audio segue passando.
       #if defined (TL_REQUIRES_AVX2) && ! defined (__aarch64__) && ! defined (__arm64__)
        if (! juce::SystemStats::hasAVX2() || ! juce::SystemStats::hasFMA3())
        {
            TL_LOGE ("modelo", "CPU sem AVX2/FMA (" + juce::SystemStats::getCpuModel()
                     + ") -- modelo NAO carregado de proposito, para nao derrubar o host");
            const juce::ScopedLock sl (statusLock);
            status = juce::String (juce::CharPointer_UTF8 (
                "Esta CPU n\xc3\xa3o tem AVX2/FMA \xe2\x80\x94 necess\xc3\xa1rio nesta vers\xc3\xa3o. "
                "O \xc3\xa1udio passa normal, mas n\xc3\xa3o h\xc3\xa1 transcri\xc3\xa7\xc3\xa3o."));
            return;
        }
       #endif

        auto cparams = whisper_context_default_params();

        // GPU e flash attention SO no Apple Silicon.
        //
        // Num binario universal cada fatia e compilada em separado, entao
        // __aarch64__ aqui significa exatamente "esta fatia e a do Apple
        // Silicon". Em Mac Intel antigo o Metal existe mas e de uma geracao
        // que o ggml nao cobre bem, e o flash attention no backend de CPU
        // dispara GGML_ASSERT -> abort, que mata o host inteiro sem aviso.
        // Perder um pouco de velocidade num Mac de 2013 e melhor do que
        // derrubar o SuperRack no meio do show.
       #if JUCE_MAC && defined (__aarch64__)
        cparams.use_gpu    = true;
        cparams.flash_attn = true;
       #else
        cparams.use_gpu    = false;
        cparams.flash_attn = false;
       #endif

        TL_LOGI ("modelo", juce::String ("backend: GPU=") + (cparams.use_gpu ? "sim" : "nao")
                 + "  flash_attn=" + (cparams.flash_attn ? "sim" : "nao"));

        const auto tIni = juce::Time::getMillisecondCounterHiRes();
        ctx = whisper_init_from_file_with_params (file.getFullPathName().toRawUTF8(), cparams);
        const auto levou = juce::Time::getMillisecondCounterHiRes() - tIni;

        const juce::ScopedLock sl (statusLock);
        if (ctx != nullptr)
        {
            modelLoaded.store (true);
            status = "Modelo: " + file.getFileName();
            TL_LOGI ("modelo", juce::String::formatted ("carregado em %.0f ms: ", levou)
                     + file.getFileName());
        }
        else
        {
            status = "Falha ao carregar " + file.getFileName();
            TL_LOGE ("modelo", "FALHOU ao carregar " + file.getFullPathName()
                     + (file.existsAsFile() ? " (arquivo existe -- modelo corrompido ou sem memoria?)"
                                            : " (o arquivo nao existe)"));
        }
    }

    if (hasPendingVad.exchange (false))
    {
        const auto file = pendingVad;
        vadLoaded.store (false);
        TL_LOGI ("vad", "carregando " + file.getFileName());

        if (vctx != nullptr) { whisper_vad_free (vctx); vctx = nullptr; }

        auto vp = whisper_vad_default_context_params();
        vp.n_threads = 1;
        vp.use_gpu   = false;
        vctx = whisper_vad_init_from_file_with_params (file.getFullPathName().toRawUTF8(), vp);

        vadLoaded.store (vctx != nullptr);

        const juce::ScopedLock sl (statusLock);
        if (vctx == nullptr)
        {
            status = "Falha ao carregar VAD " + file.getFileName();
            TL_LOGW ("vad", "FALHOU: " + file.getFullPathName()
                     + " -- sem VAD o corte de frase usa so o gate de nivel");
        }
        else
        {
            TL_LOGI ("vad", "carregado");
        }
    }
}

//==============================================================================
void TranscriptionEngine::processPendingAudio()
{
    int ready = fifo.getNumReady();
    if (ready <= 0)
        return;

    // ---- atrasou demais: joga o passado fora e pula para o presente --------
    //
    // Ate agora todo audio era sagrado: se a maquina nao acompanhava, a fila
    // enchia e o texto continuava saindo minutos atrasado o show inteiro, sem
    // nunca se recuperar. Num palco isso e PIOR do que nao transcrever: o
    // tecnico le "abaixa meu retorno" enquanto o cantor ja pediu outra coisa.
    //
    // Entao acima do limite descartamos o acumulado e recomecamos do audio de
    // agora. Perde-se frase quando a CPU nao da conta -- mas o que aparece na
    // tela e sempre o presente.
    const int limite = (int) (kSampleRate * kMaxAtrasoSeg);
    if (ready > limite)
    {
        const int descartar = ready - (int) (kSampleRate * kAtrasoAlvoSeg);

        int s1, t1, s2, t2;
        fifo.prepareToRead (descartar, s1, t1, s2, t2);
        fifo.finishedRead (t1 + t2);

        // a continuidade quebrou: comecar frase nova, sem juntar os dois lados
        windowFill = 0;
        segment.clear();
        inSpeech = false;
        speechActive.store (false);
        silenceSamples = 0;
        samplesSinceLastPartial = 0;
        if (vctx != nullptr) whisper_vad_reset_state (vctx);

        totalDescartados.fetch_add (1);
        TL_LOGW ("motor", juce::String::formatted (
            "atraso de %.1f s: descartei %.1f s de audio para voltar ao tempo real "
            "(a maquina nao acompanha este modelo)",
            ready / (double) kSampleRate, (t1 + t2) / (double) kSampleRate));

        ready = fifo.getNumReady();
        if (ready <= 0)
            return;
    }

    int start1, size1, start2, size2;
    fifo.prepareToRead (ready, start1, size1, start2, size2);

    auto consume = [this] (const float* data, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            window[(size_t) windowFill++] = data[i];

            if (windowFill == kVadWindow)
            {
                processWindow (window.data());
                windowFill = 0;
            }
        }
    };

    if (size1 > 0) consume (fifoBuffer.data() + start1, size1);
    if (size2 > 0) consume (fifoBuffer.data() + start2, size2);

    fifo.finishedRead (size1 + size2);
}

void TranscriptionEngine::processWindow (const float* w)
{
    const auto s = getSettings();

    // ---- nível (RMS em dBFS) ------------------------------------------------
    double sum = 0.0;
    for (int i = 0; i < kVadWindow; ++i) sum += (double) w[i] * w[i];
    const float rmsDb = (float) (10.0 * std::log10 (sum / kVadWindow + 1e-12));
    const bool aboveGate = rmsDb > s.gateThresholdDb;

    // ---- VAD Silero (se carregado) -------------------------------------------
    float prob = aboveGate ? 1.0f : 0.0f;
    if (vctx != nullptr)
    {
        if (whisper_vad_detect_speech_no_reset (vctx, w, kVadWindow))
        {
            const int n = whisper_vad_n_probs (vctx);
            if (n > 0) prob = whisper_vad_probs (vctx)[n - 1];
        }
    }
    lastVadProb.store (prob);

    // Fala = acima do gate E (sem VAD, ou VAD acima do limiar)
    const bool isSpeech = aboveGate && (vctx == nullptr || prob >= s.vadThreshold);

    // ---- mantém pré-roll circular -------------------------------------------
    const size_t preRollLen = (size_t) juce::jmax (kVadWindow, kSampleRate * s.preRollMs / 1000);
    if (preRoll.size() != preRollLen) { preRoll.assign (preRollLen, 0.0f); preRollPos = 0; }

    for (int i = 0; i < kVadWindow; ++i)
    {
        preRoll[(size_t) preRollPos] = w[i];
        preRollPos = (preRollPos + 1) % (int) preRollLen;
    }

    // ---- máquina de estados ---------------------------------------------------
    if (! inSpeech)
    {
        if (isSpeech)
        {
            inSpeech = true;
            speechActive.store (true);
            silenceSamples = 0;
            samplesSinceLastPartial = 0;
            speechWindows = totalWindows = windowsAtLastSpeech = 0;
            vadProbSum = 0.0f;
            currentUtteranceId = juce::Uuid().toString();

            // começa o segmento com o pré-roll (pega o ataque da primeira sílaba)
            segment.clear();
            for (size_t i = 0; i < preRollLen; ++i)
                segment.push_back (preRoll[(size_t) ((preRollPos + (int) i) % (int) preRollLen)]);
        }
        return;
    }

    // em fala: acumula sempre (inclusive silêncios curtos entre palavras)
    segment.insert (segment.end(), w, w + kVadWindow);
    samplesSinceLastPartial += kVadWindow;
    ++totalWindows;
    if (isSpeech) { ++speechWindows; windowsAtLastSpeech = totalWindows; }
    vadProbSum += prob;

    silenceSamples = isSpeech ? 0 : silenceSamples + kVadWindow;

    const int holdSamples = kSampleRate * s.holdMs / 1000;
    const int maxSamples  = (int) (kSampleRate * s.maxSegmentSec);

    if (silenceSamples >= holdSamples)
    {
        // fim da frase: remove a cauda de silêncio (deixa ~150 ms)
        const int keepTail = kSampleRate * 150 / 1000;
        const int trim = juce::jmax (0, silenceSamples - keepTail);
        if ((int) segment.size() > trim) segment.resize (segment.size() - (size_t) trim);

        finalizeSegment (true);
    }
    else if ((int) segment.size() >= maxSamples)
    {
        finalizeSegment (true);
    }
    else if (s.showPartials
             && ! modoEconomico.load()   // parcial e uma transcricao EXTRA: numa
                                         // maquina no limite ela atrapalha a frase final
             && samplesSinceLastPartial >= kSampleRate * s.partialIntervalMs / 1000
             && fifo.getNumReady() < kSampleRate / 2)   // só se estamos em dia com o áudio (< 0,5 s de atraso)
    {
        samplesSinceLastPartial = 0;
        finalizeSegment (false);
    }
}

void TranscriptionEngine::finalizeSegment (bool isFinal)
{
    const auto s = getSettings();
    const int minSamples = kSampleRate * s.minSpeechMs / 1000;

    // Rejeita "fala" que na verdade é vazamento/ruído: poucas janelas com voz
    // dentro do segmento, ou probabilidade média do VAD baixa.
    // (a razão ignora a cauda de silêncio do hold: conta só até a última janela com voz)
    const float speechRatio = windowsAtLastSpeech > 0 ? (float) speechWindows / (float) windowsAtLastSpeech : 0.0f;
    const bool  plausible   = speechRatio >= 0.5f;
    const float meanProb    = totalWindows > 0 ? vadProbSum / (float) totalWindows : 0.0f;

    TL_LOGD ("segmento", juce::String::formatted (
        "%s %.2fs razao_fala=%.2f vad_medio=%.2f", isFinal ? "final" : "parcial",
        segment.size() / (double) kSampleRate, speechRatio, meanProb));

    // Por que uma frase não virou texto: sem isto, "ele não transcreveu o que
    // eu falei" fica sem resposta possível.
    if (isFinal && ! (plausible && (int) segment.size() >= minSamples && ctx != nullptr))
    {
        totalDescartados.fetch_add (1);
        TL_LOGI ("segmento", juce::String::formatted ("descartado (%.2fs): ",
                                                      segment.size() / (double) kSampleRate)
                 + (ctx == nullptr                       ? juce::String ("nenhum modelo carregado")
                  : ! plausible                          ? juce::String::formatted ("so %.0f%% do trecho tem voz (ruido/vazamento)", speechRatio * 100.0f)
                                                         : juce::String ("fala curta demais")));
    }

    if (plausible && (int) segment.size() >= minSamples && ctx != nullptr)
    {
        const auto text = transcribe (segment, isFinal);
        if (text.isNotEmpty())
            publishLine (text, isFinal);
        else if (isFinal)
            publishLine ({}, true);   // remove parcial pendente
    }
    else if (isFinal)
    {
        publishLine ({}, true);
    }

    if (isFinal)
    {
        segment.clear();
        inSpeech = false;
        speechActive.store (false);
        silenceSamples = 0;
        if (vctx != nullptr) whisper_vad_reset_state (vctx);
    }
}

//==============================================================================
juce::String TranscriptionEngine::transcribe (const std::vector<float>& audio, bool isFinal)
{
    const auto s = getSettings();
    transcribing.store (true);

    auto p = whisper_full_default_params (WHISPER_SAMPLING_GREEDY);
    p.language          = s.language.toRawUTF8();
    p.translate         = false;
    p.n_threads         = juce::jlimit (1, 16, s.numThreads);
    p.no_context        = true;     // cada frase independente => menos alucinação
    p.single_segment    = true;
    p.no_timestamps     = true;
    p.print_progress    = false;
    p.print_realtime    = false;
    p.print_special     = false;
    p.print_timestamps  = false;
    p.suppress_blank    = true;
    p.suppress_nst      = true;     // suprime tokens não-fala ([Música], etc.)
    p.temperature       = 0.0f;
    // Temperature fallback: o whisper reprocessa a MESMA frase com temperatura
    // crescente quando o resultado nao passa nos limiares de entropia/logprob.
    // Isso melhora o texto em audio difícil, e custa caro: no log do Air, as
    // frases finais saíram ~50% mais lentas que as parciais de mesma duração
    // (RTF 6,21 contra 4,08) — e essa é a única diferença entre elas.
    //
    // Numa máquina que já não acompanha, reprocessar é o oposto do que se quer:
    // ela gasta 50% mais tempo para melhorar um texto que vai chegar tarde de
    // qualquer jeito. Então o fallback só existe fora do modo econômico.
    p.temperature_inc   = (isFinal && ! modoEconomico.load()) ? 0.2f : 0.0f;
    p.no_speech_thold   = 0.6f;
    p.entropy_thold     = 2.4f;
    p.logprob_thold     = -1.0f;
    p.max_tokens        = 0;

    // ---- audio_ctx: SO no modo economico ------------------------------------
    //
    // O encoder do whisper trabalha numa janela de 1500 quadros (30 s), e foi
    // TREINADO assim. Encurtar essa janela deixa a conta mais leve e o
    // reconhecimento pior -- e uma troca, nao uma otimizacao.
    //
    // Eu ja errei aqui: apliquei o corte sempre, para compensar uma lentidao
    // que na verdade vinha do ggml compilado em modo generico. Com a causa
    // raiz corrigida, o corte deixou de ser necessario e so custava qualidade.
    // Agora ele e ULTIMO RECURSO: entra quando a maquina nao acompanha nem
    // com o modelo mais leve disponivel.
    if (modoEconomico.load())
    {
        const double duracao = juce::jmax (1.0, audio.size() / (double) kSampleRate);
        const int    quadros = (int) std::ceil (duracao / 30.0 * 1500.0) + 256;
        p.audio_ctx = juce::jlimit (768, 1500, quadros);   // piso alto: 256 estragava o texto
    }
    else
    {
        p.audio_ctx = 0;   // janela inteira: melhor reconhecimento
    }

    // permite abortar a decodificação se o plugin for descarregado no meio
    p.abort_callback = [] (void* user) -> bool { return static_cast<juce::Thread*> (user)->threadShouldExit(); };
    p.abort_callback_user_data = static_cast<juce::Thread*> (this);

    // contexto ajuda o modelo a entender o domínio
    static const char* prompt = "Comunicação de palco entre cantor e técnico de som durante o show.";
    p.initial_prompt = prompt;

    // whisper exige >= 1 s de áudio: preenche com silêncio se preciso
    std::vector<float> padded (audio);
    if (padded.size() < (size_t) kSampleRate * 1.1)
        padded.resize ((size_t) (kSampleRate * 1.1), 0.0f);

    juce::String result;

    const auto tIni = juce::Time::getMillisecondCounterHiRes();
    const int  rc   = whisper_full (ctx, p, padded.data(), (int) padded.size());
    const auto levouMs = juce::Time::getMillisecondCounterHiRes() - tIni;

    if (rc == 0)
    {
        const int n = whisper_full_n_segments (ctx);
        for (int i = 0; i < n; ++i)
        {
            const float noSpeech = whisper_full_get_segment_no_speech_prob (ctx, i);
            TL_LOGD ("whisper", juce::String::formatted ("segmento no_speech=%.2f: ", noSpeech)
                     + juce::String::fromUTF8 (whisper_full_get_segment_text (ctx, i)).trim());

            if (noSpeech > 0.5f)
                continue;

            result += juce::String::fromUTF8 (whisper_full_get_segment_text (ctx, i));
        }
    }
    else
    {
        TL_LOGW ("whisper", juce::String::formatted ("whisper_full devolveu %d (abortado ou erro)", rc));
    }

    transcribing.store (false);

    // ---- RTF: a medida que decide se a maquina aguenta -----------------------
    // RTF = tempo gasto / duracao do audio. Abaixo de 1 sobra folga; acima de 1
    // cada frase custa mais que o proprio tempo dela e o atraso so acumula.
    const double duracaoSeg = padded.size() / (double) kSampleRate;
    const double rtf = levouMs / 1000.0 / juce::jmax (0.001, duracaoSeg);

    ultimoTempoMs.store ((int) levouMs);
    ultimoRtf.store ((float) rtf);
    if (isFinal)
        totalSegmentos.fetch_add (1);

    const auto resumo = juce::String::formatted (
        "%s %.1fs de audio em %.0f ms (RTF %.2f, %d threads)",
        isFinal ? "final:" : "parcial:", duracaoSeg, levouMs, rtf, p.n_threads);

    if (rtf > 1.0 && isFinal)
        TL_LOGW ("whisper", resumo + " -- MAIS LENTO QUE O TEMPO REAL: o atraso vai crescer");
    else
        TL_LOGI ("whisper", resumo);

    // ---- a maquina da conta deste modelo? ----------------------------------
    // So conta frase final: as parciais sao curtas e dao leitura otimista.
    if (isFinal)
    {
        if (rtf > kRtfLimite) ++frasesLentas;
        else                  frasesLentas = 0;

        // Duas velocidades de reacao, de proposito.
        //
        // RTF pouco acima de 1 e caso de duvida: pode ser um pico do host, e
        // ai vale esperar tres frases antes de rebaixar. RTF acima de 3 nao e
        // duvida nenhuma -- a maquina esta 3x atras do tempo real e nao ha
        // cenario em que a proxima frase salve. Esperar tres frases nesse caso
        // significa 20 segundos de texto inutil na tela antes de reagir.
        const bool semEsperanca = rtf > 3.0f;
        const int  necessarias  = semEsperanca ? 1 : kFrasesLentasSeguidas;

        if (frasesLentas >= necessarias && ! jaAvisouLento)
        {
            jaAvisouLento = true;          // uma vez por sessao, sem ficar alternando
            maquinaLenta.store (true);
            TL_LOGW ("motor", juce::String::formatted (
                "%d frase(s) acima do tempo real (ultimo RTF %.2f)%s -- esta maquina "
                "nao da conta deste modelo", frasesLentas, rtf,
                semEsperanca ? ", e nao e por pouco" : ""));
        }
    }

    result = result.trim();

    if (looksLikeHallucination (result))
    {
        TL_LOGD ("whisper", "descartado como alucinacao: " + result);
        return {};
    }

    return result;
}

bool TranscriptionEngine::looksLikeHallucination (const juce::String& text)
{
    if (text.isEmpty())
        return true;

    // só pontuação/símbolos
    if (! text.containsAnyOf (juce::String (juce::CharPointer_UTF8 ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ\xc3\xa1\xc3\xa9\xc3\xad\xc3\xb3\xc3\xba\xc3\xa2\xc3\xaa\xc3\xb4\xc3\xa3\xc3\xb5\xc3\xa7\xc3\x81\xc3\x89\xc3\x8d\xc3\x93\xc3\x9a\xc3\x82\xc3\x8a\xc3\x94\xc3\x83\xc3\x95\xc3\x87" "0123456789"))))
        return true;

    // frases clássicas que o Whisper inventa em silêncio/ruído (pt/en)
    static const char* junk[] = {
        "legendas pela comunidade", "amara.org", "legendado por", "transcri\xc3\xa7\xc3\xa3o por",
        "obrigado por assistir", "inscreva-se", "inscrevam-se", "curta o v\xc3\xad" "deo", "deixe o like",
        "tchau, tchau", "at\xc3\xa9 o pr\xc3\xb3ximo v\xc3\xad" "deo", "m\xc3\xbasica", "[m\xc3\xbasica]", "(m\xc3\xbasica)",
        "subtitles by", "thanks for watching", "thank you for watching", "[music]", "(music)",
        "www.", "http", ".com", "\xe2\x99\xaa"
    };

    const auto lower = text.toLowerCase();
    for (auto* j : junk)
        if (lower.contains (juce::String (juce::CharPointer_UTF8 (j))))
            return true;

    // repetição patológica ("sim sim sim..." ou "o que é o que é o que é...")
    juce::StringArray words;
    words.addTokens (lower.retainCharacters (juce::String (juce::CharPointer_UTF8 ("abcdefghijklmnopqrstuvwxyz\xc3\xa1\xc3\xa9\xc3\xad\xc3\xb3\xc3\xba\xc3\xa2\xc3\xaa\xc3\xb4\xc3\xa3\xc3\xb5\xc3\xa7" "0123456789 "))), " ", "");
    words.removeEmptyStrings();

    for (int n = 1; n <= 4; ++n)
    {
        if (words.size() < n * 3) continue;

        int repeats = 0;
        for (int i = n; i + n <= words.size(); i += n)
        {
            bool same = true;
            for (int k = 0; k < n; ++k)
                if (words[i + k] != words[i - n + k]) { same = false; break; }

            repeats = same ? repeats + 1 : 0;
            if (repeats >= 2)          // o mesmo n-grama 3x seguidas
                return true;
        }
    }

    return false;
}

//==============================================================================
void TranscriptionEngine::publishLine (const juce::String& text, bool isFinal)
{
    Line line { text, juce::Time::getCurrentTime(), isFinal, currentUtteranceId };
    bool notify = true;

    {
        const juce::ScopedLock sl (linesLock);

        // remove a parcial anterior, se existir
        if (! lines.empty() && ! lines.back().isFinal)
            lines.pop_back();

        if (text.isNotEmpty())
        {
            // evita duplicar exatamente a última frase final
            if (isFinal && text == lastFinalText)
                notify = false;
            else
            {
                lines.push_back (line);
                if (isFinal) lastFinalText = text;

                // mantém histórico limitado
                while (lines.size() > 200)
                    lines.erase (lines.begin());
            }
        }

        linesVersion.fetch_add (1);
    }

    if (notify && onLine)
        onLine (line);
}

std::vector<TranscriptionEngine::Line> TranscriptionEngine::getLines() const
{
    const juce::ScopedLock sl (linesLock);
    return lines;
}

void TranscriptionEngine::clearLines()
{
    const juce::ScopedLock sl (linesLock);
    lines.clear();
    lastFinalText.clear();
    linesVersion.fetch_add (1);
}
