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

} // namespace tl::modelos
