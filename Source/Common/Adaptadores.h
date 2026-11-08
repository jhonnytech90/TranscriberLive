#pragma once

#include <juce_core/juce_core.h>

/*  Nome real da placa de rede, para o usuario saber qual IP escolher.

    O JUCE entrega so uma lista de enderecos. Numa mesa de som moderna a
    maquina tem tres ou quatro: o Wi-Fi da casa, a placa cabeada do switch do
    palco, a interface de audio em rede (Dante/AVB) e, no Mac, ainda a ponte do
    Thunderbolt. Todos sao "192.168.alguma coisa", entao olhar so o numero nao
    diz nada -- e foi exatamente isso que o usuario viu: tres linhas escritas
    "Rede local".

    Quem sabe diferenciar e o sistema. No macOS, SystemConfiguration devolve o
    mesmo nome que aparece em Ajustes de Rede ("Wi-Fi", "Ethernet",
    "Thunderbolt Bridge"). No Windows, GetAdaptersAddresses devolve o nome
    amigavel do adaptador e o tipo (IF_TYPE_IEEE80211 = sem fio).

    Se por qualquer motivo o sistema nao responder, voltamos ao que ja havia:
    a classificacao pela faixa do IP. Nunca fica pior do que estava.        */
namespace tl
{
    struct Adaptadores
    {
        enum class Tipo { desconhecida, semFio, cabeada, ponte, virtual_ };

        struct Placa
        {
            juce::String ip;        // 192.168.1.12
            juce::String sistema;   // en0 / {GUID}
            juce::String nome;      // Wi-Fi / Ethernet / Realtek Gaming 2.5GbE
            Tipo tipo = Tipo::desconhecida;
        };

        /** Todas as placas com IPv4, do jeito que o sistema as enxerga.
            O resultado fica em cache por 5 s: isto e chamado ao desenhar. */
        static juce::Array<Placa> listar();

        /** A placa dona deste IP. Vem com nome vazio se o sistema nao soube. */
        static Placa doEndereco (const juce::String& ip);

        /** "Wi-Fi", "Cabeada", "Ponte"... ja traduzido. Vazio se desconhecida. */
        static juce::String rotuloDoTipo (Tipo t);
    };
}
