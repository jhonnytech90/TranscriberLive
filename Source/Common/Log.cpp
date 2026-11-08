#include "Log.h"

namespace tl
{
    //==========================================================================
    static juce::File dataDir()
    {
        // NAO usa Hub::getDataDir() de proposito: o log precisa funcionar mesmo
        // que o problema esteja no proprio Hub.
        auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
       #if JUCE_MAC
        base = base.getChildFile ("Application Support");
       #endif
        return base.getChildFile ("TranscriberLive");
    }

    static juce::String nomeDoProcesso()
    {
        auto n = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                     .getFileNameWithoutExtension();
        if (n.isEmpty()) n = "processo";
        return n.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_");
    }

    static const char* nomeDoNivel (LogLevel l)
    {
        switch (l)
        {
            case LogLevel::erro:  return "ERRO ";
            case LogLevel::aviso: return "AVISO";
            case LogLevel::info:  return "INFO ";
            case LogLevel::debug: return "DEBUG";
        }
        return "INFO ";
    }

    //==========================================================================
    Log& Log::get()
    {
        static Log instancia;
        return instancia;
    }

    Log::Log() : juce::Thread ("TranscriberLive Log")
    {
        t0 = juce::Time::getMillisecondCounterHiRes();

        // desligar: crie um arquivo vazio "sem-log" na pasta de dados
        habilitado = ! dataDir().getChildFile ("sem-log").existsAsFile();
        verbose.store (dataDir().getChildFile ("log-detalhado").existsAsFile());

        if (! habilitado)
            return;

        {
            const juce::ScopedLock sl (saidaLock);
            abrirArquivo();
            limparAntigos();
            escreverCabecalho();
        }

        // prioridade baixa: o log nunca disputa CPU com o audio nem com o whisper
        startThread (juce::Thread::Priority::background);
    }

    Log::~Log()
    {
        if (habilitado)
        {
            write (LogLevel::info, "sessao", "=== fim da sessao ===");
            signalThreadShouldExit();
            notify();
            stopThread (2000);
        }

        const juce::ScopedLock sl (saidaLock);

        // drenarRt ANTES de drenarFila: o rt vira texto na fila, entao drenar
        // na ordem inversa (como estava) perderia os ultimos eventos de audio
        // -- justamente os do momento em que algo deu errado.
        drenarRt();
        drenarFila();

        if (saida != nullptr)
            saida->flush();
        saida.reset();
    }

    //==========================================================================
    // Chamada SEMPRE com saidaLock ja segurado.
    void Log::abrirArquivo()
    {
        auto pasta = getFolder();
        pasta.createDirectory();

        /*  Um arquivo FIXO por programa, que vai sendo acrescentado.

            Antes era um arquivo por SESSAO, e o resultado media 6,7 MB: numa
            sessao de 11 horas, 35.373 linhas de [vitais] -- 99% do arquivo --
            e ainda assim so a sessao mais recente, enquanto o problema que o
            suporte precisa ver costuma estar na ANTERIOR.

            Agora o historico de varias sessoes cabe num arquivo de alguns KB,
            que sobe para o site num piscar. O teto em aparar() garante isso.

            Continua um por programa (Receiver, Display, app) e nao um unico:
            os tres rodam juntos na mesma maquina, e tres processos escrevendo
            na mesma linha do mesmo arquivo embaralham o texto. Evitar isso com
            trava entre processos custaria mais do que vale.                 */
        arquivo = pasta.getChildFile (nomeDoProcesso() + ".log");
        aparar (arquivo);

        // O FileOutputStream do JUCE ja abre posicionado no FIM do arquivo:
        // escrever aqui acrescenta, nao sobrescreve. E o que queremos.
        saida = std::make_unique<juce::FileOutputStream> (arquivo);
        if (saida->failedToOpen())
        {
            saida.reset();
            habilitado = false;   // sem pasta gravavel: o plugin roda igual, so nao loga
            return;
        }
        bytesEscritos = arquivo.getSize();
    }

    /*  Teto do arquivo: passou do limite, as linhas mais velhas saem.

        kTetoLog e o tamanho que queremos que o arquivo TENHA depois de aparar;
        deixamos ele crescer ate o dobro antes de mexer, para nao reescrever o
        arquivo a cada poucas linhas.

        Cortamos na quebra de linha seguinte, nunca no meio de uma linha: log
        pela metade na primeira linha confunde quem le.                      */
    static constexpr juce::int64 kTetoLog = 256 * 1024;

    void Log::aparar (const juce::File& f)
    {
        const auto tamanho = f.getSize();
        if (tamanho <= kTetoLog * 2)
            return;

        juce::MemoryBlock bloco;
        if (! f.loadFileAsData (bloco))
            return;

        auto* dados = static_cast<const char*> (bloco.getData());
        auto  corte = (size_t) (bloco.getSize() - (size_t) kTetoLog);

        while (corte < bloco.getSize() && dados[corte] != '\n')
            ++corte;
        if (corte < bloco.getSize()) ++corte;   // pula o proprio \n

        juce::FileOutputStream novo (f);
        if (novo.failedToOpen())
            return;

        novo.setPosition (0);
        novo.truncate();
        novo.writeText (juce::String ("--- inicio cortado: o arquivo passou de ")
                            + juce::String (kTetoLog / 1024) + " KB ---" + juce::newLine,
                        false, false, nullptr);
        novo.write (dados + corte, bloco.getSize() - corte);
        novo.flush();
    }

    // Chamada SEMPRE com saidaLock ja segurado.
    void Log::rolarSeNecessario()
    {
        if (saida == nullptr || bytesEscritos < kTetoLog * 2)
            return;

        saida->flush();
        saida.reset();
        aparar (arquivo);

        saida = std::make_unique<juce::FileOutputStream> (arquivo);
        if (saida->failedToOpen()) { saida.reset(); return; }
        bytesEscritos = arquivo.getSize();
    }

    /*  Varre os logs do formato antigo (um por sessao, com data no nome).

        Quem atualiza de uma versao anterior tem dezenas deles na pasta, alguns
        de varios MB. Nao servem mais para nada: o historico agora esta no
        arquivo unico. Some com eles para a pasta nao ficar com 200 MB de lixo
        que o cliente nunca vai olhar.                                       */
    void Log::limparAntigos()
    {
        auto pasta = getFolder();
        if (! pasta.isDirectory()) return;

        for (auto& f : pasta.findChildFiles (juce::File::findFiles, false, "*.log"))
        {
            // O formato novo e "<Programa>.log"; o antigo comecava com a data.
            if (f == arquivo) continue;
            if (f.getFileNameWithoutExtension().containsChar ('_')
                && juce::CharacterFunctions::isDigit (f.getFileName()[0]))
                f.deleteFile();
        }
    }

    // Chamada SEMPRE com saidaLock ja segurado.
    void Log::escreverCabecalho()
    {
        using SS = juce::SystemStats;

        juce::StringArray h;
        h.add ("================================================================");
        h.add ("Transcriber Live -- log de sessao");
        h.add ("inicio    : " + juce::Time::getCurrentTime().toString (true, true, true, true));
        h.add ("processo  : " + juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName());
       #ifdef JucePlugin_Name
        h.add ("plugin    : " JucePlugin_Name);
       #endif
       #ifdef JucePlugin_VersionString
        h.add ("versao    : " JucePlugin_VersionString);
       #endif
        h.add ("SO        : " + SS::getOperatingSystemName() + "  (" + juce::String (SS::isOperatingSystem64Bit() ? "64" : "32") + " bits)");
        h.add ("maquina   : " + SS::getComputerName() + "  |  usuario: " + SS::getLogonName());
        h.add ("CPU       : " + SS::getCpuModel() + "  |  " + SS::getCpuVendor());
        h.add ("nucleos   : " + juce::String (SS::getNumCpus())
               + " (fisicos: " + juce::String (SS::getNumPhysicalCpus()) + ")"
               + "  |  RAM: " + juce::String (SS::getMemorySizeInMegabytes()) + " MB");
        h.add (juce::String ("SIMD      : SSE4.2=") + (SS::hasSSE42() ? "sim" : "NAO")
               + "  AVX=" + (SS::hasAVX()  ? "sim" : "NAO")
               + "  AVX2=" + (SS::hasAVX2() ? "sim" : "NAO")
               + "  FMA=" + (SS::hasFMA3() ? "sim" : "NAO")
               + "  NEON=" + (SS::hasNeon() ? "sim" : "NAO"));
       #if defined (TL_REQUIRES_AVX2)
        h.add ("build     : compilado COM AVX2/FMA (exige CPU 2013+)");
       #else
        h.add ("build     : portatil (sem AVX2/FMA) -- mais compativel, mais lento");
       #endif
       #if JUCE_DEBUG
        h.add ("            DEBUG");
       #endif
        h.add ("pasta     : " + dataDir().getFullPathName());
        h.add ("detalhado : " + juce::String (verbose.load() ? "ligado" : "desligado")
               + "  (crie o arquivo \"log-detalhado\" na pasta para ligar)");
        h.add ("================================================================");

        if (saida != nullptr)
        {
            const auto texto = h.joinIntoString (juce::newLine) + juce::newLine;
            saida->writeText (texto, false, false, nullptr);
            bytesEscritos += texto.getNumBytesAsUTF8();
            saida->flush();
        }
    }

    juce::File Log::getFolder() const
    {
        return dataDir().getChildFile ("logs");
    }

    juce::File Log::getFile() const
    {
        // a thread do log troca `arquivo` quando rotaciona: ler sem trava
        // devolveria um caminho pela metade
        const juce::ScopedLock sl (saidaLock);
        return arquivo;
    }

    //==========================================================================
    void Log::write (LogLevel level, const char* categoria, const juce::String& mensagem)
    {
        if (! habilitado)
            return;

        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;

        auto linha = juce::String::formatted ("%8.3f  %s [%-9s] ", ms / 1000.0,
                                              nomeDoNivel (level), categoria)
                   + mensagem;

        {
            const juce::ScopedLock sl (filaLock);

            // Teto de memoria: se o disco travar ou a thread nao acompanhar, o
            // log perde linhas -- nunca cresce sem limite dentro do host.
            //
            // Com PRIORIDADE, e isto importa: o detalhe (DEBUG) para de entrar
            // bem antes do teto, deixando espaco reservado para erro e aviso.
            // Sem isso, ligar o modo detalhado afogava o arquivo em ruido e
            // jogava fora exatamente a linha que explicaria o problema -- foi o
            // que aconteceu no teste de carga.
            const size_t teto = (level == LogLevel::debug) ? 5000 : 8000;

            if (fila.size() >= teto)
            {
                descartadas.fetch_add (1);
                return;
            }
            fila.push_back (std::move (linha));
        }

        // erro nao espera os 200 ms: se o processo cair em seguida, a causa
        // precisa estar no arquivo
        if (level == LogLevel::erro)
            notify();
    }

    //==========================================================================
    void Log::rt (RtCode code, int a, int b) noexcept
    {
        if (! habilitado)
            return;

        // Sem lock, sem alocacao, sem I/O: so um incremento atomico e uma
        // escrita numa posicao fixa. Se o anel der a volta antes da thread
        // escritora passar, o evento mais velho se perde -- aceitavel para log,
        // inaceitavel seria travar a thread de audio.
        const auto idx = rtEscrita.fetch_add (1, std::memory_order_relaxed);
        auto& e = anel[idx % (juce::uint64) kRtCap];
        e.t    = juce::Time::getMillisecondCounterHiRes();
        e.a    = a;
        e.b    = b;
        e.code = (juce::uint8) code;
    }

    //==========================================================================
    int Log::addVitals (VitalsFn fn)
    {
        const juce::ScopedLock sl (vitaisLock);
        const int id = proximoVitalId++;
        vitais[id] = std::move (fn);
        return id;
    }

    void Log::removeVitals (int id)
    {
        const juce::ScopedLock sl (vitaisLock);
        vitais.erase (id);
    }

    //==========================================================================
    void Log::run()
    {
        while (! threadShouldExit())
        {
            // drenarRt e emitirVitais so enfileiram texto (filaLock); quem
            // encosta no arquivo e o bloco abaixo.
            drenarRt();

            const auto agora = juce::Time::getMillisecondCounterHiRes();
            if (agora - ultimoVitais >= 1000.0)
            {
                ultimoVitais = agora;
                emitirVitais();
            }

            {
                const juce::ScopedLock sl (saidaLock);
                drenarFila();
                if (saida != nullptr)
                    saida->flush();
                rolarSeNecessario();
            }

            wait (200);
        }
    }

    // Chamada SEMPRE com saidaLock ja segurado.
    void Log::drenarFila()
    {
        if (saida == nullptr)
            return;

        for (;;)
        {
            juce::String linha;
            {
                const juce::ScopedLock sl (filaLock);
                if (fila.empty())
                    break;
                linha = std::move (fila.front());
                fila.pop_front();
            }
            linha += juce::newLine;
            saida->writeText (linha, false, false, nullptr);
            bytesEscritos += linha.getNumBytesAsUTF8();
        }

        if (const int n = descartadas.exchange (0); n > 0)
        {
            const auto aviso = juce::String::formatted (
                "          AVISO [log      ] %d linhas descartadas (fila cheia)", n) + juce::newLine;
            saida->writeText (aviso, false, false, nullptr);
            bytesEscritos += aviso.getNumBytesAsUTF8();
        }
    }

    void Log::drenarRt()
    {
        const auto fim = rtEscrita.load (std::memory_order_relaxed);

        // se a thread de audio encheu o anel inteiro, pula para o mais antigo
        // ainda valido em vez de ler lixo
        juce::uint64 perdidos = 0;
        if (fim - rtLeitura > (juce::uint64) kRtCap)
        {
            perdidos  = fim - rtLeitura - (juce::uint64) kRtCap;
            rtLeitura = fim - (juce::uint64) kRtCap;
        }

        // AGREGA em vez de escrever uma linha por evento.
        //
        // Um FIFO cheio se repete a cada bloco de audio -- centenas de vezes por
        // segundo. Uma linha para cada uma afogaria o arquivo e empurraria para
        // fora justamente o que interessa. Entao juntamos o que chegou desde a
        // ultima passada (200 ms) e escrevemos um resumo por tipo.
        int  quantos[8] = {};
        long somaA[8]   = {};
        int  ultimoB[8] = {};

        while (rtLeitura < fim)
        {
            const auto e = anel[rtLeitura % (juce::uint64) kRtCap];
            ++rtLeitura;

            const int c = e.code < 8 ? e.code : 0;
            ++quantos[c];
            somaA[c]   += e.a;
            ultimoB[c]  = e.b;
        }

        auto resumo = [this, &quantos, &somaA, &ultimoB] (RtCode c, LogLevel nivel,
                                                          const juce::String& texto)
        {
            if (quantos[c] > 0)
            {
                write (nivel, "audio", texto + (quantos[c] > 1
                        ? juce::String::formatted ("  (%d ocorrencias nos ultimos 200 ms)", quantos[c])
                        : juce::String()));
            }
            juce::ignoreUnused (somaA, ultimoB);
        };

        if (quantos[rtFifoCheio] > 0)
        {
            const double segPerdidos = somaA[rtFifoCheio] / 16000.0;
            totalPerdidoSeg += segPerdidos;
            write (LogLevel::aviso, "audio", juce::String::formatted (
                "FILA CHEIA: %.2f s de audio descartados agora, %.1f s no total desta sessao "
                "-- a transcricao nao esta acompanhando o tempo real",
                segPerdidos, totalPerdidoSeg));
        }

        if (quantos[rtBlocoGrande] > 0)
            write (LogLevel::aviso, "audio", juce::String::formatted (
                "bloco maior que o preparado (ate %d amostras, capacidade %d) -- %d blocos ignorados",
                (int) somaA[rtBlocoGrande] / juce::jmax (1, quantos[rtBlocoGrande]),
                ultimoB[rtBlocoGrande], quantos[rtBlocoGrande]));

        resumo (rtPrepare,       LogLevel::info,  "prepareToPlay");
        resumo (rtSemLicenca,    LogLevel::debug, "audio passando sem licenca");
        resumo (rtCanaisMudaram, LogLevel::info,  "os canais mudaram");
        resumo (rtSilencioLongo, LogLevel::info,  "sem sinal na entrada");

        if (perdidos > 0)
            write (LogLevel::debug, "audio",
                   juce::String ((int) perdidos) + " eventos de audio nao couberam no anel");
    }

    /*  Vitais: so quando MUDA, mais uma batida de vez em quando.

        Isto era uma linha por segundo, sempre. Numa sessao de 11 horas deu
        35.373 linhas identicas -- 6,6 dos 6,7 MB do arquivo -- quase todas
        dizendo que nada estava acontecendo, porque o plugin estava aberto sem
        licenca e sem transcrever.

        Trinta e cinco mil linhas iguais nao contam nada que uma conte. Entao:
        grava quando o texto muda, e a cada kBatidaVitais grava de novo mesmo
        igual -- essa repeticao periodica nao e desperdicio, e o que prova que
        o programa continuava vivo naquele intervalo. Sem ela, um travamento
        de duas horas ficaria indistinguivel de duas horas paradas.         */
    void Log::emitirVitais()
    {
        static constexpr double kBatidaVitais = 5 * 60 * 1000.0;   // 5 min

        juce::String linha;
        {
            const juce::ScopedLock sl (vitaisLock);
            if (vitais.empty())
                return;
            for (auto& kv : vitais)
                kv.second (linha);
        }

        linha = linha.trim();
        if (linha.isEmpty())
            return;

        const auto agora = juce::Time::getMillisecondCounterHiRes();
        const bool mudou  = linha != ultimaLinhaVitais;
        const bool naHora = agora - ultimaVitaisEscritos >= kBatidaVitais;

        if (! mudou && ! naHora)
            return;

        ultimaLinhaVitais    = linha;
        ultimaVitaisEscritos = agora;
        write (LogLevel::info, "vitais", linha);
    }

    void Log::flushNow()
    {
        const juce::ScopedLock sl (saidaLock);
        if (saida != nullptr)
            saida->flush();
    }

    //==========================================================================
    juce::String Log::tail (int numLinhas)
    {
        // Trava segurada durante o flush E a leitura: se a thread do log
        // escrever (ou rotacionar o arquivo) no meio disto, o resultado e um
        // crash no host. Foi exatamente o que derrubou o REAPER quando o
        // usuario clicou em "Log".
        const juce::ScopedLock sl (saidaLock);

        if (saida != nullptr)
            saida->flush();

        if (! arquivo.existsAsFile())
            return "(sem arquivo de log)";

        auto linhas = juce::StringArray::fromLines (arquivo.loadFileAsString());
        if (linhas.size() <= numLinhas)
            return linhas.joinIntoString (juce::newLine);

        juce::StringArray fim;
        for (int i = linhas.size() - numLinhas; i < linhas.size(); ++i)
            fim.add (linhas[i]);
        return juce::String ("(...)") + juce::newLine + fim.joinIntoString (juce::newLine);
    }

    juce::String Log::diagnostico()
    {
        return juce::String ("Transcriber Live -- diagnostico") + juce::newLine
             + "arquivo: " + getFile().getFullPathName() + juce::newLine
             + juce::newLine
             + tail (400);
    }

    //==========================================================================
    Log::Step::Step (const juce::String& n)
        : nome (n), t0 (juce::Time::getMillisecondCounterHiRes())
    {
        auto& log = Log::get();
        log.write (LogLevel::info, "etapa", "-> " + nome);
        // grava ja: se o host morrer nesta etapa, a ultima linha do arquivo
        // aponta exatamente onde
        log.notify();
    }

    Log::Step::~Step()
    {
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        Log::get().write (LogLevel::info, "etapa",
                          juce::String::formatted ("   ok %s (%.0f ms)", nome.toRawUTF8(), ms));
    }
}
