#pragma once

#include <juce_core/juce_core.h>

/*
    Rastreio de inicializacao.

    O plugin escreve uma linha por etapa em

        Windows : %APPDATA%\TranscriberLive\trace.log
        macOS   : ~/Library/Application Support/TranscriberLive/trace.log

    Serve exatamente para o caso "o plugin abre e fecha": a ultima linha do
    arquivo diz qual etapa comecou e nao terminou. O custo e uma abertura de
    arquivo por etapa, so na criacao da instancia — nada disso roda na thread
    de audio.

    Para desligar: crie um arquivo vazio chamado "no-trace" na mesma pasta.
*/

namespace tl
{
    struct Trace
    {
        static juce::File file()
        {
            // NAO usa Hub::getDataDir() de proposito: o rastreio precisa
            // funcionar mesmo que o problema esteja no proprio Hub.
           #if JUCE_MAC
            auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                          .getChildFile ("Application Support").getChildFile ("TranscriberLive");
           #else
            auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                          .getChildFile ("TranscriberLive");
           #endif
            return dir.getChildFile ("trace.log");
        }

        static void write (const juce::String& line)
        {
            static const bool enabled = ! file().getSiblingFile ("no-trace").existsAsFile();
            if (! enabled) return;

            auto f = file();
            f.getParentDirectory().createDirectory();

            // nao deixa o log crescer para sempre
            if (f.getSize() > 256 * 1024)
                f.deleteFile();

            f.appendText (juce::Time::getCurrentTime().toString (true, true, true, true)
                          + "  " + line + juce::newLine);
        }

        /** Marca inicio e fim de uma etapa. Se o processo morrer no meio, o
            arquivo termina com o "-> etapa" e sem o "   ok etapa". */
        struct Step
        {
            juce::String name;
            explicit Step (const juce::String& n) : name (n) { write ("-> " + name); }
            ~Step() { write ("   ok " + name); }
        };
    };
}

#define TL_TRACE(msg)          ::tl::Trace::write (msg)
#define TL_TRACE_CAT2(a, b)    a ## b
#define TL_TRACE_CAT(a, b)     TL_TRACE_CAT2 (a, b)
#define TL_TRACE_STEP(nm)      ::tl::Trace::Step TL_TRACE_CAT (tlTraceStep_, __LINE__) { nm }
