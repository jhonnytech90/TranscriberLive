#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace tl
{

//==============================================================================
/**
    Vocabulário PT-BR de palco.

    Duas funções, propositalmente separadas:

      montarPrompt()  -> ANTES de transcrever. Um contexto curto de domínio
                         no initial_prompt, para o modelo saber que a fala é
                         de palco. Curto porque o whisper corta o prompt em
                         224 tokens (n_text_ctx/2) e recalcula esses tokens a
                         CADA frase: prompt longo custa tempo em toda
                         transcrição e ainda faz o modelo inventar palavra.

      corrigir()      -> DEPOIS de transcrever. Repõe acento em palavra que
                         saiu com as letras certas. Não entra no modelo: não
                         custa decodificação e não inventa nada.
*/
class Vocabulario
{
public:
    static Vocabulario& obter();

    /** Prompt curto de dominio para o whisper. Curto porque ele e recortado
        em 224 tokens (n_text_ctx/2) e recalculado a cada frase. */
    juce::String montarPrompt() const;

    /** Conserta a GRAFIA de palavras que sairam com as letras certas e sem
        acento: "equalizacao" recebe a cedilha e o til, "Jonatas" recebe o
        circunflexo. Nunca troca uma palavra por outra, entao nao tem como
        piorar o texto -- e por isso a lista pode ser grande a vontade.

        Ja existiu aqui uma correcao por APROXIMACAO (trocar palavra parecida
        por outra). Foi tirada junto com o campo que a alimentava, e nao faz
        falta: contra lista generica ela estragava o texto ("meu ponto" virava
        "meu ponte", a uma letra de distancia). Se voltar algum dia, volta com
        uma lista curta e especifica do show, nunca com a lista toda. */
    juce::String corrigir (const juce::String& texto) const;

    static juce::File arquivoDoUsuario();
    void recarregar();

    int  getNumAcentos()   const noexcept { return acentos.size(); }

    /** A lista crua, para o teste conferir que nenhuma entrada e inutil. */
    const juce::StringArray& getAcentos() const noexcept { return acentos; }
    bool carregouDoDisco() const noexcept { return veioDoDisco; }

    /** minusculas, sem acento, sem pontuacao -- a forma em que as comparacoes
        acontecem. A grafia bonita fica em `acentos`. Publica porque o teste
        usa a MESMA funcao para conferir a lista: se ele reimplementasse a
        regra, os dois poderiam discordar e o teste passaria errado. */
    static juce::String normalizar (const juce::String& s);

private:
    Vocabulario();
    void parse (const juce::String& conteudo);

    static int distancia (const juce::String& a, const juce::String& b, int teto);

    juce::StringArray linhasPrompt;
    juce::StringArray acentos, acentosNorm;    // mesma ordem
    bool veioDoDisco = false;

    JUCE_DECLARE_NON_COPYABLE (Vocabulario)
};

} // namespace tl
