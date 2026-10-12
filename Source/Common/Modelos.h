#pragma once

#include <juce_core/juce_core.h>

namespace tl::modelos
{

//==============================================================================
/*  Nomes dos modelos pelo que eles ENTREGAM.

    "ggml-large-v3-turbo.bin" nao diz nada para quem opera uma mesa de som no
    meio de um show. O que o tecnico precisa saber e se aquela escolha vai
    transcrever melhor ou responder mais rapido.

    O nome do arquivo nao some: ele continua no log e na dica do campo, que e
    o que serve para suporte. O que muda e so o que aparece na lista.

    A ordem desta tabela importa: a primeira entrada cujo pedaco aparece no
    nome do arquivo e a que vale. Por isso "turbo" vem antes de "large" --
    "ggml-large-v3-turbo.bin" contem as duas palavras, e e turbo.           */

struct Perfil
{
    const char* pedaco;      // o que procurar no nome do arquivo
    const char* rotulo;      // o que aparece na lista (UTF-8)
    const char* detalhe;     // uma linha a mais, na dica
    int         peso;        // 0 = mais leve; so para ordenar a lista
};

inline const Perfil* tabela (int& quantos)
{
    static const Perfil t[] = {
        { "tiny",   "Velocidade m\xc3\xa1xima",
                    "Erra muito em portugu\xc3\xaas. S\xc3\xb3 para teste.", 0 },
        { "base",   "Mais r\xc3\xa1pido",
                    "Para m\xc3\xa1quina fraca ou Mac Intel. Reconhece menos.", 1 },
        { "small",  "Equilibrado",
                    "Bom reconhecimento com folga de CPU. Serve na maioria.", 2 },
        { "medium", "Melhor transcri\xc3\xa7\xc3\xa3o",
                    "Reconhece bem melhor. Exige m\xc3\xa1quina boa.", 3 },
        { "turbo",  "M\xc3\xa1xima precis\xc3\xa3o",
                    "O que mais acerta sem perder o tempo real. "
                    "Apple Silicon ou PC forte.", 4 },
        { "large",  "M\xc3\xa1xima precis\xc3\xa3o (pesado)",
                    "O teto do reconhecimento. Pode n\xc3\xa3o acompanhar o show.", 5 },
    };

    quantos = (int) juce::numElementsInArray (t);
    return t;
}

inline const Perfil* perfilDe (const juce::String& nomeArquivo)
{
    int n = 0;
    const auto* t = tabela (n);

    for (int i = 0; i < n; ++i)
        if (nomeArquivo.containsIgnoreCase (t[i].pedaco))
            return &t[i];

    return nullptr;
}

/** O que aparece na lista. Modelo que nao conhecemos mostra o nome do
    arquivo: melhor isso do que esconder que ele existe. */
inline juce::String rotulo (const juce::String& nomeArquivo)
{
    if (const auto* p = perfilDe (nomeArquivo))
        return juce::String (juce::CharPointer_UTF8 (p->rotulo));

    return nomeArquivo;
}

/** Rotulo + nome do arquivo, para a dica do campo e para o log. */
inline juce::String descricao (const juce::String& nomeArquivo)
{
    if (const auto* p = perfilDe (nomeArquivo))
        return juce::String (juce::CharPointer_UTF8 (p->rotulo)) + " \xe2\x80\x94 "
             + juce::String (juce::CharPointer_UTF8 (p->detalhe))
             + "  (" + nomeArquivo + ")";

    return nomeArquivo;
}

/** 0 = mais leve. Desconhecido vai para o fim da fila. */
inline int peso (const juce::String& nomeArquivo)
{
    if (const auto* p = perfilDe (nomeArquivo))
        return p->peso;

    return 99;
}

//==============================================================================
/*  MODOS

    O tecnico nao escolhe mais um arquivo de modelo: escolhe o que ele quer da
    ferramenta. Qual .bin atende aquilo e problema nosso, e muda conforme o que
    esta instalado na maquina.

      Automatico  comeca no melhor que a maquina aguenta e desce sozinho se o
                  RTF mostrar que nao acompanha. E o que o plugin ja fazia.
      Desempenho  fixa o mais leve dos instalados. Nunca sobe, nunca desce.
      Precisao    fixa o mais pesado. NAO rebaixa nem se atrasar -- quem
                  escolhe isto esta dizendo que prefere esperar a errar.

    Com o instalador padrao ha dois modelos (base e large-v3-turbo), entao os
    tres modos sao de fato tres comportamentos diferentes. Se um dia houver so
    um modelo, os tres apontam para ele e nada quebra.                        */

enum class Modo { automatico = 0, desempenho = 1, precisao = 2 };

inline Modo modoDeIndice (int i)
{
    return i == 1 ? Modo::desempenho : (i == 2 ? Modo::precisao : Modo::automatico);
}

inline juce::String rotuloModo (Modo m)
{
    switch (m)
    {
        case Modo::desempenho: return juce::String (juce::CharPointer_UTF8 ("Desempenho"));
        case Modo::precisao:   return juce::String (juce::CharPointer_UTF8 ("Precis\xc3\xa3o"));
        case Modo::automatico:
        default:               return juce::String (juce::CharPointer_UTF8 ("Autom\xc3\xa1tico"));
    }
}

inline juce::String detalheModo (Modo m)
{
    switch (m)
    {
        case Modo::desempenho:
            return juce::String (juce::CharPointer_UTF8 (
                "Responde o mais r\xc3\xa1pido poss\xc3\xadvel. Erra mais palavra, "
                "mas n\xc3\xa3o atrasa nem na m\xc3\xa1quina mais fraca."));
        case Modo::precisao:
            return juce::String (juce::CharPointer_UTF8 (
                "O melhor reconhecimento dispon\xc3\xadvel. Em m\xc3\xa1quina fraca "
                "a legenda pode atrasar \xe2\x80\x94 e n\xc3\xa3o vai se rebaixar sozinho."));
        case Modo::automatico:
        default:
            return juce::String (juce::CharPointer_UTF8 (
                "Comeca no melhor que esta m\xc3\xa1quina aguenta e alivia sozinho "
                "se come\xc3\xa7ar a atrasar. Na d\xc3\xbavida, deixe neste."));
    }
}

/** O mais leve e o mais pesado entre os instalados. Vazio se a lista estiver
    vazia. Usa a mesma escala de peso da tabela acima. */
inline juce::String maisLeve (const juce::StringArray& instalados)
{
    juce::String melhor;
    int melhorPeso = 1000;

    for (auto& m : instalados)
        if (const int p = peso (m); p < melhorPeso) { melhorPeso = p; melhor = m; }

    return melhor;
}

inline juce::String maisPesado (const juce::StringArray& instalados)
{
    juce::String melhor;
    int melhorPeso = -1;

    for (auto& m : instalados)
    {
        // Modelo desconhecido tem peso 99; nao deixamos ele virar "o mais
        // pesado" so por nao estar na tabela.
        const int p = peso (m);
        if (p < 99 && p > melhorPeso) { melhorPeso = p; melhor = m; }
    }

    if (melhor.isEmpty() && ! instalados.isEmpty())
        melhor = instalados[instalados.size() - 1];

    return melhor;
}

} // namespace tl::modelos
