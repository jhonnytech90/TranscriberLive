#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "MessageStore.h"
#include "MessageBus.h"
#include "HttpServer.h"
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
        }

        ~Hub() override
        {
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
            // A porta TCP (web) é exclusiva e decide quem é o "primário". UDP com
            // SO_REUSEADDR deixaria dois processos bindarem a mesma porta e os
            // pacotes iriam só para um deles — por isso o UDP só sobe junto com o web.
            if (! web.isRunning()) web.start (httpPort);

            if (web.isRunning())
            {
                if (! listener.isBound()) listener.start (busPort);
            }
            else if (listener.isBound())
            {
                listener.stop();
            }
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
                    wait (ok ? 400 : 1500);
                }
                else
                {
                    mirroring.store (false);
                    wait (1000);
                }
            }
        }

        void loadSettings()
        {
            auto f = getDataDir().getChildFile ("settings.xml");
            if (auto xml = juce::XmlDocument::parse (f))
            {
                busPort  = xml->getIntAttribute ("busPort",  kDefaultBusPort);
                httpPort = xml->getIntAttribute ("httpPort", kDefaultHttpPort);
            }
        }

        void saveSettings()
        {
            juce::XmlElement xml ("TranscriberLive");
            xml.setAttribute ("busPort", busPort);
            xml.setAttribute ("httpPort", httpPort);
            getDataDir().createDirectory();
            xml.writeTo (getDataDir().getChildFile ("settings.xml"));
        }

        BusListener listener;
        HttpServer  web;
        int busPort = kDefaultBusPort, httpPort = kDefaultHttpPort;
        std::atomic<bool> mirroring { false };
    };
}
