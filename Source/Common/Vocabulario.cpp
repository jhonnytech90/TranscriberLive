#include "Vocabulario.h"
#include "Log.h"
#include "Hub.h"
#include <BinaryData.h>
#include <algorithm>

namespace tl
{

//==============================================================================
juce::File Vocabulario::arquivoDoUsuario()
{
    return Hub::getDataDir().getChildFile ("vocabulario-pt-br.txt");
}

Vocabulario& Vocabulario::obter()
{
    static Vocabulario v;
    return v;
}

Vocabulario::Vocabulario()
{
    recarregar();
}

void Vocabulario::recarregar()
{
    linhasPrompt.clear();
    acentos.clear();
    acentosNorm.clear();
    veioDoDisco = false;

    const auto f = arquivoDoUsuario();

    // Primeira vez: grava o embutido no disco para o técnico poder editar.
    // Se a gravação falhar (pasta sem permissão), seguimos com o embutido --
    // vocabulário é melhoria, não pode impedir o plugin de funcionar.
    if (! f.existsAsFile())
    {
        f.getParentDirectory().createDirectory();
        if (f.replaceWithText (juce::String::fromUTF8 (BinaryData::vocabularioptbr_txt,
                                                       BinaryData::vocabularioptbr_txtSize)))
            TL_LOGI ("vocab", "vocabulario gravado em " + f.getFullPathName());
        else
            TL_LOGW ("vocab", "nao consegui gravar o vocabulario em disco; usando o embutido");
    }

    juce::String conteudo;
    if (f.existsAsFile())
    {
        conteudo    = f.loadFileAsString();
        veioDoDisco = conteudo.isNotEmpty();
    }

    if (conteudo.isEmpty())
        conteudo = juce::String::fromUTF8 (BinaryData::vocabularioptbr_txt,
                                           BinaryData::vocabularioptbr_txtSize);

    parse (conteudo);

    TL_LOGI ("vocab", juce::String::formatted (
        "vocabulario: %d linhas de prompt, %d termos de acentuacao (%s)",
        linhasPrompt.size(), acentos.size(), veioDoDisco ? "disco" : "embutido"));
}

void Vocabulario::parse (const juce::String& conteudo)
{
    enum class Secao { nenhuma, prompt, acentos_ };
    auto secao = Secao::nenhuma;

    for (auto& bruta : juce::StringArray::fromLines (conteudo))
    {
        const auto linha = bruta.trim();

        if (linha.isEmpty() || linha.startsWithChar ('#'))
            continue;

        if (linha.startsWithChar ('['))
        {
            const auto nome = linha.removeCharacters ("[]").trim().toLowerCase();
            secao = nome.startsWith ("prompt") ? Secao::prompt
                  : (nome.startsWith ("acento") || nome.startsWith ("correcao")
                     || nome.startsWith ("correção")) ? Secao::acentos_
                                                      : Secao::nenhuma;
            continue;
        }

        if (secao == Secao::prompt)
        {
            linhasPrompt.add (linha);
        }
        else if (secao == Secao::acentos_)
        {
            // Entrada com espaco ("mesa de som") nao entra: a correcao
            // trabalha palavra por palavra e nao teria como casar a frase
            // inteira quando o whisper quebra o texto de outro jeito.
            if (linha.containsChar (' '))
                continue;

            const auto norm = normalizar (linha);
            if (norm.length() < 4 || acentosNorm.contains (norm))
                continue;

            acentos.add (linha);
            acentosNorm.add (norm);
        }
    }
}

//==============================================================================
juce::String Vocabulario::montarPrompt() const
{
    return linhasPrompt.joinIntoString (" ").trim();
}

//==============================================================================
juce::String Vocabulario::normalizar (const juce::String& s)
{
    static const juce::String comAcento =
        juce::String::fromUTF8 ("áàâãäéèêëíìîïóòôõöúùûüçñÁÀÂÃÄÉÈÊËÍÌÎÏÓÒÔÕÖÚÙÛÜÇÑ");
    static const juce::String semAcento =
        juce::String::fromUTF8 ("aaaaaeeeeiiiiooooouuuucnAAAAAEEEEIIIIOOOOOUUUUCN");

    juce::String out;
    out.preallocateBytes ((size_t) s.length() * 2);

    for (auto c : s)
    {
        const int i = comAcento.indexOfChar (c);
        const auto ch = (i >= 0) ? semAcento[i] : c;

        if (juce::CharacterFunctions::isLetterOrDigit (ch) || ch == '-')
            out << juce::CharacterFunctions::toLowerCase (ch);
    }

    return out;
}

/** Levenshtein com corte: se passar de `teto`, devolve teto+1 e para.
    O corte é o que torna viável comparar cada palavra com centenas de termos
    a cada frase -- a maioria das comparações morre nas primeiras linhas. */
int Vocabulario::distancia (const juce::String& a, const juce::String& b, int teto)
{
    const int na = a.length(), nb = b.length();

    if (std::abs (na - nb) > teto) return teto + 1;
    if (na == 0) return nb;
    if (nb == 0) return na;

    std::vector<int> ant ((size_t) nb + 1), cur ((size_t) nb + 1);
    for (int j = 0; j <= nb; ++j) ant[(size_t) j] = j;

    for (int i = 1; i <= na; ++i)
    {
        cur[0] = i;
        int melhorNaLinha = cur[0];

        for (int j = 1; j <= nb; ++j)
        {
            const int custo = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[(size_t) j] = juce::jmin (juce::jmin (cur[(size_t) j - 1] + 1,
                                                      ant[(size_t) j] + 1),
                                          ant[(size_t) j - 1] + custo);
            melhorNaLinha = juce::jmin (melhorNaLinha, cur[(size_t) j]);
        }

        if (melhorNaLinha > teto) return teto + 1;
        std::swap (ant, cur);
    }

    return ant[(size_t) nb];
}

//==============================================================================
juce::String Vocabulario::corrigir (const juce::String& texto) const
{
    if (texto.isEmpty() || acentos.isEmpty())
        return texto;

    juce::String saida;
    saida.preallocateBytes ((size_t) texto.length() * 2);

    juce::String palavra;
    int trocas = 0;

    auto despejar = [&]
    {
        if (palavra.isEmpty())
            return;

        const auto norm = normalizar (palavra);

        // 4 letras e o piso: abaixo disso nao vale o risco nem o trabalho.
        if (norm.length() >= 4)
        {
            const int i = acentosNorm.indexOf (norm);

            if (i >= 0)
            {
                auto certa = acentos[i];

                // Preserva a maiuscula que veio da frase: inicio de frase nao
                // pode virar minuscula so porque a lista guarda a palavra em
                // caixa baixa.
                if (juce::CharacterFunctions::isUpperCase (palavra[0])
                    && juce::CharacterFunctions::isLowerCase (certa[0]))
                    certa = certa.substring (0, 1).toUpperCase() + certa.substring (1);

                if (certa != palavra) { palavra = certa; ++trocas; }
            }
        }

        saida << palavra;
        palavra.clear();
    };

    for (auto c : texto)
    {
        if (normalizar (juce::String::charToString (c)).isNotEmpty())
            palavra << c;
        else
        {
            despejar();
            saida << c;
        }
    }
    despejar();

    if (trocas > 0)
        TL_LOGD ("vocab", juce::String::formatted ("%d acento(s) reposto(s)", trocas));

    return saida;
}

} // namespace tl
