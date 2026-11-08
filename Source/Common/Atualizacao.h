#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <functional>
#include "Idioma.h"
#include "Log.h"

/*  "Tem versao nova" -- perguntado ao site, uma vez por abertura do host.

    O cliente nao volta ao site depois de comprar. Ele instala, funciona, e
    fica naquela versao ate alguem avisar. Foi assim que gente continuou com
    um Display que fechava sozinho depois de a correcao ja estar publicada.

    Entao o proprio plugin pergunta. As regras aqui sao de bom senso:

      - uma consulta por processo, nao por janela aberta;
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

        /*  Resultado desta sessao. Vazio enquanto a consulta nao voltou. */
        static Resultado& cache()
        {
            static Resultado r;
            return r;
        }

        /*  Pergunta ao site, uma vez so. 'pronto' roda na thread da interface
            e so e chamado quando ha mesmo versao nova.                      */
        static void consultar (const juce::String& versaoAtual,
                               std::function<void (Resultado)> pronto)
        {
            static std::atomic<bool> jaPerguntou { false };

            if (jaPerguntou.exchange (true))
            {
                if (cache().temNova && pronto != nullptr)
                    pronto (cache());
                return;
            }

            juce::Thread::launch ([versaoAtual, pronto]
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
                    if (auto* o = juce::JSON::parse (json).getDynamicObject())
                    {
                        r.versao = o->getProperty ("versao").toString().trim();
                        r.url    = o->getProperty ("url").toString().trim();
                    }
                }

                if (r.url.isEmpty())
                    r.url = "https://transcriberlive.stream/#baixar";

                r.temNova = r.versao.isNotEmpty() && ehMaisNova (r.versao, versaoAtual);

                if (r.temNova)
                    TL_LOGI ("update", "versao nova no site: " + r.versao);

                juce::MessageManager::callAsync ([r, pronto]
                {
                    cache() = r;
                    if (r.temNova && pronto != nullptr)
                        pronto (r);
                });
            });
        }

        /** "2026-11-08", para marcar o Ignorar de hoje. */
        static juce::String hoje()
        {
            return juce::Time::getCurrentTime().formatted ("%Y-%m-%d");
        }
    };
}
