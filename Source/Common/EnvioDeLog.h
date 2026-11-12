#pragma once

#include <juce_core/juce_core.h>
#include "Log.h"
#include "License.h"
#include "Idioma.h"

/*  Manda o diagnostico para o site, em vez de pedir para o cliente achar a
    pasta de logs.

    Por que isto existe: a pasta e oculta no Windows, e o caminho so aparece no
    meio de um show, quando ja deu problema. O cliente clicava em Log, a pasta
    abria, e dali em diante dependia dele compactar e mandar no WhatsApp. Quase
    ninguem fazia -- e sem o log nao ha como saber se foi CPU, rede ou modelo.

    Vai sempre em outra thread: a janela do plugin nao pode congelar esperando a
    rede de uma casa de show, e falhar tem que ser so uma mensagem, nunca um
    travo no host.

    Quem identifica o envio e a propria licenca (nome e e-mail de quem comprou).
    Em maquina sem licenca vai so com o ID da maquina.                       */
namespace tl
{
    struct EnvioDeLog
    {
        /** O que vai subir, montado antes de perguntar ao usuario. */
        struct Pacote
        {
            juce::String nome, email, maquina, versao, so, host;
            juce::StringArray nomesDeArquivo;
            juce::int64 bytes = 0;
        };

        static juce::String endereco() { return "https://transcriberlive.stream/api/log"; }

        /*  Junta os logs da pasta.

            Sao poucos e pequenos desde que o log virou um arquivo por programa
            com teto: Receiver, Display e o app, alguns KB cada. Mandar os tres
            importa porque o problema costuma estar no outro processo -- o
            Display que fechou sozinho nao aparece no log do Receiver.        */
        static juce::Array<juce::File> arquivos()
        {
            juce::Array<juce::File> fora;
            const auto pasta = Log::get().getFolder();

            if (! pasta.isDirectory())
                return fora;

            for (auto& f : pasta.findChildFiles (juce::File::findFiles, false, "*.log"))
                if (f.getSize() > 0)
                    fora.add (f);

            return fora;
        }

        static Pacote montar (const LicenseInfo& lic, const juce::String& hostDescricao)
        {
            Pacote p;
            p.nome    = lic.name;
            p.email   = lic.email;
            p.maquina = lic.machineId.isNotEmpty() ? lic.machineId : License::getMachineId();
            p.host    = hostDescricao;
            p.so      = juce::SystemStats::getOperatingSystemName();

           #ifdef JucePlugin_VersionString
            p.versao  = JucePlugin_VersionString;
           #endif

            for (auto& f : arquivos())
            {
                p.nomesDeArquivo.add (f.getFileName());
                p.bytes += f.getSize();
            }

            return p;
        }

        /*  Sobe de verdade. Devolve texto vazio em caso de sucesso, ou a frase
            de erro pronta para a tela. Roda na thread de quem chamar.        */
        static juce::String enviar (const Pacote& p, const juce::String& recado)
        {
            juce::Array<juce::var> lista;

            for (auto& f : arquivos())
            {
                auto* a = new juce::DynamicObject();
                a->setProperty ("nome",  f.getFileName());
                a->setProperty ("texto", f.loadFileAsString());
                lista.add (juce::var (a));
            }

            if (lista.isEmpty())
                return tr ("N\xc3\xa3o encontrei nenhum log para enviar.");

            auto* o = new juce::DynamicObject();
            o->setProperty ("nome",     p.nome);
            o->setProperty ("email",    p.email);
            o->setProperty ("maquina",  p.maquina);
            o->setProperty ("versao",   p.versao);
            o->setProperty ("so",       p.so);
            o->setProperty ("host",     p.host);
            o->setProperty ("recado",   recado);
            o->setProperty ("arquivos", juce::var (lista));

            juce::URL url (endereco());
            url = url.withPOSTData (juce::JSON::toString (juce::var (o), true));

            int codigo = 0;
            auto opcoes = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                              .withConnectionTimeoutMs (20000)
                              .withExtraHeaders ("Content-Type: application/json")
                              .withHttpRequestCmd ("POST")
                              .withStatusCode (&codigo);

            auto entrada = url.createInputStream (opcoes);

            if (entrada == nullptr)
                return tr ("N\xc3\xa3o consegui falar com o site. Confira a internet desta m\xc3\xa1quina.");

            const auto resposta = entrada->readEntireStreamAsString();

            if (codigo >= 200 && codigo < 300)
                return {};

            // O var vive ate o fim da funcao: getDynamicObject() aponta para dentro dele.
            const auto corpoJson = juce::JSON::parse (resposta);

            if (auto* r = corpoJson.getDynamicObject())
                if (r->hasProperty ("erro"))
                    return r->getProperty ("erro").toString();

            return tr ("O site recusou o envio.") + " (" + juce::String (codigo) + ")";
        }
    };
}
