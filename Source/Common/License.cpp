#include "License.h"
#include "Hub.h"

namespace tl
{
    //==============================================================================
    // Chave PÚBLICA do desenvolvedor ("expoente,modulo" em hexadecimal).
    // Gerada por Ferramentas/licenca.py init — a privada NUNCA entra aqui.
    static const char* kPublicKey = "10001,89DA375D728E1E4303960CCC33CD89EA079AC793019810EC4E0F31EE4B70E5627F273FF6B191772DD32F6F60262A81F1B1BBCEAA2467319949B4EF0BBC82221282B8D51D658EDFA30E66846EE9DA749415659636FE271FC40720643753EAF533FC5B269378912956B179721772DEB5E5B6CC59AA00BB5EA897B273632375F617014D618A552783AEC137366877585B851A68C3112E06360D82D8A91C5090977F744502C1396E8774BCD9621698F9299F1D57F52981D763D2BE614C7570E564FF88AD12FB462E1937F5C49B30C04A3B35C846C7758F2A84B98E4F5CB810B71C6A6DAEFB8BFAA89D9BE86FCAD91E7829E85BE24FCD207729F427D1428CFC4BFF29";

    static constexpr const char* kProduct = "transcriber-live";

    //==============================================================================
    juce::String License::normalise (const juce::String& id)
    {
        // hash estável -> TL-XXXX-XXXX-XXXX
        const auto hex = juce::SHA256 (id.toRawUTF8(), id.getNumBytesAsUTF8()).toHexString().toUpperCase();
        return "TL-" + hex.substring (0, 4) + "-" + hex.substring (4, 8) + "-" + hex.substring (8, 12);
    }

    juce::String License::getMachineId()
    {
        const auto unique = juce::SystemStats::getUniqueDeviceID();
        if (unique.isNotEmpty())
            return normalise (unique);

        const auto ids = getAllMachineIds();
        return ids.isEmpty() ? juce::String ("TL-0000-0000-0000") : ids[0];
    }

    juce::StringArray License::getAllMachineIds()
    {
        using Flags = juce::SystemStats::MachineIdFlags;
        const auto all = (Flags) ((int) Flags::uniqueId | (int) Flags::legacyUniqueId
                                  | (int) Flags::fileSystemId | (int) Flags::macAddresses);

        juce::StringArray out;
        const auto unique = juce::SystemStats::getUniqueDeviceID();
        if (unique.isNotEmpty())
            out.add (normalise (unique));

        for (auto& id : juce::SystemStats::getMachineIdentifiers (all))
            if (id.isNotEmpty())
                out.addIfNotAlreadyThere (normalise (id));

        return out;
    }

    juce::File License::getLicenseFile()
    {
        return Hub::getDataDir().getChildFile ("license.key");
    }

    //==============================================================================
    LicenseInfo License::verify (const juce::String& licenseText)
    {
        LicenseInfo info;

        // 1. limpa: descarta linhas de cabeçalho e todo espaço em branco
        juce::String blob;
        for (auto& line : juce::StringArray::fromLines (licenseText))
            if (! line.contains ("-----"))
                blob += line.trim();
        blob = blob.removeCharacters (" \t\r\n");

        if (blob.isEmpty())            { info.error = "Nenhuma licença informada."; return info; }
        if (! blob.containsChar ('.')) { info.error = "Formato inválido (o texto parece incompleto)."; return info; }

        const auto b64    = blob.upToLastOccurrenceOf (".", false, false);
        const auto sigHex = blob.fromLastOccurrenceOf (".", false, false);

        // 2. payload = bytes exatos que foram assinados
        juce::MemoryOutputStream payload;
        if (! juce::Base64::convertFromBase64 (payload, b64) || payload.getDataSize() == 0)
        {
            info.error = "Conteúdo ilegível — copie a licença inteira, incluindo todas as linhas.";
            return info;
        }

        // 3. assinatura: sig^e mod n deve reproduzir o SHA-256 do payload
        const auto digestHex = juce::SHA256 (payload.getData(), payload.getDataSize()).toHexString();

        juce::BigInteger digest, value;
        digest.parseString (digestHex, 16);
        value.parseString (sigHex, 16);

        const juce::RSAKey publicKey (kPublicKey);
        if (! publicKey.applyToValue (value) || value != digest)
        {
            info.error = "Assinatura inválida — esta licença não foi emitida para o Transcriber Live.";
            return info;
        }

        // 4. campos
        const auto json = juce::JSON::parse (payload.toString());
        if (auto* o = json.getDynamicObject())
        {
            info.name     = o->getProperty ("nome").toString();
            info.email    = o->getProperty ("email").toString();
            info.machineId= o->getProperty ("mid").toString().toUpperCase();
            info.serial   = o->getProperty ("serial").toString();
            info.issued   = o->getProperty ("iat").toString();
            info.expires  = o->getProperty ("exp").toString();
            info.note     = o->getProperty ("nota").toString();

            if (o->getProperty ("prod").toString() != kProduct)
            {
                info.error = "Licença de outro produto.";
                return info;
            }
        }
        else
        {
            info.error = "Conteúdo ilegível.";
            return info;
        }

        // 5. máquina
        if (! getAllMachineIds().contains (info.machineId))
        {
            info.error = "Esta licença foi emitida para outro computador (" + info.machineId + ").";
            return info;
        }

        // 6. validade (vazio = perpetua)
        if (info.expires.isNotEmpty())
        {
            const auto today = juce::Time::getCurrentTime().formatted ("%Y-%m-%d");
            if (today > info.expires)
            {
                info.error = "Licença expirada em " + info.expires + ".";
                return info;
            }
        }

        info.valid = true;
        return info;
    }

    LicenseInfo License::loadInstalled()
    {
        const auto f = getLicenseFile();
        if (! f.existsAsFile())
        {
            LicenseInfo info;
            info.error = "Nenhuma licença instalada nesta máquina.";
            return info;
        }
        return verify (f.loadFileAsString());
    }

    LicenseInfo License::install (const juce::String& licenseText)
    {
        auto info = verify (licenseText);
        if (info.valid)
        {
            const auto f = getLicenseFile();
            f.getParentDirectory().createDirectory();
            f.replaceWithText (licenseText.trim() + juce::newLine);
        }
        return info;
    }

    void License::uninstall()
    {
        getLicenseFile().deleteFile();
    }
}
