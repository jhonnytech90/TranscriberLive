#pragma once

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>

namespace tl
{
    //==============================================================================
    /**
        Licenciamento offline do Transcriber Live.

        A licença é um texto assinado com a chave RSA privada do desenvolvedor
        (ver Ferramentas/licenca.py). O plugin traz apenas a chave PÚBLICA e faz a
        verificação localmente — sem internet, sem servidor, sem depender de nada
        durante o show.

        Conteúdo da licença (JSON assinado): titular, e-mail, ID da máquina,
        serial, data de emissão e, opcionalmente, validade (para aluguel).
        A licença só vale na máquina para a qual foi emitida.
    */
    struct LicenseInfo
    {
        bool valid = false;
        juce::String name, email, machineId, serial, issued, expires, note;
        juce::String error;          // motivo, quando inválida
        bool isPerpetual() const { return expires.isEmpty(); }
    };

    class License
    {
    public:
        /** ID desta máquina, no formato TL-XXXX-XXXX-XXXX (é o que o cliente envia). */
        static juce::String getMachineId();

        /** Todos os IDs aceitáveis desta máquina (tolera um identificador ficar indisponível). */
        static juce::StringArray getAllMachineIds();

        /** Arquivo onde a licença instalada fica (compartilhado por todos os plugins). */
        static juce::File getLicenseFile();

        /** Verifica um texto de licença (assinatura + máquina + validade). Não instala. */
        static LicenseInfo verify (const juce::String& licenseText);

        /** Lê e verifica a licença instalada nesta máquina. */
        static LicenseInfo loadInstalled();

        /** Verifica e, se válida, instala. Devolve o resultado da verificação. */
        static LicenseInfo install (const juce::String& licenseText);

        /** Remove a licença instalada. */
        static void uninstall();

        /** Reduz um identificador cru do sistema ao formato TL-XXXX-XXXX-XXXX. */
        static juce::String normalise (const juce::String& id);
    };
}
