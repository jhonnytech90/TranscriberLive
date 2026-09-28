#pragma once

#include <juce_graphics/juce_graphics.h>
#include <vector>
#include <cstdint>

namespace tl
{

//==============================================================================
/**
    Gerador de QR Code, só o necessário para caber um endereço de rede local.

    Modo byte, correção de erro nível M, versões 1 a 6 (até 106 caracteres).
    Uma URL do tipo http://192.168.100.101:47801/ tem 29 caracteres e cabe na
    versão 2, com folga de sobra.

    Por que não uma biblioteca pronta: isto entra num plugin VST3 que já
    carrega JUCE e whisper.cpp, e o QR é meia página de código. Dependência
    nova aqui custaria mais em build, licença e tamanho do que resolve.

    O limite de 6 versões não é preguiça e sim escolha: da versão 7 em diante
    o símbolo precisa carregar também um bloco de informação de versão, e
    nenhum endereço de rede chega perto de 106 caracteres.
*/
class QrCode
{
public:
    /** Devolve false se o texto não couber (mais de 106 bytes em UTF-8). */
    static bool gerar (const juce::String& texto, QrCode& destino);

    int  getTamanho() const noexcept          { return n; }
    bool escuro (int x, int y) const noexcept
    {
        return x >= 0 && y >= 0 && x < n && y < n && modulos[(size_t) (y * n + x)];
    }

    /** Desenha centralizado e quadrado dentro de `area`, com quiet zone. */
    void desenhar (juce::Graphics& g, juce::Rectangle<int> area,
                   juce::Colour fundo = juce::Colours::white,
                   juce::Colour tinta = juce::Colours::black) const;

private:
    int n = 0;
    std::vector<bool> modulos, reservado;

    void  set (int x, int y, bool v)  { modulos[(size_t) (y * n + x)] = v; }
    void  reserva (int x, int y)      { reservado[(size_t) (y * n + x)] = true; }
    bool  isReservado (int x, int y) const { return reservado[(size_t) (y * n + x)]; }

    void desenharFuncoes (int versao);
    void colocarDados (const std::vector<uint8_t>& bits);
    void aplicarMascara (int mascara);
    int  penalidade() const;
    void gravarFormato (int mascara);
};

} // namespace tl
