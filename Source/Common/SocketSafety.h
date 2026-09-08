#pragma once

#include <juce_core/juce_core.h>

#if ! JUCE_WINDOWS
 #include <sys/socket.h>
 #include <signal.h>
#endif

namespace tl
{
    /**
        Proteção contra SIGPIPE.

        No macOS (e Linux), escrever num socket cujo outro lado já fechou — ex.: o
        celular fechou a página / bloqueou a tela — gera SIGPIPE, que por padrão
        MATA O PROCESSO INTEIRO (o host ou o app). O JUCE não protege os sockets
        contra isso, então fazemos aqui: SO_NOSIGPIPE por socket (Apple) e, por
        segurança, ignoramos o sinal no processo (todos os hosts de áudio já
        convivem bem com isso).
    */
    inline void ignoreSigpipeOnce()
    {
       #if ! JUCE_WINDOWS
        static bool done = false;
        if (! done) { done = true; ::signal (SIGPIPE, SIG_IGN); }
       #endif
    }

    inline void makeSocketSafe (int rawHandle)
    {
       #if JUCE_MAC || JUCE_IOS
        if (rawHandle >= 0)
        {
            int one = 1;
            ::setsockopt (rawHandle, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof (one));
        }
       #else
        juce::ignoreUnused (rawHandle);
       #endif
    }
}
