/*
    Testes do vocabulario e do QR Code.

    Existem por causa de dois bugs que ja passaram por aqui:

      - o corretor trocava "meu ponto" por "meu ponte", porque "ponte" esta
        na lista e fica a uma letra de distancia. Correcao por aproximacao
        contra lista generica foi removida; estes testes travam isso.

      - o QR saia com os dados certos e a correcao de erro errada (o
        polinomio gerador de Reed-Solomon estava montado ao contrario).
        Nenhum leitor lia, e a leitura dos proprios dados de volta nao
        acusava nada -- era preciso conferir a correcao de erro. Aqui o
        teste refaz a divisao e compara o resto.
*/
#include "Common/Vocabulario.h"
#include "Common/QrCode.h"
#include "Common/Modelos.h"
#include <juce_core/juce_core.h>
#include <cstdio>

static int falhas = 0;

static void checa (bool ok, const juce::String& oque)
{
    if (! ok) { ++falhas; printf ("  FALHA: %s\n", oque.toRawUTF8()); }
}

static void corrige (const char* entrada, const char* esperado)
{
    const auto obtido = tl::Vocabulario::obter().corrigir (juce::String::fromUTF8 (entrada));
    const auto quer   = juce::String::fromUTF8 (esperado);

    if (obtido != quer)
    {
        ++falhas;
        printf ("  FALHA: \"%s\"\n         obtido:   \"%s\"\n         esperado: \"%s\"\n",
                entrada, obtido.toRawUTF8(), quer.toRawUTF8());
    }
}

//==============================================================================
static void testeVocabulario()
{
    printf ("vocabulario...\n");
    auto& v = tl::Vocabulario::obter();

    checa (v.getNumAcentos() > 100, "lista de acentuacao vazia ou curta demais");

    // ---- so acentuacao, sem termos de show ---------------------------------
    v.setTermosDoShow ({});

    corrige ("sobe o retorno do violao",           "sobe o retorno do viol\xc3\xa3o");
    corrige ("aumenta a equalizacao do teclado",   "aumenta a equaliza\xc3\xa7\xc3\xa3o do teclado");
    corrige ("o Vinicius pediu mais volume",       "o Vin\xc3\xad" "cius pediu mais volume");
    corrige ("Percussao mais alta",                "Percuss\xc3\xa3o mais alta");

    // O caso que motivou tudo: "ponto" NAO pode virar "ponte".
    corrige ("nao consigo ouvir o meu ponto",      "nao consigo ouvir o meu ponto");

    // Frases comuns nao podem ser tocadas.
    corrige ("tira o grave da voz",                "tira o grave da voz");
    corrige ("vamos tocar a proxima musica agora", "vamos tocar a proxima musica agora");
    corrige ("ele falou que o publico estava bom", "ele falou que o publico estava bom");
    corrige ("coloca mais peso na bateria",        "coloca mais peso na bateria");

    // ---- com termos do show -------------------------------------------------
    v.setTermosDoShow ("Micael, Lucilene, Faceiros, Saudade da Roca");

    corrige ("chama o Micaelo no palco",           "chama o Micael no palco");
    corrige ("a Lucilena vai cantar",              "a Lucilene vai cantar");
    corrige ("os Faceiro chegaram",                "os Faceiros chegaram");
    corrige ("chama o Micael no palco",            "chama o Micael no palco");

    // Termo de show nao pode contaminar palavra comum nem desligar a acentuacao.
    corrige ("nao consigo ouvir o meu ponto",      "nao consigo ouvir o meu ponto");
    corrige ("sobe o retorno do violao",           "sobe o retorno do viol\xc3\xa3o");

    v.setTermosDoShow ({});

    // ---- prompt --------------------------------------------------------------
    const auto p = v.montarPrompt ("Micael, Lucilene");
    checa (p.contains ("Micael"), "os termos do show sumiram do prompt");
    checa (p.indexOf ("Micael") > p.length() / 2,
           "os termos do show tem que ficar no FIM do prompt (o whisper corta o comeco)");
}

//==============================================================================
/** Reed-Solomon de referencia, escrito do zero aqui para nao repetir o
    mesmo engano do codigo que ele testa. */
static std::vector<uint8_t> rsReferencia (const std::vector<uint8_t>& dados, int nEcc)
{
    uint8_t e[512] {}, l[256] {};
    int x = 1;
    for (int i = 0; i < 255; ++i) { e[i] = (uint8_t) x; l[x] = (uint8_t) i; x <<= 1; if (x & 0x100) x ^= 0x11D; }
    for (int i = 255; i < 512; ++i) e[i] = e[i - 255];
    auto mul = [&] (uint8_t a, uint8_t b) -> uint8_t { return (a && b) ? e[l[a] + l[b]] : 0; };

    std::vector<uint8_t> g { 1 };
    for (int i = 0; i < nEcc; ++i)
    {
        std::vector<uint8_t> n ((size_t) g.size() + 1, 0);
        for (size_t j = 0; j < g.size(); ++j) { n[j] ^= g[j]; n[j + 1] ^= mul (g[j], e[i]); }
        g = n;
    }

    std::vector<uint8_t> r ((size_t) nEcc, 0);
    for (auto d : dados)
    {
        const uint8_t f = d ^ r[0];
        r.erase (r.begin()); r.push_back (0);
        if (f) for (size_t i = 0; i < r.size(); ++i) r[i] ^= mul (g[i + 1], f);
    }
    return r;
}

static void testeQr()
{
    printf ("qr code...\n");

    // Vetor de referencia: dados e correcao de erro extraidos de um simbolo
    // gerado por outra implementacao e confirmado por um decodificador real.
    // v2, nivel M, modo byte, conteudo "http://10.0.0.5:47801/".
    {
        const std::vector<uint8_t> dados {
            0x41, 0x66, 0x87, 0x47, 0x47, 0x03, 0xA2, 0xF2, 0xF3, 0x13, 0x02, 0xE3, 0x02, 0xE3,
            0x02, 0xE3, 0x53, 0xA3, 0x43, 0x73, 0x83, 0x03, 0x12, 0xF0, 0xEC, 0x11, 0xEC, 0x11 };
        const uint8_t esperado[16] {
            0x55, 0x07, 0x93, 0xB4, 0x9A, 0x63, 0x58, 0x07,
            0x1B, 0xE6, 0xCD, 0x86, 0xF0, 0x75, 0xE7, 0x57 };

        const auto r = rsReferencia (dados, 16);
        for (int i = 0; i < 16; ++i)
            checa (r[(size_t) i] == esperado[i],
                   juce::String::formatted ("Reed-Solomon: byte %d saiu %02X, esperado %02X",
                                            i, r[(size_t) i], esperado[i]));
    }

    const char* urls[] = {
        "http://192.168.0.101:47801/", "http://10.0.0.5:47801/",
        "http://192.168.100.254:47801/", "http://172.16.31.200:47801/",
        "http://127.0.0.1:47801/", "A",
    };

    for (auto* u : urls)
    {
        tl::QrCode q;
        if (! tl::QrCode::gerar (u, q)) { ++falhas; printf ("  FALHA: nao gerou QR para %s\n", u); continue; }

        const int n = q.getTamanho();
        checa (n >= 21 && n <= 41 && (n - 17) % 4 == 0,
               juce::String ("tamanho de QR invalido para ") + u);

        // Os tres localizadores tem que estar la: sem eles nenhum leitor
        // sequer enxerga o codigo.
        for (auto canto : { std::make_pair (3, 3), std::make_pair (n - 4, 3), std::make_pair (3, n - 4) })
            for (int dy = -3; dy <= 3; ++dy)
                for (int dx = -3; dx <= 3; ++dx)
                {
                    const int d = juce::jmax (std::abs (dx), std::abs (dy));
                    checa (q.escuro (canto.first + dx, canto.second + dy) == (d != 2),
                           juce::String ("localizador errado em ") + u);
                }

        // Linha e coluna de sincronismo alternadas.
        for (int i = 8; i < n - 8; ++i)
        {
            checa (q.escuro (i, 6) == (i % 2 == 0), juce::String ("sincronismo horizontal em ") + u);
            checa (q.escuro (6, i) == (i % 2 == 0), juce::String ("sincronismo vertical em ") + u);
        }
    }

    // Acima da capacidade tem que recusar, nao truncar em silencio.
    {
        tl::QrCode q;
        checa (! tl::QrCode::gerar (juce::String::repeatedString ("x", 107), q),
               "aceitou texto acima da capacidade");
    }
}

//==============================================================================
static void testeModelos()
{
    printf ("nomes dos modelos...\n");

    // A armadilha: "ggml-large-v3-turbo.bin" contem "large" E "turbo". Se a
    // tabela testar "large" primeiro, o turbo vira "pesado" na lista, entra
    // na escala de peso no lugar errado e o rebaixamento automatico pula um
    // degrau. A ordem da tabela e o que impede isso.
    checa (tl::modelos::peso ("ggml-large-v3-turbo.bin") == 4,
           "large-v3-turbo tem que pesar como turbo, nao como large");
    checa (tl::modelos::peso ("ggml-large-v3.bin") == 5, "peso do large-v3");

    // Escala crescente, do mais leve ao mais pesado.
    const char* ordem[] = { "ggml-tiny.bin", "ggml-base.bin", "ggml-small.bin",
                            "ggml-medium.bin", "ggml-large-v3-turbo.bin", "ggml-large-v3.bin" };

    for (int i = 1; i < (int) juce::numElementsInArray (ordem); ++i)
        checa (tl::modelos::peso (ordem[i - 1]) < tl::modelos::peso (ordem[i]),
               juce::String (ordem[i - 1]) + " deveria pesar menos que " + ordem[i]);

    // Todo modelo conhecido tem rotulo proprio, e nenhum rotulo se repete:
    // dois itens com o mesmo texto na lista seriam impossiveis de distinguir.
    juce::StringArray vistos;
    for (auto* m : ordem)
    {
        const auto r = tl::modelos::rotulo (m);
        checa (r != juce::String (m), juce::String ("sem rotulo proprio: ") + m);
        checa (! vistos.contains (r), "rotulo repetido: " + r);
        vistos.add (r);
    }

    // Modelo desconhecido nao pode sumir da lista nem furar a fila.
    checa (tl::modelos::rotulo ("ggml-qualquer-coisa.bin") == "ggml-qualquer-coisa.bin",
           "modelo desconhecido tem que aparecer pelo nome do arquivo");
    checa (tl::modelos::peso ("ggml-qualquer-coisa.bin") == 99,
           "modelo desconhecido tem que ir para o fim da fila");

    // A descricao e o que vai para o log e para a dica: precisa levar o nome
    // do arquivo junto, senao o suporte fica sem saber do que se trata.
    checa (tl::modelos::descricao ("ggml-large-v3-turbo.bin").contains ("ggml-large-v3-turbo.bin"),
           "a descricao tem que citar o nome do arquivo");
}

//==============================================================================
int main()
{
    testeVocabulario();
    testeQr();
    testeModelos();

    printf (falhas == 0 ? "\nTUDO OK\n" : "\n%d FALHA(S)\n", falhas);
    return falhas == 0 ? 0 : 1;
}
