#pragma once

#include <juce_core/juce_core.h>
#include "Protocol.h"
#include <map>
#include <vector>

namespace tl
{
    //==============================================================================
    /**
        Estado do Display: participantes + histórico de conversa.
        Thread-safe (o bus escreve, a UI e o servidor web leem).
    */
    class MessageStore
    {
    public:
        struct Participant
        {
            juce::String id, name;
            juce::Colour colour;
            int  importance = 1;
            bool flash = false;
            juce::int64 lastSeenMs = 0;
            int  messageCount = 0;
            bool isOnline (juce::int64 nowMs) const { return nowMs - lastSeenMs < 7000; }
        };

        struct Entry
        {
            juce::int64  seq = 0;         // id crescente (para SSE / diffs)
            juce::String channelId, utteranceId, name, text;
            juce::Colour colour;
            int  importance = 1;
            bool isFinal = true;
            juce::int64 timeMs = 0;
        };

        struct FlashEvent
        {
            juce::int64 seq = 0;
            juce::Colour colour;
            int importance = 1;
        };

        //-- escrita (thread do bus) ---------------------------------------------
        void handle (const Message& m)
        {
            const juce::ScopedLock sl (lock);
            const auto now = juce::Time::currentTimeMillis();

            if (m.type == "clear") { clearLocked(); return; }

            auto& p = participants[m.channelId];
            p.id = m.channelId; p.name = m.name; p.colour = m.colour;
            p.importance = m.importance; p.flash = m.flash; p.lastSeenMs = now;

            if (m.type != "msg" || m.text.isEmpty()) { ++version; return; }

            // parcial -> substitui a entrada existente da mesma fala
            for (auto it = entries.rbegin(); it != entries.rend(); ++it)
            {
                if (it->channelId == m.channelId && it->utteranceId == m.utteranceId)
                {
                    it->text = m.text; it->isFinal = m.isFinal; it->seq = ++lastSeq;
                    it->timeMs = m.timeMs > 0 ? m.timeMs : now;
                    if (m.isFinal) fireFlashLocked (m);
                    ++version;
                    return;
                }
            }

            Entry e;
            e.seq = ++lastSeq; e.channelId = m.channelId; e.utteranceId = m.utteranceId;
            e.name = m.name; e.text = m.text; e.colour = m.colour; e.importance = m.importance;
            e.isFinal = m.isFinal; e.timeMs = m.timeMs > 0 ? m.timeMs : now;
            entries.push_back (e);
            ++p.messageCount;
            while (entries.size() > 500) entries.erase (entries.begin());

            fireFlashLocked (m);   // pisca já na primeira parcial (chama atenção mais cedo)
            ++version;
        }

        void clear() { const juce::ScopedLock sl (lock); clearLocked(); }

        //-- leitura ----------------------------------------------------------------
        int getVersion() const { const juce::ScopedLock sl (lock); return version; }

        std::vector<Entry> getEntries (const juce::String& onlyChannel = {}) const
        {
            const juce::ScopedLock sl (lock);
            if (onlyChannel.isEmpty()) return entries;
            std::vector<Entry> out;
            for (auto& e : entries) if (e.channelId == onlyChannel) out.push_back (e);
            return out;
        }

        std::vector<Entry> getEntriesSince (juce::int64 seq) const
        {
            const juce::ScopedLock sl (lock);
            std::vector<Entry> out;
            for (auto& e : entries) if (e.seq > seq) out.push_back (e);
            return out;
        }

        std::vector<Participant> getParticipants() const
        {
            const juce::ScopedLock sl (lock);
            std::vector<Participant> out;
            for (auto& kv : participants) out.push_back (kv.second);
            return out;
        }

        juce::int64 getLastSeq() const { const juce::ScopedLock sl (lock); return lastSeq; }
        juce::int64 getClearSeq() const { const juce::ScopedLock sl (lock); return clearSeq; }

        /** Devolve o flash mais recente com seq > `after` (ou seq 0 se não houver). */
        FlashEvent getFlashSince (juce::int64 after) const
        {
            const juce::ScopedLock sl (lock);
            return lastFlash.seq > after ? lastFlash : FlashEvent{};
        }

        //-- JSON para o servidor web -------------------------------------------------
        static juce::var entryToVar (const Entry& e)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("seq", e.seq); o->setProperty ("ch", e.channelId); o->setProperty ("utt", e.utteranceId);
            o->setProperty ("name", e.name); o->setProperty ("text", e.text);
            o->setProperty ("color", e.colour.toDisplayString (false)); o->setProperty ("imp", e.importance);
            o->setProperty ("final", e.isFinal); o->setProperty ("t", e.timeMs);
            return juce::var (o);
        }

        juce::var participantsToVar() const
        {
            const auto now = juce::Time::currentTimeMillis();
            juce::Array<juce::var> arr;
            for (auto& p : getParticipants())
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("ch", p.id); o->setProperty ("name", p.name);
                o->setProperty ("color", p.colour.toDisplayString (false)); o->setProperty ("imp", p.importance);
                o->setProperty ("flash", p.flash); o->setProperty ("online", p.isOnline (now));
                o->setProperty ("count", p.messageCount);
                arr.add (juce::var (o));
            }
            return juce::var (arr);
        }

        /** Modo secundário: aplica o JSON de /state de outro processo (que tem as portas). */
        void applyStateJson (const juce::String& json)
        {
            auto v = juce::JSON::parse (json);
            auto* o = v.getDynamicObject();
            if (o == nullptr) return;

            const auto remoteClear = (juce::int64) o->getProperty ("clearSeq");
            if (remoteClear != lastRemoteClearSeq)
            {
                if (lastRemoteClearSeq != 0 || getEntries().empty() == false) clear();
                lastRemoteClearSeq = remoteClear;
            }

            std::map<juce::String, bool> flashByChannel;
            if (auto* parts = o->getProperty ("participants").getArray())
            {
                const juce::ScopedLock sl (lock);
                const auto now = juce::Time::currentTimeMillis();
                for (auto& pv : *parts)
                {
                    auto* po = pv.getDynamicObject(); if (po == nullptr) continue;
                    auto& p = participants[po->getProperty ("ch").toString()];
                    p.id = po->getProperty ("ch").toString();
                    p.name = po->getProperty ("name").toString();
                    p.colour = juce::Colour::fromString ("ff" + po->getProperty ("color").toString());
                    p.importance = (int) po->getProperty ("imp");
                    p.flash = (bool) po->getProperty ("flash");
                    if ((bool) po->getProperty ("online")) p.lastSeenMs = now;
                    p.messageCount = (int) po->getProperty ("count");
                    flashByChannel[p.id] = p.flash;
                }
                ++version;
            }

            if (auto* ents = o->getProperty ("entries").getArray())
            {
                for (auto& ev : *ents)
                {
                    auto* eo = ev.getDynamicObject(); if (eo == nullptr) continue;
                    Message m;
                    m.type = "msg";
                    m.channelId = eo->getProperty ("ch").toString();
                    m.name = eo->getProperty ("name").toString();
                    m.colour = juce::Colour::fromString ("ff" + eo->getProperty ("color").toString());
                    m.importance = (int) eo->getProperty ("imp");
                    m.flash = flashByChannel.count (m.channelId) ? flashByChannel[m.channelId] : false;
                    m.utteranceId = eo->getProperty ("utt").toString();
                    m.text = eo->getProperty ("text").toString();
                    m.isFinal = (bool) eo->getProperty ("final");
                    m.timeMs = (juce::int64) eo->getProperty ("t");

                    // só passa pelo handle() (que dispara flash) se for novo ou mudou
                    bool changed = true;
                    {
                        const juce::ScopedLock sl (lock);
                        for (auto it = entries.rbegin(); it != entries.rend(); ++it)
                            if (it->channelId == m.channelId && it->utteranceId == m.utteranceId)
                            { changed = it->text != m.text || it->isFinal != m.isFinal; break; }
                    }
                    if (changed) handle (m);
                }
            }
        }

    private:
        juce::int64 lastRemoteClearSeq = 0;

        void clearLocked()
        {
            entries.clear();
            for (auto& kv : participants) kv.second.messageCount = 0;
            clearSeq = ++lastSeq;
            ++version;
        }

        void fireFlashLocked (const Message& m)
        {
            if (! m.flash) return;
            lastFlash.seq = ++lastSeq;
            lastFlash.colour = m.colour;
            lastFlash.importance = m.importance;
        }

        mutable juce::CriticalSection lock;
        std::map<juce::String, Participant> participants;
        std::vector<Entry> entries;
        int version = 0;
        juce::int64 lastSeq = 0, clearSeq = 0;
        FlashEvent lastFlash;
    };
}
