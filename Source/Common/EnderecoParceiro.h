#pragma once

#include <juce_core/juce_core.h>

namespace tl
{
    /*  Endereco da maquina parceira: duas funcoes pequenas, fora do Hub e fora
        da interface, para o teste poder exercitar exatamente o que o usuario
        digita sem subir servidor nenhum.

        Nasceu do tecnico de P.A. e do de monitor, cada um num PC. O campo e
        preenchido no meio do show, as pressas, com o que estiver na mao.    */

    /*  Aceita "192.168.0.20", "192.168.0.20:47801", "http://192.168.0.20:47801/"
        e devolve so o que serve. Limpar e melhor que recusar: recusar deixa o
        tecnico parado diante de um campo vermelho sem saber o que tirar.

        A porta digitada FICA. As duas maquinas quase sempre usam a mesma porta
        web, mas quem achou a porta ocupada subiu para a seguinte, e so quem
        esta na frente daquele PC sabe disso.                                */
    inline juce::String limparEnderecoParceiro (const juce::String& bruto)
    {
        return bruto.trim()
                    .fromLastOccurrenceOf ("//", false, false)   // tira http://
                    .upToFirstOccurrenceOf ("/",  false, false)  // tira /caminho
                    .trim();
    }

    /** Completa com a porta local quando o usuario digitou so o IP. */
    inline juce::String completarPorta (const juce::String& host, int portaPadrao)
    {
        if (host.isEmpty() || host.containsChar (':')) return host;
        return host + ":" + juce::String (portaPadrao);
    }
}
