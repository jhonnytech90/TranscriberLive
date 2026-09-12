#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <deque>
#include <functional>
#include <map>

/*
    ============================================================================
    Log continuo do Transcriber Live  ("logcat")
    ============================================================================

    Grava tudo o que a ferramenta faz, do inicio ao fim da sessao, num arquivo
    de texto por processo:

        Windows : %APPDATA%\TranscriberLive\logs\
        macOS   : ~/Library/Application Support/TranscriberLive/logs/

    Um arquivo por processo (REAPER, Cubase, o app do Display...), porque dois
    processos escrevendo no mesmo arquivo embaralham as linhas. O nome carrega
    data, programa e PID:

        2026-09-12_20-31-07_REAPER_48210.log

    --------------------------------------------------------------------------
    Regra que nao pode ser quebrada: NADA disto roda na thread de audio.
    --------------------------------------------------------------------------

    Escrever em arquivo, alocar memoria ou pegar um lock na thread de audio
    produz estouro de buffer (aquele "tec" no PA) -- justamente no meio do show.
    Por isso existem dois caminhos:

      TL_LOG*(...)      threads normais (UI, worker, rede). Enfileira a linha;
                        uma thread propria escreve no disco a cada 200 ms.

      TL_LOGRT(...)     thread de audio. So empurra numeros num anel de tamanho
                        fixo -- sem alocar, sem lock, sem tocar no disco. Quem
                        transforma em texto e a thread escritora.

    Alem dos eventos, a cada segundo sai uma linha VITAIS com o estado da
    maquina (atraso da fila, tempo de transcricao, RTF, blocos perdidos). E
    dela que sai o diagnostico de "esta atrasando": o RTF diz, em numero, se a
    CPU do cliente da conta do modelo escolhido.
*/

namespace tl
{
    enum class LogLevel { erro = 0, aviso = 1, info = 2, debug = 3 };

    class Log : private juce::Thread
    {
    public:
        static Log& get();

        //-- caminho normal (qualquer thread MENOS a de audio) -------------------
        void write (LogLevel level, const char* categoria, const juce::String& mensagem);

        //-- caminho da thread de audio: sem alocacao, sem lock, sem disco -------
        enum RtCode : juce::uint8
        {
            rtFifoCheio = 1,    // a=amostras perdidas
            rtBlocoGrande,      // a=amostras do bloco, b=capacidade
            rtPrepare,          // a=sampleRate, b=tamanho do bloco
            rtSemLicenca,
            rtCanaisMudaram,    // a=entradas, b=saidas
            rtSilencioLongo     // a=segundos
        };
        void rt (RtCode code, int a = 0, int b = 0) noexcept;

        //-- vitais: amostrados 1x por segundo pela thread escritora -------------
        /** A funcao e chamada FORA da thread de audio; deve so ler atomics.
            Devolve um id para cancelar no destrutor de quem registrou. */
        using VitalsFn = std::function<void (juce::String&)>;
        int  addVitals (VitalsFn fn);
        void removeVitals (int id);

        //-- consulta -----------------------------------------------------------
        juce::File   getFolder() const;
        juce::File   getFile() const;
        /** Ultimas linhas do arquivo desta sessao (para o botao de diagnostico). */
        juce::String tail (int numLinhas = 400);
        /** Cabecalho + ultimas linhas, pronto para colar num e-mail de suporte. */
        juce::String diagnostico();

        void flushNow();
        void setVerbose (bool v) noexcept       { verbose.store (v); }
        bool isVerbose() const noexcept         { return verbose.load(); }
        bool isEnabled() const noexcept         { return habilitado; }

        /** Uma linha por etapa de inicializacao, com gravacao imediata: se o
            processo morrer no meio, o arquivo termina no "-> etapa". */
        struct Step
        {
            juce::String nome;
            double       t0;
            explicit Step (const juce::String& n);
            ~Step();
        };

    private:
        Log();
        ~Log() override;
        void run() override;

        void abrirArquivo();
        void rolarSeNecessario();
        void escreverCabecalho();
        void limparAntigos();
        void drenarFila();
        void drenarRt();
        void emitirVitais();

        //-- fila de texto (threads normais) ------------------------------------
        juce::CriticalSection        filaLock;
        std::deque<juce::String>     fila;
        std::atomic<int>             descartadas { 0 };

        //-- anel da thread de audio (sem lock) ---------------------------------
        struct RtEvento { double t; int a; int b; juce::uint8 code; };
        static constexpr int kRtCap = 512;
        RtEvento                  anel[kRtCap] {};
        std::atomic<juce::uint64> rtEscrita { 0 };
        juce::uint64              rtLeitura = 0;
        double                    totalPerdidoSeg = 0.0;   // audio descartado na sessao

        //-- vitais --------------------------------------------------------------
        juce::CriticalSection      vitaisLock;
        std::map<int, VitalsFn>    vitais;
        int                        proximoVitalId = 1;

        //-- arquivo -------------------------------------------------------------
        // Trava do arquivo e do stream.
        //
        // Sem ela havia um crash real: o botao "Log" da interface chamava
        // flushNow() e lia o arquivo na thread da UI enquanto a thread do log
        // escrevia no mesmo stream. Duas threads no mesmo FileOutputStream
        // derrubam o host inteiro -- foi o que aconteceu num Mac Intel.
        //
        // Ordem de travas, para nao travar o programa: quem pega esta PODE
        // pegar filaLock depois; o contrario nunca acontece.
        mutable juce::CriticalSection              saidaLock;
        juce::File                                 arquivo;
        std::unique_ptr<juce::FileOutputStream>    saida;
        juce::int64                                bytesEscritos = 0;
        bool                                       habilitado = true;
        std::atomic<bool>                          verbose { false };
        double                                     ultimoVitais = 0.0;
        double                                     t0 = 0.0;

        JUCE_DECLARE_NON_COPYABLE (Log)
    };
}

//==============================================================================
#define TL_LOGE(cat, msg)  ::tl::Log::get().write (::tl::LogLevel::erro,  cat, msg)
#define TL_LOGW(cat, msg)  ::tl::Log::get().write (::tl::LogLevel::aviso, cat, msg)
#define TL_LOGI(cat, msg)  ::tl::Log::get().write (::tl::LogLevel::info,  cat, msg)

/** Detalhe: so sai quando o modo detalhado esta ligado (arquivo "log-detalhado"
    na pasta de dados). Assim o log normal fica legivel e o detalhado existe
    quando o cliente precisa caçar um bug especifico. */
#define TL_LOGD(cat, msg)                                                    \
    do { if (::tl::Log::get().isVerbose())                                   \
             ::tl::Log::get().write (::tl::LogLevel::debug, cat, msg); } while (false)

/** Thread de audio. Nunca aloca, nunca bloqueia. */
#define TL_LOGRT(code, a, b)  ::tl::Log::get().rt (::tl::Log::code, a, b)

#define TL_LOG_CAT2(a, b)  a ## b
#define TL_LOG_CAT(a, b)   TL_LOG_CAT2 (a, b)
#define TL_LOG_STEP(nome)  ::tl::Log::Step TL_LOG_CAT (tlStep_, __LINE__) { nome }
