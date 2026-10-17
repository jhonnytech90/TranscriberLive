#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <functional>

/*  Idioma da interface: portugues e ingles.

    Como funciona
    -------------
    A CHAVE de cada texto e o proprio portugues. Nao ha arquivo de ids nem
    "str_botao_qr_17": o codigo continua legivel, e um texto sem traducao
    aparece em portugues em vez de aparecer como chave crua na tela.

    A tabela de ingles esta em TraducaoEn.h, gerada a partir de um catalogo
    (Ferramentas/idioma/). Nao edite aquele arquivo na mao: edite o catalogo
    e gere de novo, senao a proxima geracao apaga a sua correcao.

    Quem troca os textos e o juce::LocalisedStrings, que ja vem no JUCE.

    Por que automatico MAIS escolha manual
    --------------------------------------
    So o automatico seria armadilha: tecnico brasileiro com o sistema em
    ingles abriria o plugin em ingles sem ter pedido. Entao o padrao segue a
    maquina e quem quiser manda por cima -- e a escolha fica gravada.
*/
namespace tl
{
    enum class Idioma { automatico = 0, portugues = 1, ingles = 2 };

    /** Literal UTF-8 do codigo vira juce::String sem depender do compilador. */
    inline juce::String utf8 (const char* s)
    {
        return juce::String (juce::CharPointer_UTF8 (s));
    }

    /** Texto para a tela, na lingua atual. A chave e o portugues. */
    inline juce::String tr (const char* portuguesUtf8)
    {
        return juce::translate (utf8 (portuguesUtf8));
    }

    /** Idem, quando o texto ja esta numa juce::String. */
    inline juce::String tr (const juce::String& portugues)
    {
        return juce::translate (portugues);
    }

    /*  O que a maquina esta falando.

        getUserLanguage devolve o codigo ISO ("pt", "en", "es", ...). Qualquer
        coisa que nao seja portugues cai em ingles: um site ou um plugin em
        portugues para quem nao le portugues nao serve de nada, e ingles
        alcanca muito mais gente.                                            */
    inline Idioma daMaquina()
    {
        const auto lg = juce::SystemStats::getUserLanguage().toLowerCase();
        return lg.startsWith ("pt") ? Idioma::portugues : Idioma::ingles;
    }

    /** O idioma que vale agora, ja resolvendo o "automatico". */
    inline Idioma resolver (Idioma escolhido)
    {
        return escolhido == Idioma::automatico ? daMaquina() : escolhido;
    }

    juce::String tabelaIngles();   // TraducaoEn.h

    /*  Instala (ou tira) a tabela de traducao.

        Em portugues NAO instalamos tabela nenhuma: sem mapeamento, o
        juce::translate devolve o proprio texto, que ja esta em portugues.  */
    inline void aplicarIdioma (Idioma escolhido)
    {
        if (resolver (escolhido) == Idioma::ingles)
            juce::LocalisedStrings::setCurrentMappings (
                new juce::LocalisedStrings (tabelaIngles(), false));
        else
            juce::LocalisedStrings::setCurrentMappings (nullptr);
    }

    /*  Avisa as janelas abertas para redesenhar os textos.

        Sem isto, trocar o idioma so valeria para a proxima vez que a janela
        abrisse -- e o usuario acharia que o seletor nao funcionou.         */
    struct AvisoDeIdioma : public juce::ChangeBroadcaster
    {
        static AvisoDeIdioma& get()
        {
            static AvisoDeIdioma u;
            return u;
        }
    };

    inline void trocarIdioma (Idioma escolhido)
    {
        aplicarIdioma (escolhido);
        AvisoDeIdioma::get().sendChangeMessage();
    }

    /** Nome do idioma para o seletor -- cada um na propria lingua. */
    inline juce::String rotuloIdioma (Idioma i)
    {
        switch (i)
        {
            case Idioma::portugues: return utf8 ("Portugu\xc3\xaas");
            case Idioma::ingles:    return "English";
            case Idioma::automatico:
            default:                return tr ("Autom\xc3\xa1tico (sistema)");
        }
    }

    inline Idioma idiomaDeIndice (int i)
    {
        return i == 1 ? Idioma::portugues : i == 2 ? Idioma::ingles : Idioma::automatico;
    }
    inline int indiceDeIdioma (Idioma i) { return (int) i; }
}

/*  No fim de proposito: a tabela e um header gerado que usa utf8() daqui.
    Incluindo aqui, quem usa o idioma nao precisa saber que a tabela existe
    -- e nao da erro de link por ninguem ter incluido o gerado.            */
#include "TraducaoEn.h"
