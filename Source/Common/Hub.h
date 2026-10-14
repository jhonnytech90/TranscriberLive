#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "EnderecoParceiro.h"
#include "MessageStore.h"
#include "MessageBus.h"
#include "HttpServer.h"
#include "Log.h"
#include <map>

namespace tl
{
    //==============================================================================
    /**
        Hub compartilhado por TODAS as instâncias (Receiver e Display) dentro do
        mesmo processo (juce::SharedResourcePointer<Hub>).

        - Assim que qualquer parte da ferramenta abre, o hub tenta subir o listener
          UDP (Receivers -> aqui) e o servidor web (celular/tablet). Não precisa do
          Display aberto para o celular funcionar.
        - Se outro processo já tem as portas (ex.: o app Display aberto ao lado do
          host), este hub vira "secundário": tenta de novo a cada 3 s e, enquanto
          isso, espelha o estado do primário via HTTP (127.0.0.1:porta/state) para
          que um Display neste processo continue mostrando tudo.
        - Portas ficam num arquivo de configuração comum a todos os processos.
    */
    class Hub : private juce::Timer,
                private juce::Thread
    {
    public:
        Hub()
            : juce::Thread ("TranscriberLive Hub sync"),
              listener ([this] (const Message& m) { store.handle (m); }),
              web (store, pageHtml(), pageHtmlSize())
        {
            loadSettings();
            tryStart();
            startTimer (3000);
            startThread();

            TL_LOGI ("hub", juce::String::formatted (
                "iniciado: porta bus=%d web=%d  |  papel=%s", busPort, httpPort,
                isPrimary() ? "PRIMARIO (servidores nossos)" : "secundario (espelhando outro processo)"));
        }

        ~Hub() override
        {
            TL_LOGI ("hub", "encerrando");
            stopTimer();
            stopThread (3000);
            web.stop();
            listener.stop();
        }

        MessageStore store;

        //-- estado ----------------------------------------------------------------
        int  getBusPort() const noexcept   { return busPort; }
        int  getHttpPort() const noexcept  { return httpPort; }
        bool isBusPrimary() const          { return listener.isBound(); }
        bool isWebPrimary() const          { return web.isRunning(); }
        bool isPrimary() const             { return web.isRunning(); }
        int  getWebClients() const         { return web.getNumClients(); }
        bool isMirroring() const noexcept  { return mirroring.load(); }

        //-- maquina parceira ---------------------------------------------------------
        /*  Outro computador da mesma rede cujas mensagens aparecem aqui tambem.

            Nasceu do tecnico de P.A. conversando com o de monitor: cada um num
            PC, cada um com o seu Receiver. Cada lado aponta para o outro e os
            dois passam a ver a conversa inteira.

            E PUXADO, nao empurrado: este Display busca o estado de la. Assim so
            quem quer ver precisa configurar -- nao e preciso mexer na maquina
            do outro tecnico, que pode nem estar por perto na hora.           */
        juce::String getParceiro() const
        {
            const juce::ScopedLock sl (parceiroLock);
            return parceiro;
        }

        void setParceiro (const juce::String& host)
        {
            const auto limpo = host.trim();
            {
                const juce::ScopedLock sl (parceiroLock);
                if (limpo == parceiro) return;
                parceiro = limpo;
                parceiroOnline.store (false);
            }
            TL_LOGI ("hub", limpo.isEmpty() ? juce::String ("sem maquina parceira")
                                            : "maquina parceira: " + limpo);
            saveSettings();
        }

        /** true quando a parceira respondeu na ultima tentativa. */
        bool isParceiroOnline() const noexcept { return parceiroOnline.load(); }

        /*  Completa o endereco da parceira com a porta.

            O usuario pode digitar so o IP ou "ip:porta". As duas maquinas quase
            sempre ficam na mesma porta web, mas nao e garantido: se uma delas
            achou a porta ocupada, ela subiu para a seguinte. Por isso a porta
            digitada manda, e a nossa so entra quando ele nao digitou nenhuma. */
        juce::String comPorta (const juce::String& host) const
        {
            return completarPorta (host, httpPort);
        }

        /*  UM endereco, o melhor da lista -- nao dois.

            Mostrar "A ou B" parecia gentil e nao era: o Receiver desenhava o QR
            com o primeiro da lista e o Display anunciava os dois, entao a mesma
            maquina exibia enderecos diferentes em duas janelas e o cliente
            concluia, com razao, que havia algo errado. Agora os dois perguntam
            a mesma funcao e dizem a mesma coisa.

            Quem tem varias redes troca pelo botao no painel do QR.           */
        juce::String getWebAddressHint() const
        {
            const auto ip = HttpServer::melhorEndereco();
            if (ip.isEmpty()) return "sem rede";
            return "http://" + ip + ":" + juce::String (httpPort);
        }

        //-- escrita pelos celulares ------------------------------------------------
        /*  Estes quatro metodos existem porque o servidor web pode nao estar
            NESTE processo. Quando o app do Display esta aberto ao lado, e ele
            quem segurou a porta, e o Receiver vira secundario -- mas quem
            desenha o QR Code (e quem tem a chave geral na interface) e o
            Receiver. Entao: se somos o primario, mexemos direto; se nao,
            falamos com o primario por 127.0.0.1, que e o unico endereco que
            /token aceita.                                                    */

        /** Codigo que vai dentro do QR. Vazio = nao consegui falar com o
            primario; quem chama decide o que mostrar. */
        juce::String getTokenEscrita() const
        {
            if (isWebPrimary())
                return web.getToken();

            const juce::ScopedLock sl (tokenRemotoLock);
            return tokenRemoto;
        }

        bool isEscritaPermitida() const
        {
            if (isWebPrimary())
                return web.isEscritaPermitida();

            const juce::ScopedLock sl (tokenRemotoLock);
            return escritaRemota;
        }

        void setEscritaPermitida (bool permitir)
        {
            if (isWebPrimary())
            {
                web.setEscritaPermitida (permitir);
                salvarEscrita (permitir);
                return;
            }

            falarComPrimario (permitir, false);
            salvarEscrita (permitir);
        }

        /*  Exigir o codigo do QR para escrever.

            Desligado, quem digitar o endereco escreve. E o caminho do tecnico
            de P.A. falando com o de monitor: os dois estao em PC, e PC nao tem
            camera para ler QR Code.                                          */
        bool isExigirCodigo() const
        {
            if (isWebPrimary()) return web.isExigirCodigo();
            const juce::ScopedLock sl (tokenRemotoLock);
            return exigirRemoto;
        }

        void setExigirCodigo (bool exigir)
        {
            if (isWebPrimary())
            {
                web.setExigirCodigo (exigir);
                salvarExigir (exigir);
                return;
            }

            falarComPrimario (web.isEscritaPermitida(), false, &exigir);
            salvarExigir (exigir);
        }

        /** Sorteia um codigo novo: os aparelhos que ja entraram param de
            escrever ate lerem o QR de novo. E o "corta o acesso daquele
            sujeito" sem desligar a funcao para todo mundo. */
        void trocarTokenEscrita()
        {
            if (isWebPrimary())
            {
                web.sortearToken();
                TL_LOGI ("hub", "codigo de escrita trocado");
                return;
            }

            falarComPrimario (isEscritaPermitida(), true);
        }

        void setPorts (int newBusPort, int newHttpPort)
        {
            busPort  = newBusPort  > 0 ? newBusPort  : kDefaultBusPort;
            httpPort = newHttpPort > 0 ? newHttpPort : kDefaultHttpPort;
            saveSettings();
            web.stop();
            listener.stop();
            tryStart();
        }

        /** Limpa o histórico aqui e, se outro processo é o primário, lá também. */
        void clearEverywhere()
        {
            store.clear();
            if (! isWebPrimary())
                juce::Thread::launch ([port = httpPort]
                {
                    juce::URL url ("http://127.0.0.1:" + juce::String (port) + "/clear");
                    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                    .withConnectionTimeoutMs (1000)
                                    .withHttpRequestCmd ("POST");
                    if (auto in = url.createInputStream (opts)) in->readEntireStreamAsString();
                });
        }

        //-- pasta de dados / modelos ------------------------------------------------
        /** macOS: ~/Library/Application Support/TranscriberLive   Windows: %APPDATA%\TranscriberLive */
        static juce::File getDataDir()
        {
            auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
           #if JUCE_MAC
            base = base.getChildFile ("Application Support");   // no macOS o JUCE devolve só ~/Library
           #endif
            return base.getChildFile ("TranscriberLive");
        }
        static juce::File getModelsDir() { return getDataDir().getChildFile ("models"); }

        /** Todas as pastas onde procuramos modelos (a principal + caminhos antigos/alternativos). */
        static juce::Array<juce::File> getModelSearchDirs()
        {
            juce::Array<juce::File> dirs;
            dirs.add (getModelsDir());
            dirs.addIfNotAlreadyThere (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("TranscriberLive").getChildFile ("models"));
            dirs.addIfNotAlreadyThere (juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory).getChildFile ("TranscriberLive").getChildFile ("models"));
           #if JUCE_MAC
            dirs.addIfNotAlreadyThere (juce::File ("/Library/Application Support/TranscriberLive/models"));
           #endif
            dirs.addIfNotAlreadyThere (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("TranscriberLive").getChildFile ("models"));
            return dirs;
        }

        /** Arquivos ggml-*.bin de todas as pastas (nome -> arquivo; a primeira pasta ganha em caso de duplicata). */
        static std::map<juce::String, juce::File> findModelFiles()
        {
            std::map<juce::String, juce::File> out;
            for (auto& d : getModelSearchDirs())
                if (d.isDirectory())
                    for (auto& f : d.findChildFiles (juce::File::findFiles, false, "ggml-*.bin"))
                        if (out.find (f.getFileName()) == out.end())
                            out[f.getFileName()] = f;
            return out;
        }

    private:
        bool redesRegistradas = false;

        static const char* pageHtml();
        static int pageHtmlSize();

        void tryStart()
        {
            const bool eraPrimario = web.isRunning();

            // A porta TCP (web) é exclusiva e decide quem é o "primário". UDP com
            // SO_REUSEADDR deixaria dois processos bindarem a mesma porta e os
            // pacotes iriam só para um deles — por isso o UDP só sobe junto com o web.
            if (! web.isRunning()) web.start (httpPort);

            if (web.isRunning())
            {
                /*  Registra as redes desta maquina assim que viramos primario.

                    O log nao dizia nada sobre rede, e foi exatamente isso que
                    faltou num chamado: o cliente via o QR apontar para um IP
                    publico da Amazon (VPN ligada) e nao havia como saber disso
                    sem estar na frente da maquina. Agora o Log conta.
                    Uma vez por processo: isto nao e para virar enxurrada.   */
                if (! redesRegistradas)
                {
                    redesRegistradas = true;
                    TL_LOGI ("rede", "enderecos desta maquina: " + HttpServer::enderecosParaLog());
                    TL_LOGI ("rede", "o QR vai usar: " + getWebAddressHint());
                }

                if (! listener.isBound())
                {
                    listener.start (busPort);
                    if (! listener.isBound())
                        TL_LOGE ("hub", juce::String::formatted (
                            "nao consegui abrir a porta UDP %d -- os Receivers nao vao chegar aqui "
                            "(firewall ou porta ocupada)", busPort));
                }
            }
            else if (listener.isBound())
            {
                listener.stop();
            }

            // Trocar de papel no meio da sessão explica sozinho vários "sumiu o
            // texto": quem era primário deixou de ser, e as mensagens passaram a
            // ir para outro processo.
            if (eraPrimario != web.isRunning())
                TL_LOGW ("hub", juce::String ("mudou de papel: agora ")
                         + (web.isRunning() ? "PRIMARIO" : "secundario"));
        }

        void timerCallback() override { tryStart(); }

        // thread de espelhamento (modo secundário)
        void run() override
        {
            while (! threadShouldExit())
            {
                if (! web.isRunning())
                {
                    juce::URL url ("http://127.0.0.1:" + juce::String (httpPort) + "/state");
                    auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs (800);
                    juce::String json;
                    if (auto in = url.createInputStream (opts)) json = in->readEntireStreamAsString();

                    const bool ok = json.isNotEmpty();
                    mirroring.store (ok);
                    if (ok) store.applyStateJson (json);

                    // Aproveita a volta para trazer o codigo de escrita do
                    // primario. Fica aqui, e nao na interface, porque e uma
                    // chamada de rede: na thread da UI ela travaria a janela
                    // do plugin toda vez que o primario demorasse.
                    if (ok) buscarTokenDoPrimario();

                    wait (ok ? 400 : 1500);
                }
                else
                {
                    mirroring.store (false);
                    wait (1000);
                }

                buscarDoParceiro();
            }
        }

        /*  Puxa o estado da maquina parceira e junta ao nosso.

            Sem obedecer a limpeza dela (ver applyStateJson): quando o outro
            tecnico limpa a tela dele, a nossa conversa continua aqui.

            Falha calada de proposito: a parceira pode estar desligada no meio
            do show, e isso nao e erro -- a interface mostra que ela esta fora
            e o resto do Display segue funcionando.                           */
        void buscarDoParceiro()
        {
            const auto host = getParceiro();
            if (host.isEmpty()) { parceiroOnline.store (false); return; }

            juce::URL url ("http://" + comPorta (host) + "/state");
            auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                            .withConnectionTimeoutMs (900);

            juce::String json;
            if (auto in = url.createInputStream (opts)) json = in->readEntireStreamAsString();

            const bool ok = json.isNotEmpty();
            parceiroOnline.store (ok);
            if (ok) store.applyStateJson (json, false);
        }

        //-- ponte com o primario (so 127.0.0.1 responde a /token) -----------------
        juce::String pedir (const juce::String& corpoPost) const
        {
            juce::URL url ("http://127.0.0.1:" + juce::String (httpPort) + "/token");

            if (corpoPost.isNotEmpty())
                url = url.withPOSTData (corpoPost);

            auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                            .withConnectionTimeoutMs (800)
                            .withExtraHeaders ("Content-Type: application/json")
                            .withHttpRequestCmd (corpoPost.isNotEmpty() ? "POST" : "GET");

            if (auto in = url.createInputStream (opts))
                return in->readEntireStreamAsString();

            return {};
        }

        void guardarResposta (const juce::String& json)
        {
            if (auto* o = juce::JSON::parse (json).getDynamicObject())
            {
                const juce::ScopedLock sl (tokenRemotoLock);
                tokenRemoto   = o->getProperty ("token").toString();
                escritaRemota = (bool) o->getProperty ("escrita");
                if (o->hasProperty ("exigirCodigo"))
                    exigirRemoto = (bool) o->getProperty ("exigirCodigo");
            }
        }

        void buscarTokenDoPrimario() { guardarResposta (pedir ({})); }

        void falarComPrimario (bool permitir, bool novoToken, const bool* exigir = nullptr)
        {
            // Em thread propria: isto sai de um clique na interface, e uma
            // chamada de rede sincrona ali travaria a janela do plugin.
            // O ponteiro nao sobrevive a volta desta funcao: a thread so leva
            // o valor, em -1/0/1 (nao mexer / desligar / ligar).
            const int exigirValor = exigir == nullptr ? -1 : (*exigir ? 1 : 0);

            juce::Thread::launch ([this, permitir, novoToken, exigirValor]
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("escrita", permitir);
                o->setProperty ("novo", novoToken);
                if (exigirValor >= 0) o->setProperty ("exigirCodigo", exigirValor == 1);
                guardarResposta (pedir (juce::JSON::toString (juce::var (o), true)));
            });
        }

        void salvarEscrita (bool permitir)
        {
            escritaSalva = permitir;
            saveSettings();
        }

        void salvarExigir (bool exigir)
        {
            exigirSalvo = exigir;
            saveSettings();
        }

        void loadSettings()
        {
            auto f = getDataDir().getChildFile ("settings.xml");
            if (auto xml = juce::XmlDocument::parse (f))
            {
                busPort  = xml->getIntAttribute ("busPort",  kDefaultBusPort);
                httpPort = xml->getIntAttribute ("httpPort", kDefaultHttpPort);
                escritaSalva = xml->getBoolAttribute ("escritaCelular", true);
                // Padrao true: quem atualiza nao ganha o telao aberto para a
                // rede sem ter pedido.
                exigirSalvo  = xml->getBoolAttribute ("exigirCodigo", true);
                { const juce::ScopedLock sl (parceiroLock);
                  parceiro = xml->getStringAttribute ("parceiro", {}).trim(); }
            }

            web.setEscritaPermitida (escritaSalva);
            web.setExigirCodigo (exigirSalvo);
        }

        void saveSettings()
        {
            juce::XmlElement xml ("TranscriberLive");
            xml.setAttribute ("busPort", busPort);
            xml.setAttribute ("httpPort", httpPort);
            xml.setAttribute ("escritaCelular", escritaSalva);
            xml.setAttribute ("exigirCodigo", exigirSalvo);
            xml.setAttribute ("parceiro", getParceiro());
            getDataDir().createDirectory();
            xml.writeTo (getDataDir().getChildFile ("settings.xml"));
        }

        BusListener listener;
        HttpServer  web;
        int busPort = kDefaultBusPort, httpPort = kDefaultHttpPort;
        std::atomic<bool> mirroring { false };
        bool exigirSalvo = true;
        mutable juce::CriticalSection parceiroLock;
        juce::String parceiro;
        std::atomic<bool> parceiroOnline { false };

        bool escritaSalva = true;

        // Copia do que o primario respondeu, quando o servidor nao e nosso.
        mutable juce::CriticalSection tokenRemotoLock;
        juce::String tokenRemoto;
        bool         escritaRemota = true;
        bool exigirRemoto = true;
    };
}
