#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <functional>
#include <atomic>
#include "Idioma.h"
#include "Log.h"

/*  "Tem versao nova" -- perguntado ao site, uma vez por abertura do host.

    O cliente nao volta ao site depois de comprar. Ele instala, funciona, e
    fica naquela versao ate alguem avisar. Foi assim que gente continuou com
    um Display que fechava sozinho depois de a correcao ja estar publicada.

    Entao o proprio plugin pergunta. As regras aqui sao de bom senso:

      - uma consulta por processo (repetida a cada 3 h se o host ficar aberto),
        nao por janela aberta;
      - 4 s de paciencia e acabou: casa de show tem rede ruim, e e melhor
        nao avisar nada do que fazer o host engasgar na abertura;
      - falhou, fica calado. Nunca aparece erro de rede por causa disto;
      - quem clicar em Ignorar nao ve de novo no mesmo dia -- no dia seguinte
        volta a lembrar, porque o aviso so vale enquanto incomoda um pouco. */
namespace tl
{
    struct Atualizacao
    {
        struct Resultado
        {
            juce::String versao;    // "2.0"
            juce::String url;       // pagina de download
            bool temNova = false;
        };

        /** true se 'candidata' for mais nova que 'atual' ("2.0" > "1.9"). */
        static bool ehMaisNova (const juce::String& candidata, const juce::String& atual)
        {
            auto pedacos = [] (const juce::String& v)
            {
                juce::StringArray p;
                p.addTokens (v.trim(), ".", {});
                return p;
            };

            const auto a = pedacos (candidata), b = pedacos (atual);

            for (int i = 0; i < juce::jmax (a.size(), b.size()); ++i)
            {
                const int x = i < a.size() ? a[i].getIntValue() : 0;
                const int y = i < b.size() ? b[i].getIntValue() : 0;
                if (x != y) return x > y;
            }
            return false;
        }

        /*  Ultimo resultado conhecido. 'versao' vazia = o site ainda nao
            respondeu; a interface mostra "verificando" ou "nao consegui
            verificar" conforme falhou().                                    */
        static juce::CriticalSection& trava()
        {
            static juce::CriticalSection t;
            return t;
        }

        static Resultado& guardado()
        {
            static Resultado r;
            return r;
        }

        /** Copia do ultimo resultado. Qualquer janela pode chamar a qualquer hora. */
        static Resultado obter()
        {
            const juce::ScopedLock sl (trava());
            return guardado();
        }

        /** true se a ultima tentativa falhou (rede, site fora do ar). */
        static std::atomic<bool>& falhou()
        {
            static std::atomic<bool> f { false };
            return f;
        }

        /*  Pergunta ao site se (a) nunca perguntou ou (b) a ultima resposta e
            velha (3 h) -- host de palco fica aberto por dias.

            O resultado NAO vai para um callback preso a uma janela. O SuperRack
            cria e destroi o editor do plugin varias vezes na abertura; quem
            esperava o callback era justamente uma janela que ja nao existia, e
            a janela que o tecnico via nunca recebia o aviso. Agora o resultado
            fica guardado e cada janela le quando quiser (obter()).          */
        static void atualizarSeNecessario (const juce::String& versaoAtual, bool forcar = false)
        {
            static std::atomic<bool> emAndamento { false };
            static std::atomic<juce::int64> ultima { 0 };

            const auto agora = juce::Time::currentTimeMillis();
            const juce::int64 intervalo = falhou().load() ? 5 * 60 * 1000          // falhou: tenta em 5 min
                                                         : 3 * 60 * 60 * 1000;     // deu certo: 3 h

            if (! forcar && ultima.load() != 0 && agora - ultima.load() < intervalo)
                return;

            if (emAndamento.exchange (true))
                return;

            ultima = agora;

            juce::Thread::launch ([versaoAtual]
            {
                Resultado r;

                juce::URL url ("https://transcriberlive.stream/api/versao");
                const auto opcoes = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                        .withConnectionTimeoutMs (4000);

                juce::String json;
                if (auto entrada = url.createInputStream (opcoes))
                    json = entrada->readEntireStreamAsString();

                if (json.isNotEmpty())
                {
                    /*  O var precisa viver ate o fim do bloco: getDynamicObject()
                        devolve ponteiro para um objeto que pertence a ele. Com
                        o var temporario no proprio 'if' o objeto era liberado
                        antes de ser lido e a versao vinha sempre vazia.     */
                    const auto resposta = juce::JSON::parse (json);

                    if (auto* o = resposta.getDynamicObject())
                    {
                        r.versao = o->getProperty ("versao").toString().trim();
                        r.url    = o->getProperty ("url").toString().trim();
                    }
                }

                if (r.url.isEmpty())
                    r.url = "https://transcriberlive.stream/#baixar";

                const bool ok = r.versao.isNotEmpty();
                r.temNova = ok && ehMaisNova (r.versao, versaoAtual);

                // Sempre no log: foi a falta disto que deixou o defeito invisivel.
                TL_LOGI ("update", ok ? "site respondeu versao " + r.versao + " | instalada " + versaoAtual
                                        + (r.temNova ? " | HA VERSAO NOVA" : " | em dia")
                                      : juce::String ("consulta sem resposta (rede ou site fora do ar)"));

                {
                    const juce::ScopedLock sl (trava());
                    if (ok) guardado() = r;
                }
                falhou() = ! ok;
                emAndamento = false;
            });
        }

        /** "2026-11-08", para marcar o Ignorar de hoje. */
        static juce::String hoje()
        {
            return juce::Time::getCurrentTime().formatted ("%Y-%m-%d");
        }
    };
}
