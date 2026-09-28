#include "QrCode.h"
#include <array>
#include <limits>
#include <cstring>
#include <algorithm>

namespace tl
{
namespace
{

// --- tabelas, versões 1..6, nível M ----------------------------------------
// Nas seis primeiras versões do nível M todos os blocos têm o mesmo tamanho,
// o que dispensa a distinção entre grupo 1 e grupo 2 do padrão. É por isso
// que o intercalado abaixo é um laço simples.
struct Spec { int totalCw, eccPorBloco, blocos, alinhamento; };

constexpr Spec kSpec[7] = {
    { 0,   0,  0,  0 },   // índice 0 não usado
    { 26,  10, 1,  0 },
    { 44,  16, 1, 18 },
    { 70,  26, 1, 22 },
    { 100, 18, 2, 26 },
    { 134, 24, 2, 30 },
    { 172, 16, 4, 34 },
};

inline int dadosCw (int v)      { return kSpec[v].totalCw - kSpec[v].eccPorBloco * kSpec[v].blocos; }
inline int capacidade (int v)   { return (dadosCw (v) * 8 - 12) / 8; }   // 4 bits modo + 8 bits contagem

//--------------------------------------------------------------------------
// GF(256) com polinômio primitivo 0x11D, como manda a especificação do QR.
struct Galois
{
    uint8_t exp[512] {}, log[256] {};

    Galois()
    {
        int x = 1;
        for (int i = 0; i < 255; ++i)
        {
            exp[i] = (uint8_t) x;
            log[x] = (uint8_t) i;
            x <<= 1;
            if (x & 0x100) x ^= 0x11D;
        }
        for (int i = 255; i < 512; ++i)
            exp[i] = exp[i - 255];
    }

    uint8_t mul (uint8_t a, uint8_t b) const
    {
        if (a == 0 || b == 0) return 0;
        return exp[log[a] + log[b]];
    }
};

const Galois& gf() { static const Galois g; return g; }

/** Reed-Solomon sistemático: devolve os `nEcc` bytes de correção de `dados`. */
std::vector<uint8_t> rs (const std::vector<uint8_t>& dados, int nEcc)
{
    // gerador = produto de (x - a^i)
    std::vector<uint8_t> ger (1, 1);
    for (int i = 0; i < nEcc; ++i)
    {
        std::vector<uint8_t> novo (ger.size() + 1, 0);
        // ger[0] é o coeficiente de MAIOR grau. Multiplicar por (x + a^i):
        // o termo em x mantém o índice, o termo constante anda um para a
        // frente. Já escrevi isto ao contrário uma vez -- os dados saíam
        // certos, só a correção de erro saía errada, e nenhum leitor
        // conseguia ler o código. Foi preciso comparar codeword a codeword
        // com um gerador de referência para ver onde estava.
        for (size_t j = 0; j < ger.size(); ++j)
        {
            novo[j]     ^= ger[j];
            novo[j + 1] ^= gf().mul (ger[j], gf().exp[i]);
        }
        ger = novo;
    }

    std::vector<uint8_t> resto ((size_t) nEcc, 0);
    for (uint8_t d : dados)
    {
        const uint8_t fator = d ^ resto[0];
        resto.erase (resto.begin());
        resto.push_back (0);
        if (fator != 0)
            for (size_t i = 0; i < resto.size(); ++i)
                resto[i] ^= gf().mul (ger[i + 1], fator);
    }
    return resto;
}

} // namespace

//==============================================================================
bool QrCode::gerar (const juce::String& texto, QrCode& destino)
{
    const auto utf8 = texto.toRawUTF8();
    const std::vector<uint8_t> bytes (utf8, utf8 + std::strlen (utf8));

    int versao = 0;
    for (int v = 1; v <= 6; ++v)
        if ((int) bytes.size() <= capacidade (v)) { versao = v; break; }

    if (versao == 0)
        return false;   // não cabe: quem chama decide o que mostrar

    // ---- fluxo de bits -----------------------------------------------------
    std::vector<uint8_t> bits;
    auto push = [&bits] (int valor, int nBits)
    {
        for (int i = nBits - 1; i >= 0; --i)
            bits.push_back ((uint8_t) ((valor >> i) & 1));
    };

    push (0b0100, 4);                    // modo byte
    push ((int) bytes.size(), 8);        // contagem (8 bits até a versão 9)
    for (uint8_t b : bytes) push (b, 8);

    const int capBits = dadosCw (versao) * 8;
    for (int i = 0; i < 4 && (int) bits.size() < capBits; ++i)   // terminador
        bits.push_back (0);
    while (bits.size() % 8 != 0) bits.push_back (0);

    std::vector<uint8_t> cw;
    for (size_t i = 0; i < bits.size(); i += 8)
    {
        uint8_t b = 0;
        for (int j = 0; j < 8; ++j) b = (uint8_t) ((b << 1) | bits[i + (size_t) j]);
        cw.push_back (b);
    }
    // preenchimento alternado 0xEC / 0x11, como manda o padrão
    for (int i = 0; (int) cw.size() < dadosCw (versao); ++i)
        cw.push_back (i % 2 == 0 ? 0xEC : 0x11);

    // ---- blocos e intercalado ---------------------------------------------
    const int nBlocos = kSpec[versao].blocos;
    const int porBloco = dadosCw (versao) / nBlocos;
    const int nEcc = kSpec[versao].eccPorBloco;

    std::vector<std::vector<uint8_t>> blocosDados, blocosEcc;
    for (int b = 0; b < nBlocos; ++b)
    {
        std::vector<uint8_t> d (cw.begin() + b * porBloco, cw.begin() + (b + 1) * porBloco);
        blocosEcc.push_back (rs (d, nEcc));
        blocosDados.push_back (std::move (d));
    }

    std::vector<uint8_t> finalCw;
    for (int i = 0; i < porBloco; ++i)
        for (int b = 0; b < nBlocos; ++b)
            finalCw.push_back (blocosDados[(size_t) b][(size_t) i]);
    for (int i = 0; i < nEcc; ++i)
        for (int b = 0; b < nBlocos; ++b)
            finalCw.push_back (blocosEcc[(size_t) b][(size_t) i]);

    std::vector<uint8_t> bitsFinais;
    bitsFinais.reserve (finalCw.size() * 8);
    for (uint8_t b : finalCw)
        for (int i = 7; i >= 0; --i)
            bitsFinais.push_back ((uint8_t) ((b >> i) & 1));

    // ---- matriz ------------------------------------------------------------
    destino.n = versao * 4 + 17;
    destino.modulos.assign ((size_t) (destino.n * destino.n), false);
    destino.reservado.assign ((size_t) (destino.n * destino.n), false);

    destino.desenharFuncoes (versao);
    destino.colocarDados (bitsFinais);

    // Escolhe a máscara pela menor penalidade, como o padrão define. Sem isso
    // o símbolo pode sair com faixas grandes de uma cor só e leitor de celular
    // erra a leitura em ângulo ou com pouca luz.
    int melhorMascara = 0, melhorPen = std::numeric_limits<int>::max();
    for (int m = 0; m < 8; ++m)
    {
        destino.aplicarMascara (m);
        destino.gravarFormato (m);
        const int pen = destino.penalidade();
        if (pen < melhorPen) { melhorPen = pen; melhorMascara = m; }
        destino.aplicarMascara (m);   // XOR de novo desfaz
    }
    destino.aplicarMascara (melhorMascara);
    destino.gravarFormato (melhorMascara);

    return true;
}

//==============================================================================
void QrCode::desenharFuncoes (int versao)
{
    auto finder = [this] (int cx, int cy)
    {
        for (int dy = -4; dy <= 4; ++dy)
            for (int dx = -4; dx <= 4; ++dx)
            {
                const int x = cx + dx, y = cy + dy;
                if (x < 0 || y < 0 || x >= n || y >= n) continue;
                const int d = juce::jmax (std::abs (dx), std::abs (dy));
                set (x, y, d != 2 && d <= 3);
                reserva (x, y);
            }
    };

    finder (3, 3);
    finder (n - 4, 3);
    finder (3, n - 4);

    // timing
    for (int i = 8; i < n - 8; ++i)
    {
        set (i, 6, i % 2 == 0);  reserva (i, 6);
        set (6, i, i % 2 == 0);  reserva (6, i);
    }

    // alinhamento (da versão 2 em diante, um só nas versões que usamos)
    if (const int a = kSpec[versao].alinhamento; a > 0)
    {
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx)
            {
                const int d = juce::jmax (std::abs (dx), std::abs (dy));
                set (a + dx, a + dy, d != 1);
                reserva (a + dx, a + dy);
            }
    }

    // área do formato + módulo sempre escuro
    for (int i = 0; i < 9; ++i)   { reserva (i, 8); reserva (8, i); }
    for (int i = 0; i < 8; ++i)   { reserva (n - 1 - i, 8); reserva (8, n - 1 - i); }
    set (8, n - 8, true);
    reserva (8, n - 8);
}

void QrCode::colocarDados (const std::vector<uint8_t>& bits)
{
    size_t i = 0;
    bool subindo = true;

    for (int col = n - 1; col >= 1; col -= 2)
    {
        if (col == 6) --col;   // a coluna do timing não conta

        for (int passo = 0; passo < n; ++passo)
        {
            const int y = subindo ? (n - 1 - passo) : passo;

            for (int k = 0; k < 2; ++k)
            {
                const int x = col - k;
                if (isReservado (x, y)) continue;
                set (x, y, i < bits.size() && bits[i] != 0);
                ++i;
            }
        }
        subindo = ! subindo;
    }
}

void QrCode::aplicarMascara (int m)
{
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
        {
            if (isReservado (x, y)) continue;

            bool inverte = false;
            switch (m)
            {
                case 0: inverte = ((x + y) % 2) == 0; break;
                case 1: inverte = (y % 2) == 0; break;
                case 2: inverte = (x % 3) == 0; break;
                case 3: inverte = ((x + y) % 3) == 0; break;
                case 4: inverte = (((y / 2) + (x / 3)) % 2) == 0; break;
                case 5: inverte = ((x * y) % 2 + (x * y) % 3) == 0; break;
                case 6: inverte = (((x * y) % 2 + (x * y) % 3) % 2) == 0; break;
                default:inverte = (((x + y) % 2 + (x * y) % 3) % 2) == 0; break;
            }

            if (inverte)
                set (x, y, ! escuro (x, y));
        }
}

void QrCode::gravarFormato (int mascara)
{
    // nível M = 0b00, seguido dos 3 bits da máscara; BCH(15,5) + máscara 0x5412
    int dados = (0b00 << 3) | mascara;
    int resto = dados;
    for (int i = 0; i < 10; ++i)
    {
        resto <<= 1;
        if (resto & 0x400) resto ^= 0x537;
    }
    const int fmt = ((dados << 10) | resto) ^ 0x5412;

    for (int i = 0; i < 15; ++i)
    {
        const bool b = ((fmt >> i) & 1) != 0;

        // cópia 1, em volta do finder superior esquerdo
        if (i < 6)       set (8, i, b);
        else if (i == 6) set (8, 7, b);
        else if (i == 7) set (8, 8, b);
        else if (i == 8) set (7, 8, b);
        else             set (14 - i, 8, b);

        // cópia 2, espelhada nos outros dois cantos
        if (i < 8) set (n - 1 - i, 8, b);
        else       set (8, n - 15 + i, b);
    }
}

int QrCode::penalidade() const
{
    int p = 0;

    // regra 1: sequências de 5 ou mais módulos iguais
    auto corrida = [&] (bool horizontal)
    {
        for (int a = 0; a < n; ++a)
        {
            int run = 1;
            for (int b = 1; b < n; ++b)
            {
                const bool atual  = horizontal ? escuro (b, a)     : escuro (a, b);
                const bool antes  = horizontal ? escuro (b - 1, a) : escuro (a, b - 1);
                if (atual == antes) { ++run; }
                else { if (run >= 5) p += 3 + (run - 5); run = 1; }
            }
            if (run >= 5) p += 3 + (run - 5);
        }
    };
    corrida (true);
    corrida (false);

    // regra 2: blocos 2x2 de uma cor só
    for (int y = 0; y + 1 < n; ++y)
        for (int x = 0; x + 1 < n; ++x)
        {
            const bool c = escuro (x, y);
            if (c == escuro (x + 1, y) && c == escuro (x, y + 1) && c == escuro (x + 1, y + 1))
                p += 3;
        }

    // regra 3: padrão parecido com finder (1:1:3:1:1) em qualquer direção
    static const bool alvo[7] = { true, false, true, true, true, false, true };
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
        {
            bool h = x + 6 < n, v = y + 6 < n;
            for (int i = 0; i < 7; ++i)
            {
                if (h && escuro (x + i, y) != alvo[i]) h = false;
                if (v && escuro (x, y + i) != alvo[i]) v = false;
            }
            if (h) p += 40;
            if (v) p += 40;
        }

    // regra 4: desequilíbrio entre claro e escuro
    int escuros = 0;
    for (int i = 0; i < n * n; ++i)
        if (modulos[(size_t) i]) ++escuros;
    const int pct = escuros * 100 / (n * n);
    p += (std::abs (pct - 50) / 5) * 10;

    return p;
}

//==============================================================================
void QrCode::desenhar (juce::Graphics& g, juce::Rectangle<int> area,
                       juce::Colour fundo, juce::Colour tinta) const
{
    if (n <= 0) return;

    // Quiet zone de 4 módulos: sem ela muitos leitores simplesmente não veem
    // o código. É parte do símbolo, não margem estética.
    const int quiet = 4;
    const int total = n + quiet * 2;

    // Escala inteira: meio pixel por módulo borra a grade e o celular erra.
    const int lado = juce::jmin (area.getWidth(), area.getHeight());
    const int esc  = juce::jmax (1, lado / total);
    const int px   = esc * total;

    auto r = juce::Rectangle<int> (px, px).withCentre (area.getCentre());

    g.setColour (fundo);
    g.fillRect (r);

    g.setColour (tinta);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            if (escuro (x, y))
                g.fillRect (r.getX() + (x + quiet) * esc,
                            r.getY() + (y + quiet) * esc, esc, esc);
}

} // namespace tl
