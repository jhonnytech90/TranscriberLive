#pragma once

#include <juce_core/juce_core.h>
#include "MessageStore.h"

namespace tl
{
    //==============================================================================
    /**
        Mini servidor HTTP embutido no Display para celular/tablet na rede local.

          GET  /         página HTML (conversa ao vivo)
          GET  /state    JSON com participantes + histórico
          GET  /events   Server-Sent Events: msg / clear / flash / participants
          POST /clear    limpa o histórico
    */
    class HttpServer : private juce::Thread
    {
    public:
        HttpServer (MessageStore& s, const char* html, int htmlSize)
            : juce::Thread ("TranscriberLive HTTP"), store (s), pageHtml (html), pageHtmlSize (htmlSize) {}

        ~HttpServer() override { stop(); }

        bool start (int port)
        {
            stop();
            listener = std::make_unique<juce::StreamingSocket>();
            if (! listener->createListener (port, "0.0.0.0"))
            {
                listener.reset();
                return false;
            }
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
            juce::StringArray out;
            for (auto& a : juce::IPAddress::getAllAddresses (false))
                if (a != juce::IPAddress::local() && ! a.isIPv6 && ! a.toString().startsWith ("169.254"))
                    out.add (a.toString());
            return out;
        }

    private:
        //==========================================================================
        class Connection : public juce::Thread
        {
        public:
            Connection (HttpServer& s, juce::StreamingSocket* sock)
                : juce::Thread ("TranscriberLive HTTP conn"), server (s), socket (sock) { startThread(); }

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
                else if (path == "/" || path == "/index.html")
                    sendResponse ("200 OK", "text/html; charset=utf-8", juce::String::fromUTF8 (server.pageHtml, server.pageHtmlSize));
                else
                    sendResponse ("404 Not Found", "text/plain", "not found");

                socket->close();
                finished.store (true);
            }

            bool isFinished() const noexcept { return finished.load(); }

        private:
            juce::String stateJson() const
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("participants", server.store.participantsToVar());
                juce::Array<juce::var> arr;
                for (auto& e : server.store.getEntries()) arr.add (MessageStore::entryToVar (e));
                o->setProperty ("entries", juce::var (arr));
                o->setProperty ("lastSeq", server.store.getLastSeq());
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
                return socket->write (s.toRawUTF8(), (int) s.getNumBytesAsUTF8()) >= 0;
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
        int boundPort = 0;
    };
}
