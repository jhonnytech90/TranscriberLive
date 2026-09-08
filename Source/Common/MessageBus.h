#pragma once

#include <juce_core/juce_core.h>
#include "Protocol.h"
#include "SocketSafety.h"
#include <functional>

namespace tl
{
    //==============================================================================
    /** Lado do Receiver: manda datagramas para host:port (não bloqueia a UI). */
    class BusSender
    {
    public:
        BusSender() { ignoreSigpipeOnce(); socket.bindToPort (0); makeSocketSafe (socket.getRawSocketHandle()); }

        void setTarget (const juce::String& host, int port)
        {
            const juce::ScopedLock sl (lock);
            targetHost = host.trim().isEmpty() ? "127.0.0.1" : host.trim();
            targetPort = port > 0 ? port : kDefaultBusPort;
        }

        void send (const Message& m)
        {
            const auto json = m.toJson();
            const juce::ScopedLock sl (lock);
            socket.write (targetHost, targetPort, json.toRawUTF8(), (int) json.getNumBytesAsUTF8());
        }

        juce::String getTargetHost() const { const juce::ScopedLock sl (lock); return targetHost; }
        int          getTargetPort() const { const juce::ScopedLock sl (lock); return targetPort; }

    private:
        mutable juce::CriticalSection lock;
        juce::DatagramSocket socket { true };   // broadcast permitido (ex.: 192.168.0.255)
        juce::String targetHost { "127.0.0.1" };
        int targetPort = kDefaultBusPort;
    };

    //==============================================================================
    /** Lado do Display: escuta a porta UDP e entrega mensagens (na thread do listener). */
    class BusListener : private juce::Thread
    {
    public:
        using Callback = std::function<void (const Message&)>;

        explicit BusListener (Callback cb) : juce::Thread ("TranscriberLive Bus"), callback (std::move (cb)) {}
        ~BusListener() override { stop(); }

        bool start (int port)
        {
            stop();
            ignoreSigpipeOnce();
            socket = std::make_unique<juce::DatagramSocket> (false);
            if (! socket->bindToPort (port))
            {
                socket.reset();
                bound.store (false);
                return false;
            }
            makeSocketSafe (socket->getRawSocketHandle());
            boundPort = port;
            bound.store (true);
            startThread();
            return true;
        }

        void stop()
        {
            signalThreadShouldExit();
            if (socket != nullptr) socket->shutdown();
            stopThread (2000);
            socket.reset();
            bound.store (false);
        }

        bool isBound() const noexcept { return bound.load(); }
        int  getPort()  const noexcept { return boundPort; }

    private:
        void run() override
        {
            std::vector<char> buf (65536);
            while (! threadShouldExit())
            {
                if (socket->waitUntilReady (true, 200) != 1) continue;

                const int n = socket->read (buf.data(), (int) buf.size() - 1, false);
                if (n <= 0) continue;

                buf[(size_t) n] = 0;
                Message m;
                if (Message::fromJson (juce::String::fromUTF8 (buf.data(), n), m) && callback)
                    callback (m);
            }
        }

        Callback callback;
        std::unique_ptr<juce::DatagramSocket> socket;
        std::atomic<bool> bound { false };
        int boundPort = 0;
    };
}
