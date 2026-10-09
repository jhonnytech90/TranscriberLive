#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
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

        juce::String getWebAddressHint() const
        {
            auto ips = HttpServer::getLocalAddresses();
            if (ips.isEmpty()) return "sem rede";
            juce::String s;
            for (int i = 0; i < juce::jmin (2, ips.size()); ++i)
                s += (i ? "  ou  " : "") + juce::String ("http://") + ips[i] + ":" + juce::String (httpPort);
            return s;
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
            }
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
            }
        }

        void buscarTokenDoPrimario() { guardarResposta (pedir ({})); }

        void falarComPrimario (bool permitir, bool novoToken)
        {
            // Em thread propria: isto sai de um clique na interface, e uma
            // chamada de rede sincrona ali travaria a janela do plugin.
            juce::Thread::launch ([this, permitir, novoToken]
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("escrita", permitir);
                o->setProperty ("novo", novoToken);
                guardarResposta (pedir (juce::JSON::toString (juce::var (o), true)));
            });
        }

        void salvarEscrita (bool permitir)
        {
            escritaSalva = permitir;
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
            }

            web.setEscritaPermitida (escritaSalva);
        }

        void saveSettings()
        {
            juce::XmlElement xml ("TranscriberLive");
            xml.setAttribute ("busPort", busPort);
            xml.setAttribute ("httpPort", httpPort);
            xml.setAttribute ("escritaCelular", escritaSalva);
            getDataDir().createDirectory();
            xml.writeTo (getDataDir().getChildFile ("settings.xml"));
        }

        BusListener listener;
        HttpServer  web;
        int busPort = kDefaultBusPort, httpPort = kDefaultHttpPort;
        std::atomic<bool> mirroring { false };

        bool escritaSalva = true;

        // Copia do que o primario respondeu, quando o servidor nao e nosso.
        mutable juce::CriticalSection tokenRemotoLock;
        juce::String tokenRemoto;
        bool         escritaRemota = true;
    };
}
