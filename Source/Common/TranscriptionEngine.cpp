#include "TranscriptionEngine.h"
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

    // Silencia o log do whisper/ggml (não polui o console do host)
    whisper_log_set ([] (ggml_log_level, const char*, void*) {}, nullptr);

    startThread (juce::Thread::Priority::normal);
}

TranscriptionEngine::~TranscriptionEngine()
{
    // whisper_full pode demorar; o abort_callback faz ele sair rápido quando a thread é encerrada
    stopThread (15000);

    if (ctx  != nullptr) whisper_free (ctx);
    if (vctx != nullptr) whisper_vad_free (vctx);
}

//==============================================================================
void TranscriptionEngine::loadModel (const juce::File& modelFile)
{
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
    // (se o FIFO encher — worker travado — as amostras extras são descartadas; nunca bloqueia)
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
    if (hasPendingModel.exchange (false))
    {
        const auto file = pendingModel;

        { const juce::ScopedLock sl (statusLock); status = "Carregando modelo..."; }
        modelLoaded.store (false);

        if (ctx != nullptr) { whisper_free (ctx); ctx = nullptr; }

        auto cparams = whisper_context_default_params();
        cparams.use_gpu    = true;       // Metal no macOS; ignorado se indisponível
        cparams.flash_attn = true;

        ctx = whisper_init_from_file_with_params (file.getFullPathName().toRawUTF8(), cparams);

        const juce::ScopedLock sl (statusLock);
        if (ctx != nullptr)
        {
            modelLoaded.store (true);
            status = "Modelo: " + file.getFileName();
        }
        else
        {
            status = "Falha ao carregar " + file.getFileName();
        }
    }

    if (hasPendingVad.exchange (false))
    {
        const auto file = pendingVad;
        vadLoaded.store (false);

        if (vctx != nullptr) { whisper_vad_free (vctx); vctx = nullptr; }

        auto vp = whisper_vad_default_context_params();
        vp.n_threads = 1;
        vp.use_gpu   = false;
        vctx = whisper_vad_init_from_file_with_params (file.getFullPathName().toRawUTF8(), vp);

        vadLoaded.store (vctx != nullptr);

        const juce::ScopedLock sl (statusLock);
        if (vctx == nullptr)
            status = "Falha ao carregar VAD " + file.getFileName();
    }
}

//==============================================================================
void TranscriptionEngine::processPendingAudio()
{
    const int ready = fifo.getNumReady();
    if (ready <= 0)
        return;

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
#if TRANSCRIBER_DEBUG
    const float meanProb    = totalWindows > 0 ? vadProbSum / (float) totalWindows : 0.0f;
    std::fprintf (stderr, "[seg %s len=%.2fs ratio=%.2f meanProb=%.2f]\n", isFinal ? "final" : "parcial", segment.size() / 16000.0, speechRatio, meanProb);
#endif

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
    p.temperature_inc   = isFinal ? 0.2f : 0.0f;   // parcial: sem fallback (mais rápido)
    p.no_speech_thold   = 0.6f;
    p.entropy_thold     = 2.4f;
    p.logprob_thold     = -1.0f;
    p.max_tokens        = 0;
    p.audio_ctx         = 0;

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

    if (whisper_full (ctx, p, padded.data(), (int) padded.size()) == 0)
    {
        const int n = whisper_full_n_segments (ctx);
        for (int i = 0; i < n; ++i)
        {
#if TRANSCRIBER_DEBUG
            std::fprintf (stderr, "[whisper no_speech=%.2f '%s']\n", whisper_full_get_segment_no_speech_prob (ctx, i), whisper_full_get_segment_text (ctx, i));
#endif
            if (whisper_full_get_segment_no_speech_prob (ctx, i) > 0.5f)
                continue;

            result += juce::String::fromUTF8 (whisper_full_get_segment_text (ctx, i));
        }
    }

    transcribing.store (false);

    result = result.trim();

    if (looksLikeHallucination (result))
        return {};

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
