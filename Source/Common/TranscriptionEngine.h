#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>
#include <functional>

struct whisper_context;
struct whisper_vad_context;

//==============================================================================
/**
    Motor de transcrição em tempo real.

    Recebe áudio mono a 16 kHz (já reamostrado pelo processador), detecta fala
    (gate por nível em dB + VAD Silero via whisper.cpp), acumula segmentos de
    fala e roda o Whisper em uma thread separada. O áudio do host nunca é
    bloqueado: a thread de áudio só escreve num FIFO lock-free.
*/
class TranscriptionEngine : private juce::Thread
{
public:
    static constexpr int kSampleRate = 16000;
    static constexpr int kVadWindow  = 512;   // Silero VAD: 512 amostras @16k = 32 ms

    struct Line
    {
        juce::String text;
        juce::Time   time;
        bool         isFinal = true;   // false = parcial (ainda falando)
        juce::String utteranceId;      // mesma fala: parciais + final compartilham o id
    };

    /** Chamado na thread do worker a cada linha publicada (parcial ou final).
        Texto vazio + isFinal = a parcial anterior foi descartada. */
    std::function<void (const Line&)> onLine;

    struct Settings
    {
        juce::String language     = "pt";
        float  gateThresholdDb    = -45.0f;  // nível mínimo p/ considerar fala
        float  vadThreshold       = 0.55f;   // prob. Silero p/ considerar fala
        int    holdMs             = 700;     // silêncio p/ encerrar a frase
        int    preRollMs          = 250;     // áudio antes do início da fala
        int    minSpeechMs        = 350;     // fala mais curta que isso é descartada
        float  maxSegmentSec      = 10.0f;   // corta frases muito longas
        int    partialIntervalMs  = 1500;    // atualiza texto parcial enquanto fala
        bool   showPartials       = true;
        int    numThreads         = 4;
    };

    TranscriptionEngine();
    ~TranscriptionEngine() override;

    //-- Modelos ---------------------------------------------------------------
    /** Pede o carregamento do modelo Whisper (ggml-*.bin). Assíncrono. */
    void loadModel (const juce::File& modelFile);
    /** Pede o carregamento do modelo Silero VAD (ggml-silero-v5.1.2.bin). Opcional. */
    void loadVadModel (const juce::File& vadFile);

    /** Descarrega tudo (usado quando não há licença válida). */
    void unloadModels();

    bool isModelLoaded() const noexcept   { return modelLoaded.load(); }
    bool isVadLoaded()   const noexcept   { return vadLoaded.load(); }
    juce::String getStatus() const;

    //-- Configuração (thread-safe) --------------------------------------------
    void setSettings (const Settings& s);
    Settings getSettings() const;

    //-- Áudio (chamado da thread de áudio) ------------------------------------
    /** Empurra amostras mono @16 kHz. Nunca bloqueia. */
    void pushAudio (const float* samples, int numSamples) noexcept;

    //-- Estado para a UI --------------------------------------------------------
    bool  isSpeechActive() const noexcept { return speechActive.load(); }
    float getLastVadProb() const noexcept { return lastVadProb.load(); }
    bool  isTranscribing() const noexcept { return transcribing.load(); }
    int   getPendingSamples() const noexcept { return fifo.getNumReady(); }   // atraso do worker

    /** Cópia das linhas atuais (a UI chama com timer). */
    std::vector<Line> getLines() const;
    int  getLinesVersion() const noexcept { return linesVersion.load(); }
    void clearLines();

private:
    void run() override;

    void handleModelRequests();
    void processPendingAudio();
    void processWindow (const float* window);   // kVadWindow amostras
    void finalizeSegment (bool isFinal);
    juce::String transcribe (const std::vector<float>& audio, bool isFinal);
    static bool looksLikeHallucination (const juce::String& text);
    void publishLine (const juce::String& text, bool isFinal);

    // Whisper
    whisper_context*     ctx  = nullptr;
    whisper_vad_context* vctx = nullptr;
    std::atomic<bool> modelLoaded { false }, vadLoaded { false };
    juce::File pendingModel, pendingVad;
    std::atomic<bool> hasPendingModel { false }, hasPendingVad { false }, hasPendingUnload { false };
    mutable juce::CriticalSection statusLock;
    juce::String status { "Nenhum modelo carregado" };

    // FIFO thread de áudio -> worker
    juce::AbstractFifo fifo { kSampleRate * 30 };   // 30 s de margem
    std::vector<float> fifoBuffer;

    // Estado do detector de fala
    std::vector<float> window;         // acumula até kVadWindow
    int                windowFill = 0;
    std::vector<float> preRoll;        // buffer circular do pré-roll
    int                preRollPos = 0;
    std::vector<float> segment;        // fala acumulada
    bool               inSpeech = false;
    int                silenceSamples = 0;
    int                samplesSinceLastPartial = 0;
    int                speechWindows = 0, totalWindows = 0, windowsAtLastSpeech = 0;
    float              vadProbSum = 0.0f;
    std::atomic<bool>  speechActive { false };
    std::atomic<float> lastVadProb { 0.0f };
    std::atomic<bool>  transcribing { false };
    juce::String       lastFinalText;
    juce::String       currentUtteranceId;

    // Config
    mutable juce::CriticalSection settingsLock;
    Settings settings;

    // Saída
    mutable juce::CriticalSection linesLock;
    std::vector<Line> lines;
    std::atomic<int>  linesVersion { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranscriptionEngine)
};
