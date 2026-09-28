#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace tl
{

//==============================================================================
/**
    Vocabulário PT-BR de palco.

    Duas funções, propositalmente separadas, porque atacam problemas
    diferentes e têm custos diferentes:

      montarPrompt()  -> ANTES de transcrever. Inclina o modelo para este
                         domínio. O whisper corta o initial_prompt em 224
                         tokens (n_text_ctx/2) e guarda os ÚLTIMOS, e esses
                         tokens são recalculados a CADA frase. Por isso o
                         prompt é curto e os termos do show vão no FIM: é o
                         pedaço que sobrevive ao corte.

      corrigir()      -> DEPOIS de transcrever. Conserta palavra que saiu
                         quase certa ("Jonatas" -> "Jônatas") comparando com
                         uma lista que pode ser grande à vontade, porque não
                         entra no modelo: não custa tempo de decodificação e
                         não inventa palavra que ninguém falou.

    Por que não jogar a lista inteira no prompt: além de ser cortada em 224
    tokens, uma lista de nomes no prompt faz o whisper ALUCINAR nomes que não
    foram ditos -- ele entende a lista como contexto do que está sendo falado.
    Testar isso foi o que separou as duas funções.
*/
class Vocabulario
{
public:
    static Vocabulario& obter();

    /** Prompt para o whisper. `termosDoShow` entra no fim, que e a parte que
        sobrevive ao corte de 224 tokens. */
    juce::String montarPrompt (const juce::String& termosDoShow) const;

    /** Termos do show correntes. Guardados aqui, e nao passados a cada
        chamada, porque quem chama corrigir() e a thread do worker do motor,
        por um lambda que nao captura o processador -- se capturasse, um
        descarregamento do plugin no meio de uma transcricao deixaria
        ponteiro pendurado. */
    void setTermosDoShow (const juce::String& termos);

    /** Conserta o texto do whisper. Duas regras, com riscos bem diferentes:

        1. ACENTO (lista [acentos], centenas de entradas): so troca a grafia
           de uma palavra que ja saiu com as mesmas letras -- "equalizacao"
           recebe cedilha e til. Nunca troca uma palavra por outra, entao
           nao tem como piorar o texto.

        2. APROXIMACAO (so contra os termos do show): troca palavra parecida
           por termo do show. So aqui, e de proposito. Medido: com a lista
           generica ligada nesta regra, "meu ponto" virava "meu ponte" --
           "ponte" esta a uma letra de distancia. Palavra certa trocada por
           errada num telao de show e pior do que nao corrigir. */
    juce::String corrigir (const juce::String& texto) const;

    static juce::File arquivoDoUsuario();
    void recarregar();

    int  getNumAcentos()   const noexcept { return acentos.size(); }
    bool carregouDoDisco() const noexcept { return veioDoDisco; }

private:
    Vocabulario();
    void parse (const juce::String& conteudo);

    /** minusculas, sem acento, sem pontuacao -- a forma em que as comparacoes
        acontecem. A grafia bonita fica em `acentos`. */
    static juce::String normalizar (const juce::String& s);
    static int distancia (const juce::String& a, const juce::String& b, int teto);

    juce::StringArray linhasPrompt;
    juce::StringArray acentos, acentosNorm;    // mesma ordem
    bool veioDoDisco = false;

    mutable juce::CriticalSection termosLock;
    juce::String      termosTexto;
    juce::StringArray termos, termosNorm;      // palavras soltas dos termos

    JUCE_DECLARE_NON_COPYABLE (Vocabulario)
};

} // namespace tl
