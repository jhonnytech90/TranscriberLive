#pragma once

#include "Idioma.h"

#include <juce_core/juce_core.h>
#include <algorithm>
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

        Quem abre, escreve
        ------------------
        Não há código nem permissão: quem alcança o endereço participa da
        conversa. Houve um código dentro do QR Code, e ele saiu a pedido do
        Jhonatan — atrapalhava mais do que protegia. Na prática a barreira
        real é a rede: só escreve quem já está no Wi-Fi do local, e quem está
        lá é a equipe. A alternativa custava uma explicação a cada show.

        O que isso significa, dito claramente: numa rede de produção, tudo
        bem. Num Wi-Fi aberto de casa de show, qualquer um que descubra o
        endereço escreve no telão.
    */
    class HttpServer : private juce::Thread
    {
    public:
        HttpServer (MessageStore& s, const char* html, int htmlSize)
            : juce::Thread ("TranscriberLive HTTP"), store (s), pageHtml (html), pageHtmlSize (htmlSize)
        {
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

        /*  Quao bom e este endereco para o celular do pessoal alcancar.
            Menor e melhor.

            Isto existe por um chamado real: a maquina do cliente tinha uma VPN
            ligada, e o primeiro endereco da lista era 54.232.189.113 -- um IP
            PUBLICO da Amazon. O QR apontava para la, o celular tentava abrir um
            servidor que nao existe e ficava na tela preta. O endereco certo,
            192.168.0.6, era o segundo.

            Por que nao perguntar a tabela de rotas qual IP a maquina usa para
            sair: nesse mesmo caso a resposta seria o da VPN, que e quem tem a
            rota padrao. O criterio que vale aqui nao e "por onde eu saio para a
            internet", e "qual endereco meu o celular na mesma sala alcanca" --
            e esse e sempre um endereco de rede local.                        */
        static int notaDoEndereco (const juce::String& ip)
        {
            const bool e192 = ip.startsWith ("192.168.");
            const bool e10  = ip.startsWith ("10.");
            bool e172 = false;
            if (ip.startsWith ("172."))
            {
                const int seg = ip.fromFirstOccurrenceOf (".", false, false).getIntValue();
                e172 = seg >= 16 && seg <= 31;
            }

            // Faixas que existem na maquina mas nao levam a lugar nenhum util:
            // sao redes de maquina virtual, so entre o host e a VM.
            const bool virtual_ =
                   ip.startsWith ("10.211.55.")    // Parallels
                || ip.startsWith ("10.37.129.")    // Parallels
                || ip.startsWith ("192.168.56.")   // VirtualBox (host-only)
                || ip.startsWith ("172.17.")       // Docker
                || ip.startsWith ("172.18.")       // Docker
                || ip.startsWith ("25.");          // Hamachi

            if (virtual_)            return 50;
            if (e192)                return 0;    // a faixa mais comum em casa e em palco
            if (e172)                return 1;
            if (e10)                 return 2;
            if (ip.startsWith ("100.")) return 60;  // CGNAT/Tailscale: nao e a rede da sala
            return 90;                              // publico: VPN, tunel. Nunca e o certo.
        }

        /*  Em que tipo de rede este endereco esta, em portugues simples.

            Serve para o tecnico escolher a rede certa sem saber o que e um
            bloco RFC1918. A mesma nota que ordena a lista vira a explicacao:
            se o endereco esta la em baixo na ordem, e porque dificilmente e a
            rede em que o celular dele esta.                                 */
        static juce::String tipoDaRede (const juce::String& ip)
        {
            switch (notaDoEndereco (ip))
            {
                case 0:  case 1:  case 2:  return tr ("Rede local");
                case 50: return tr ("Rede virtual (VPN ou m\xc3\xa1quina virtual)");
                case 60: return tr ("Rede do provedor (n\xc3\xa3o e a rede da sala)");
                default: return tr ("Endere\xc3\xa7" "o p\xc3\xba" "blico (n\xc3\xa3o e a rede da sala)");
            }
        }

        /** true quando este endereco e de uma rede local de verdade. */
        static bool ehRedeLocal (const juce::String& ip) { return notaDoEndereco (ip) <= 2; }

        /** IPs desta maquina, O MELHOR PRIMEIRO (ver notaDoEndereco). */
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

                // stable_sort: entre dois enderecos igualmente bons, mantem a
                // ordem do sistema, que costuma por a interface ativa antes.
                std::stable_sort (cached.strings.begin(), cached.strings.end(),
                                  [] (const juce::String& a, const juce::String& b)
                                  { return notaDoEndereco (a) < notaDoEndereco (b); });
                cachedAt = now;
            }
            return cached;
        }

        /** O endereco que a interface deve mostrar e o QR deve usar. Vazio se
            a maquina nao tem rede nenhuma. */
        static juce::String melhorEndereco()
        {
            const auto ips = getLocalAddresses();
            return ips.isEmpty() ? juce::String() : ips[0];
        }

        /** "192.168.0.6 (local)  |  54.232.189.113 (publico, descartado)" --
            para o log, que e o que o suporte le quando o cliente reclama. */
        static juce::String enderecosParaLog()
        {
            juce::String s;
            for (auto& ip : getLocalAddresses())
            {
                const int n = notaDoEndereco (ip);
                s << (s.isEmpty() ? "" : "  |  ") << ip << " ("
                  << (n >= 90 ? "publico" : n >= 60 ? "fora da rede local"
                      : n >= 50 ? "maquina virtual" : "rede local") << ")";
            }
            return s.isEmpty() ? juce::String ("nenhum") : s;
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

            void atenderSay (const juce::String& corpo)
            {
                auto v = juce::JSON::parse (corpo);
                auto* o = v.getDynamicObject();

                if (o == nullptr)
                {
                    sendResponse ("400 Bad Request", "text/plain", "json invalido");
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

                // Sempre true: nao ha mais trava de escrita. Fica no JSON
                // porque uma pagina ja aberta no celular, carregada antes da
                // atualizacao, ainda pergunta por ela -- sem o campo, aquele
                // aparelho ficaria em somente leitura ate recarregar.
                o->setProperty ("escritaLivre", true);
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

        int boundPort = 0;
    };
}
