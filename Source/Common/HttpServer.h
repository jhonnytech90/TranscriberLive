#pragma once

#include <juce_core/juce_core.h>
#include "MessageStore.h"
#include "SocketSafety.h"
#include "Log.h"

namespace tl
{
    //==============================================================================
    /**
        Mini servidor HTTP embutido no Display para celular/tablet na rede local.

          GET  /         página HTML (conversa ao vivo)
          GET  /state    JSON com participantes + histórico
          GET  /events   Server-Sent Events: msg / clear / flash / participants
          POST /clear    limpa o histórico
          POST /say      mensagem digitada num celular/tablet
          GET  /token    o código de escrita — SÓ para quem chama de 127.0.0.1

        Sobre /token e o código de escrita
        ----------------------------------
        Qualquer pessoa no Wi-Fi do local alcança este servidor. Sem nenhuma
        barreira, qualquer uma delas escreveria no telão do cantor no meio do
        show. O código resolve isso sem atrapalhar ninguém: ele vai dentro do
        QR Code, então quem aponta a câmera já entra podendo escrever, e quem
        só descobriu o endereço vê a conversa mas não escreve.

        /token existe porque quem desenha o QR é o Receiver, que pode estar
        em OUTRO processo na mesma máquina (o app do Display costuma ser o
        primário). Ele responde só para 127.0.0.1: um processo da mesma
        máquina lê o código, um celular na rede não. Se /token respondesse a
        qualquer um, o código não valeria nada.
    */
    class HttpServer : private juce::Thread
    {
    public:
        HttpServer (MessageStore& s, const char* html, int htmlSize)
            : juce::Thread ("TranscriberLive HTTP"), store (s), pageHtml (html), pageHtmlSize (htmlSize)
        {
            sortearToken();
        }

        //-- escrita pelos celulares -------------------------------------------------
        /** Chave geral. Desligada, nem o código do QR deixa escrever. */
        void setEscritaPermitida (bool p) noexcept
        {
            if (escritaPermitida.exchange (p) != p)
                TL_LOGI ("web", juce::String ("escrita pelos celulares: ") + (p ? "LIGADA" : "desligada"));
        }
        bool isEscritaPermitida() const noexcept   { return escritaPermitida.load(); }

        juce::String getToken() const { const juce::ScopedLock sl (tokenLock); return token; }

        /** Novo código: invalida os aparelhos que já tinham entrado. Serve
            para cortar o acesso de alguém sem desligar a função para todos. */
        void sortearToken()
        {
            const juce::ScopedLock sl (tokenLock);
            auto& r = juce::Random::getSystemRandom();
            token.clear();

            // Sem vogais e sem 0/O/1/I/l: o código as vezes e lido em voz alta
            // ou digitado a mao, e esses caracteres se confundem.
            static const char* alfabeto = "23456789BCDFGHJKLMNPQRSTVWXZ";
            const int n = (int) strlen (alfabeto);

            for (int i = 0; i < 8; ++i)
                token << alfabeto[r.nextInt (n)];
        }

        ~HttpServer() override { stop(); }

        bool start (int port)
        {
            stop();
            ignoreSigpipeOnce();
            listener = std::make_unique<juce::StreamingSocket>();
            if (! listener->createListener (port, "0.0.0.0"))
            {
                listener.reset();
                return false;
            }
            makeSocketSafe (listener->getRawSocketHandle());
            boundPort = port;
            running.store (true);
            startThread();
            return true;
        }

        void stop()
        {
            signalThreadShouldExit();
            if (listener != nullptr) listener->close();
            stopThread (3000);
            {
                const juce::ScopedLock sl (connLock);
                for (auto* c : connections) c->signalThreadShouldExit();
            }
            connections.clear();   // OwnedArray: destrói (cada thread faz stopThread no destrutor)
            listener.reset();
            running.store (false);
        }

        bool isRunning() const noexcept { return running.load(); }
        int  getPort()   const noexcept { return boundPort; }
        int  getNumClients() const { const juce::ScopedLock sl (connLock); return connections.size(); }

        /** IPs locais (para mostrar "abra http://x.x.x.x:porta" na UI). */
        static juce::StringArray getLocalAddresses()
        {
            static juce::CriticalSection lock;
            static juce::StringArray cached;
            static juce::uint32 cachedAt = 0;

            const juce::ScopedLock sl (lock);
            const auto now = juce::Time::getMillisecondCounter();
            if (cached.isEmpty() || now - cachedAt > 5000)
            {
                cached.clear();
                for (auto& a : juce::IPAddress::getAllAddresses (false))
                    if (a != juce::IPAddress::local() && ! a.isIPv6 && ! a.toString().startsWith ("169.254"))
                        cached.add (a.toString());
                cachedAt = now;
            }
            return cached;
        }

    private:
        //==========================================================================
        class Connection : public juce::Thread
        {
        public:
            Connection (HttpServer& s, juce::StreamingSocket* sock)
                : juce::Thread ("TranscriberLive HTTP conn"), server (s), socket (sock)
            {
                makeSocketSafe (socket->getRawSocketHandle());
                startThread();
            }

            ~Connection() override { socket->close(); stopThread (2000); }

            void run() override
            {
                juce::String request;
                char buf[4096];

                // lê até o fim dos headers (ou timeout)
                const auto deadline = juce::Time::getMillisecondCounter() + 3000;
                while (! request.contains ("\r\n\r\n") && juce::Time::getMillisecondCounter() < deadline && ! threadShouldExit())
                {
                    if (socket->waitUntilReady (true, 200) != 1) continue;
                    const int n = socket->read (buf, (int) sizeof (buf) - 1, false);
                    if (n <= 0) break;
                    buf[n] = 0;
                    request += juce::String::fromUTF8 (buf, n);
                }

                const auto firstLine = request.upToFirstOccurrenceOf ("\r\n", false, false);
                const auto method = firstLine.upToFirstOccurrenceOf (" ", false, false);
                auto path = firstLine.fromFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf (" ", false, false);
                path = path.upToFirstOccurrenceOf ("?", false, false);

                if (path == "/events")            serveEvents();
                else if (path == "/state")        sendResponse ("200 OK", "application/json; charset=utf-8", stateJson());
                else if (path == "/clear" && method == "POST") { server.store.clear(); sendResponse ("200 OK", "text/plain", "ok"); }
                else if (path == "/say"   && method == "POST") atenderSay (lerCorpo (request, deadline));
                else if (path == "/token")                     atenderToken (method, lerCorpo (request, deadline));
                else if (path == "/" || path == "/index.html")
                    sendResponse ("200 OK", "text/html; charset=utf-8", juce::String::fromUTF8 (server.pageHtml, server.pageHtmlSize));
                else
                    sendResponse ("404 Not Found", "text/plain", "not found");

                socket->close();
                finished.store (true);
            }

            bool isFinished() const noexcept { return finished.load(); }

        private:
            /** O laco de cima para no fim dos CABECALHOS. Para um POST ainda
                falta o corpo: le mais ate completar o Content-Length. Sem
                isto o JSON chegaria cortado -- ou vazio, se o celular mandar
                cabecalhos e corpo em pacotes separados, que e o normal. */
            juce::String lerCorpo (const juce::String& request, juce::uint32 deadline)
            {
                const auto cabecalhos = request.upToFirstOccurrenceOf ("\r\n\r\n", false, false);
                auto corpo = request.fromFirstOccurrenceOf ("\r\n\r\n", false, false);

                int esperado = 0;
                for (auto& linha : juce::StringArray::fromLines (cabecalhos))
                    if (linha.startsWithIgnoreCase ("content-length:"))
                        esperado = linha.fromFirstOccurrenceOf (":", false, false).trim().getIntValue();

                // Teto: o corpo e uma frase digitada no celular, nunca um
                // arquivo. Sem teto, uma requisicao mentindo no Content-Length
                // prenderia esta thread ate o tempo acabar.
                if (esperado <= 0 || esperado > 8192)
                    return corpo;

                char buf[2048];
                while ((int) corpo.getNumBytesAsUTF8() < esperado
                       && juce::Time::getMillisecondCounter() < deadline
                       && ! threadShouldExit())
                {
                    if (socket->waitUntilReady (true, 200) != 1) continue;
                    const int n = socket->read (buf, (int) sizeof (buf) - 1, false);
                    if (n <= 0) break;
                    buf[n] = 0;
                    corpo += juce::String::fromUTF8 (buf, n);
                }

                return corpo;
            }

            /** So para quem chama da propria maquina. O Receiver precisa do
                codigo para desenhar o QR; um celular na rede nao pode te-lo
                de graca, senao o codigo nao protege nada. */
            void atenderToken (const juce::String& method, const juce::String& corpo)
            {
                const auto quem = socket->getHostName();

                if (quem != "127.0.0.1" && quem != "::1" && quem != "localhost")
                {
                    TL_LOGW ("web", "pedido de /token recusado, veio de " + quem);
                    sendResponse ("403 Forbidden", "text/plain", "nao");
                    return;
                }

                // POST: o Receiver manda a chave geral e pede codigo novo.
                // Mesma porta de entrada do GET, e pela mesma razao: so a
                // propria maquina pode mexer nisto.
                if (method == "POST")
                {
                    if (auto* in = juce::JSON::parse (corpo).getDynamicObject())
                    {
                        if (in->hasProperty ("escrita"))
                            server.setEscritaPermitida ((bool) in->getProperty ("escrita"));

                        if ((bool) in->getProperty ("novo"))
                            server.sortearToken();
                    }
                }

                auto* o = new juce::DynamicObject();
                o->setProperty ("token", server.getToken());
                o->setProperty ("escrita", server.isEscritaPermitida());
                sendResponse ("200 OK", "application/json; charset=utf-8",
                              juce::JSON::toString (juce::var (o), true));
            }

            void atenderSay (const juce::String& corpo)
            {
                if (! server.isEscritaPermitida())
                {
                    sendResponse ("403 Forbidden", "text/plain", "escrita desligada no Receiver");
                    return;
                }

                auto v = juce::JSON::parse (corpo);
                auto* o = v.getDynamicObject();

                if (o == nullptr)
                {
                    sendResponse ("400 Bad Request", "text/plain", "json invalido");
                    return;
                }

                if (o->getProperty ("k").toString() != server.getToken())
                {
                    TL_LOGW ("web", "mensagem recusada: codigo de escrita errado (de "
                                    + socket->getHostName() + ")");
                    sendResponse ("403 Forbidden", "text/plain", "codigo invalido");
                    return;
                }

                const auto texto = o->getProperty ("text").toString().trim();

                if (texto.isEmpty())
                {
                    sendResponse ("400 Bad Request", "text/plain", "texto vazio");
                    return;
                }

                Message m;
                m.type   = "msg";

                // O id vem do aparelho e leva prefixo "web-": e o que separa
                // um celular de um canal de audio na barra de participantes,
                // e o que impede um aparelho de se passar por um Receiver.
                const auto id = o->getProperty ("id").toString().retainCharacters (
                    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-");

                m.channelId   = "web-" + (id.isNotEmpty() ? id.substring (0, 36) : juce::String ("anonimo"));
                m.name        = o->getProperty ("name").toString().trim().substring (0, 24);
                if (m.name.isEmpty()) m.name = "Celular";

                m.colour      = juce::Colour::fromString ("ff" + o->getProperty ("color").toString()
                                                                  .retainCharacters ("0123456789abcdefABCDEF")
                                                                  .paddedLeft ('0', 6).substring (0, 6));
                m.importance  = juce::jlimit (1, 3, (int) o->getProperty ("imp"));
                m.flash       = true;

                // Cada envio e uma fala fechada: id unico, para o Display nao
                // tratar a proxima mensagem como correcao desta.
                m.utteranceId = juce::Uuid().toDashedString();
                m.text        = texto.substring (0, 500);
                m.isFinal     = true;
                m.timeMs      = juce::Time::currentTimeMillis();

                server.store.handle (m);
                TL_LOGI ("web", "mensagem de " + m.name + ": " + m.text);

                sendResponse ("200 OK", "text/plain", "ok");
            }

            juce::String stateJson() const
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("participants", server.store.participantsToVar());
                juce::Array<juce::var> arr;
                for (auto& e : server.store.getEntries()) arr.add (MessageStore::entryToVar (e));
                o->setProperty ("entries", juce::var (arr));
                o->setProperty ("lastSeq", server.store.getLastSeq());
                o->setProperty ("clearSeq", server.store.getClearSeq());
                return juce::JSON::toString (juce::var (o), true);
            }

            void sendResponse (const char* status, const char* contentType, const juce::String& body)
            {
                juce::MemoryOutputStream out;
                out << "HTTP/1.1 " << status << "\r\n"
                    << "Content-Type: " << contentType << "\r\n"
                    << "Content-Length: " << (int) body.getNumBytesAsUTF8() << "\r\n"
                    << "Access-Control-Allow-Origin: *\r\n"
                    << "Cache-Control: no-store\r\n"
                    << "Connection: close\r\n\r\n"
                    << body;
                socket->write (out.getData(), (int) out.getDataSize());
            }

            bool writeStr (const juce::String& s)
            {
                if (! socket->isConnected()) return false;
                const int n = (int) s.getNumBytesAsUTF8();
                return socket->write (s.toRawUTF8(), n) == n;
            }

            void serveEvents()
            {
                writeStr ("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-store\r\n"
                          "Access-Control-Allow-Origin: *\r\nConnection: keep-alive\r\n\r\n");
                writeStr (": connected\n\n");

                auto& store = server.store;
                juce::int64 lastSeq = store.getLastSeq();   // o cliente já buscou /state; só o que vier depois
                juce::int64 lastClear = store.getClearSeq();
                juce::int64 lastFlash = 0;
                juce::uint32 lastParticipants = 0, lastPing = juce::Time::getMillisecondCounter();

                while (! threadShouldExit())
                {
                    if (! socket->isConnected()) break;

                    const auto clearSeq = store.getClearSeq();
                    if (clearSeq != lastClear)
                    {
                        lastClear = clearSeq;
                        if (! writeStr ("data: {\"type\":\"clear\"}\n\n")) break;
                    }

                    for (auto& e : store.getEntriesSince (lastSeq))
                    {
                        auto v = MessageStore::entryToVar (e);
                        v.getDynamicObject()->setProperty ("type", "msg");
                        if (! writeStr ("data: " + juce::JSON::toString (v, true) + "\n\n")) return;
                        lastSeq = juce::jmax (lastSeq, e.seq);
                    }

                    const auto flash = store.getFlashSince (lastFlash);
                    if (flash.seq > 0)
                    {
                        lastFlash = flash.seq;
                        lastSeq = juce::jmax (lastSeq, flash.seq);
                        writeStr ("data: {\"type\":\"flash\",\"color\":\"" + flash.colour.toDisplayString (false)
                                  + "\",\"imp\":" + juce::String (flash.importance) + "}\n\n");
                    }

                    const auto now = juce::Time::getMillisecondCounter();
                    if (now - lastParticipants > 2000)
                    {
                        lastParticipants = now;
                        auto* o = new juce::DynamicObject();
                        o->setProperty ("type", "participants");
                        o->setProperty ("list", store.participantsToVar());
                        if (! writeStr ("data: " + juce::JSON::toString (juce::var (o), true) + "\n\n")) break;
                    }
                    if (now - lastPing > 15000) { lastPing = now; if (! writeStr (": ping\n\n")) break; }

                    wait (100);
                }
            }

            HttpServer& server;
            std::unique_ptr<juce::StreamingSocket> socket;
            std::atomic<bool> finished { false };
        };

        //==========================================================================
        void run() override
        {
            while (! threadShouldExit())
            {
                if (listener->waitUntilReady (true, 200) != 1) { reap(); continue; }

                if (auto* s = listener->waitForNextConnection())
                {
                    const juce::ScopedLock sl (connLock);
                    connections.add (new Connection (*this, s));
                }
                reap();
            }
        }

        void reap()
        {
            const juce::ScopedLock sl (connLock);
            for (int i = connections.size(); --i >= 0;)
                if (connections[i]->isFinished())
                    connections.remove (i);
        }

        MessageStore& store;
        const char* pageHtml;
        int pageHtmlSize;
        std::unique_ptr<juce::StreamingSocket> listener;
        mutable juce::CriticalSection connLock;
        juce::OwnedArray<Connection> connections;
        std::atomic<bool> running { false };

        // O codigo de escrita fica atras de lock porque sortearToken() pode
        // ser chamado da UI enquanto uma thread de conexao esta validando.
        mutable juce::CriticalSection tokenLock;
        juce::String      token;
        std::atomic<bool> escritaPermitida { true };
        int boundPort = 0;
    };
}
