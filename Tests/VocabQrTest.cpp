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
#include "Common/HttpServer.h"
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

    corrige ("sobe o retorno do violao",           "sobe o retorno do viol\xc3\xa3o");
    corrige ("aumenta a equalizacao do teclado",   "aumenta a equaliza\xc3\xa7\xc3\xa3o do teclado");
    corrige ("o Vinicius pediu mais volume",       "o Vin\xc3\xad" "cius pediu mais volume");
    corrige ("Percussao mais alta",                "Percuss\xc3\xa3o mais alta");

    // Palavra que NAO esta na lista nao pode ser tocada. A correcao repoe
    // acento em palavra com as mesmas letras, e so isso -- nunca troca uma
    // palavra por outra. Ja houve aqui uma correcao por aproximacao que
    // transformava "meu ponto" em "meu ponte" ("ponte" esta na lista, a uma
    // letra de distancia). Estes casos travam a volta daquele comportamento.
    corrige ("nao consigo ouvir o meu ponto",      "nao consigo ouvir o meu ponto");
    corrige ("tira o grave da voz",                "tira o grave da voz");
    corrige ("vamos tocar a proxima musica agora", "vamos tocar a proxima musica agora");
    corrige ("ele falou que o publico estava bom", "ele falou que o publico estava bom");
    corrige ("coloca mais peso na bateria",        "coloca mais peso na bateria");

    // ---- prompt --------------------------------------------------------------
    const auto p = v.montarPrompt();
    checa (p.isNotEmpty(), "o prompt de dominio ficou vazio");
    checa (p.length() < 400, "prompt longo demais: o whisper corta em 224 tokens e recalcula a cada frase");
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
/*  Escrita pelos celulares.

    Estes testes sobem um servidor de verdade e falam com ele por socket, em
    vez de chamar as funcoes direto. E de proposito: os dois pontos que podem
    quebrar aqui so aparecem no fio.

      1. O laco de leitura original parava no fim dos CABECALHOS. Para um POST
         isso deixa o corpo para tras -- e pior, um celular costuma mandar
         cabecalhos e corpo em pacotes separados, entao o corpo chega vazio
         justamente na maquina do cliente e nunca na de quem testou.

      2. /token so pode responder a 127.0.0.1. Se um dia responder a qualquer
         um, o codigo do QR deixa de proteger qualquer coisa -- e nada no
         comportamento visivel denunciaria isso.                              */

static juce::String falarComServidor (int porta, const juce::String& requisicao,
                                      bool corpoEmPacoteSeparado = false)
{
    juce::StreamingSocket s;

    if (! s.connect ("127.0.0.1", porta, 2000))
        return "SEM-CONEXAO";

    const auto cabecalhos = requisicao.upToFirstOccurrenceOf ("\r\n\r\n", true, false);
    const auto corpo      = requisicao.fromFirstOccurrenceOf ("\r\n\r\n", false, false);

    if (corpoEmPacoteSeparado && corpo.isNotEmpty())
    {
        s.write (cabecalhos.toRawUTF8(), (int) cabecalhos.getNumBytesAsUTF8());
        juce::Thread::sleep (60);          // obriga o servidor a ler em duas vezes
        s.write (corpo.toRawUTF8(), (int) corpo.getNumBytesAsUTF8());
    }
    else
    {
        s.write (requisicao.toRawUTF8(), (int) requisicao.getNumBytesAsUTF8());
    }

    juce::String resposta;
    char buf[4096];
    const auto limite = juce::Time::getMillisecondCounter() + 3000;

    while (juce::Time::getMillisecondCounter() < limite)
    {
        if (s.waitUntilReady (true, 200) != 1) continue;
        const int n = s.read (buf, (int) sizeof (buf) - 1, false);
        if (n <= 0) break;
        buf[n] = 0;
        resposta += juce::String::fromUTF8 (buf, n);
    }

    s.close();
    return resposta;
}

static juce::String postSay (int porta, const juce::String& json, bool separado = false)
{
    return falarComServidor (porta,
        "POST /say HTTP/1.1\r\nHost: x\r\nContent-Type: application/json\r\nContent-Length: "
        + juce::String ((int) json.getNumBytesAsUTF8()) + "\r\nConnection: close\r\n\r\n" + json,
        separado);
}

static void testeEscritaWeb()
{
    printf ("escrita pelos celulares...\n");

    tl::MessageStore store;
    const char vazio[] = "<html></html>";
    tl::HttpServer web (store, vazio, (int) sizeof (vazio) - 1);

    // Porta alta e fora da faixa do produto, para nao brigar com um Display
    // aberto na maquina de quem roda o teste.
    int porta = 0;
    for (int tentativa = 0; tentativa < 20 && porta == 0; ++tentativa)
        if (const int p = 49200 + tentativa; web.start (p))
            porta = p;

    if (porta == 0) { ++falhas; printf ("  FALHA: nao consegui subir o servidor de teste\n"); return; }

    const auto token = web.getToken();
    checa (token.length() == 8, "o codigo de escrita deveria ter 8 caracteres");
    checa (! token.containsAnyOf ("AEIOU01lI"),
           "o codigo nao pode ter caracteres que se confundem ao ler em voz alta");

    auto corpoDe = [] (const juce::String& r) { return r.fromFirstOccurrenceOf ("\r\n\r\n", false, false); };
    auto statusDe = [] (const juce::String& r) { return r.upToFirstOccurrenceOf ("\r\n", false, false); };

    // ---- codigo certo: entra ------------------------------------------------
    {
        const auto r = postSay (porta, "{\"k\":\"" + token + "\",\"id\":\"abc\",\"name\":\"Producao\","
                                       "\"color\":\"4fc3f7\",\"text\":\"corta o delay\"}");
        checa (statusDe (r).contains ("200"), "mensagem com codigo certo deveria ser aceita: " + statusDe (r));

        const auto e = store.getEntries();
        checa (e.size() == 1, "a mensagem nao chegou no historico");

        if (! e.empty())
        {
            checa (e[0].text == "corta o delay", "o texto chegou errado: " + e[0].text);
            checa (e[0].name == "Producao", "o nome chegou errado: " + e[0].name);

            // O prefixo e o que separa um celular de um canal de audio na
            // barra de participantes, e o que impede um aparelho de se passar
            // por um Receiver.
            checa (e[0].channelId.startsWith ("web-"), "faltou o prefixo web- no id: " + e[0].channelId);
        }
    }

    // ---- corpo em pacote separado: tem que funcionar igual ------------------
    {
        const auto r = postSay (porta, "{\"k\":\"" + token + "\",\"id\":\"abc\",\"name\":\"Producao\","
                                       "\"color\":\"4fc3f7\",\"text\":\"sobe o retorno\"}", true);
        checa (statusDe (r).contains ("200"), "POST com corpo em pacote separado foi recusado: " + statusDe (r));
        checa (store.getEntries().size() == 2, "a mensagem em dois pacotes nao chegou");
    }

    // ---- codigo errado: barrado ---------------------------------------------
    {
        const auto r = postSay (porta, "{\"k\":\"XXXXXXXX\",\"id\":\"z\",\"name\":\"Intruso\",\"text\":\"oi\"}");
        checa (statusDe (r).contains ("403"), "codigo errado deveria ser recusado: " + statusDe (r));
        checa (store.getEntries().size() == 2, "mensagem com codigo errado entrou no historico");
    }

    // ---- texto vazio: nao vira mensagem --------------------------------------
    {
        const auto r = postSay (porta, "{\"k\":\"" + token + "\",\"id\":\"abc\",\"text\":\"   \"}");
        checa (statusDe (r).contains ("400"), "texto vazio deveria ser recusado");
        checa (store.getEntries().size() == 2, "texto vazio virou mensagem");
    }

    // ---- chave geral desligada: nem com o codigo certo -----------------------
    {
        web.setEscritaPermitida (false);
        const auto r = postSay (porta, "{\"k\":\"" + token + "\",\"id\":\"abc\",\"text\":\"nao devia entrar\"}");
        checa (statusDe (r).contains ("403"), "com a escrita desligada, nem o codigo certo passa");
        checa (store.getEntries().size() == 2, "entrou mensagem com a escrita desligada");
        web.setEscritaPermitida (true);
    }

    // ---- /token pela loopback: responde --------------------------------------
    {
        const auto r = falarComServidor (porta, "GET /token HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
        checa (statusDe (r).contains ("200"), "/token deveria responder a 127.0.0.1");
        checa (corpoDe (r).contains (token), "/token nao devolveu o codigo");
    }

    // ---- trocar o codigo invalida quem ja entrou ------------------------------
    {
        const auto antigo = token;
        web.sortearToken();
        checa (web.getToken() != antigo, "sortearToken nao trocou o codigo");

        const auto r = postSay (porta, "{\"k\":\"" + antigo + "\",\"id\":\"abc\",\"text\":\"codigo velho\"}");
        checa (statusDe (r).contains ("403"), "o codigo antigo deveria ter parado de valer");
    }

    web.stop();
}

//==============================================================================
int main()
{
    testeVocabulario();
    testeQr();
    testeModelos();
    testeEscritaWeb();

    printf (falhas == 0 ? "\nTUDO OK\n" : "\n%d FALHA(S)\n", falhas);
    return falhas == 0 ? 0 : 1;
}
