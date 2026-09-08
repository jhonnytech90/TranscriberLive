#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

//==============================================================================
/**
    Protocolo Receiver -> Display.

    Cada Receiver manda datagramas UDP com um objeto JSON por mensagem.
    Tipos:
      "hello"  — heartbeat (a cada ~2 s) com identidade do canal (nome, cor, importância)
      "msg"    — frase transcrita (parcial ou final). Parciais e o final da mesma
                 fala compartilham o mesmo `utt` (id da fala) para o Display substituir.
      "clear"  — pede ao Display para limpar o histórico (vem da UI do Receiver)
*/
namespace tl
{
    constexpr int kDefaultBusPort  = 47800;   // UDP  Receiver -> Display
    constexpr int kDefaultHttpPort = 47801;   // HTTP Display -> celular/tablet

    enum class Importance { normal = 1, important = 2, urgent = 3 };

    struct Message
    {
        juce::String type;                 // hello | msg | clear
        juce::String channelId;            // UUID do Receiver
        juce::String name;                 // nome da pessoa/canal
        juce::Colour colour;
        int          importance = 1;       // 1..3
        bool         flash = false;        // Display deve piscar
        juce::String utteranceId;          // id da fala (parcial/final)
        juce::String text;
        bool         isFinal = true;
        juce::int64  timeMs = 0;           // epoch ms

        juce::String toJson() const
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("type",  type);
            o->setProperty ("ch",    channelId);
            o->setProperty ("name",  name);
            o->setProperty ("color", colour.toDisplayString (false));   // "RRGGBB"
            o->setProperty ("imp",   importance);
            o->setProperty ("flash", flash);
            o->setProperty ("utt",   utteranceId);
            o->setProperty ("text",  text);
            o->setProperty ("final", isFinal);
            o->setProperty ("t",     timeMs);
            return juce::JSON::toString (juce::var (o), true);
        }

        static bool fromJson (const juce::String& json, Message& out)
        {
            auto v = juce::JSON::parse (json);
            auto* o = v.getDynamicObject();
            if (o == nullptr) return false;

            out.type        = o->getProperty ("type").toString();
            out.channelId   = o->getProperty ("ch").toString();
            out.name        = o->getProperty ("name").toString();
            out.colour      = juce::Colour::fromString ("ff" + o->getProperty ("color").toString());
            out.importance  = juce::jlimit (1, 3, (int) o->getProperty ("imp"));
            out.flash       = (bool) o->getProperty ("flash");
            out.utteranceId = o->getProperty ("utt").toString();
            out.text        = o->getProperty ("text").toString();
            out.isFinal     = (bool) o->getProperty ("final");
            out.timeMs      = (juce::int64) o->getProperty ("t");
            return out.type.isNotEmpty();
        }
    };

    /** Paleta padrão para novos canais (bem distintas entre si num fundo escuro). */
    inline juce::Colour defaultColourForIndex (int i)
    {
        static const juce::uint32 palette[] = {
            0xff3ddc84, 0xff4fc3f7, 0xffffb74d, 0xfff06292, 0xffba68c8,
            0xffaed581, 0xffff8a65, 0xff4dd0e1, 0xfffff176, 0xff90a4ae };
        return juce::Colour (palette[juce::jmax (0, i) % (int) std::size (palette)]);
    }
}
