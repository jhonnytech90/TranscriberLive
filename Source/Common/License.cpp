#include "License.h"
#include "Hub.h"
#include "Trace.h"

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

    // Consulta ao hardware: SMBIOS, volume do disco, placas de rede. No Windows
    // isso passa por GetSystemFirmwareTable / GetAdaptersAddresses, que em maquina
    // ou VM com firmware estranho pode devolver lixo. Por isso:
    //   - roda UMA vez por processo (resultado em cache),
    //   - cada consulta e isolada, uma falha nao derruba as outras,
    //   - se tudo falhar, cai num ID derivado do nome da maquina + usuario.
    // Sem isso, uma excecao aqui sobe pelo construtor do editor e o host fecha a
    // janela do plugin (ou cai junto).
    static juce::StringArray computeMachineIds()
    {
        using Flags = juce::SystemStats::MachineIdFlags;
        juce::StringArray raw;

        auto tentar = [&raw] (const char* etiqueta, auto&& fn)
        {
            TL_TRACE (juce::String ("machine-id: consultando ") + etiqueta);
            try
            {
                fn();
                TL_TRACE (juce::String ("machine-id: ") + etiqueta + " ok");
            }
            catch (...)
            {
                TL_TRACE (juce::String ("machine-id: ") + etiqueta + " FALHOU (ignorado)");
            }
        };

        tentar ("uniqueDeviceID", [&raw]
        {
            const auto s = juce::SystemStats::getUniqueDeviceID();
            if (s.isNotEmpty()) raw.add (s);
        });

        // um flag por vez: se um deles explodir, os outros ainda valem
        for (auto flag : { Flags::uniqueId, Flags::legacyUniqueId, Flags::fileSystemId, Flags::macAddresses })
        {
            tentar (flag == Flags::uniqueId       ? "uniqueId"
                  : flag == Flags::legacyUniqueId ? "legacyUniqueId"
                  : flag == Flags::fileSystemId   ? "fileSystemId"
                                                  : "macAddresses",
                    [&raw, flag]
                    {
                        for (auto& id : juce::SystemStats::getMachineIdentifiers (flag))
                            if (id.isNotEmpty()) raw.addIfNotAlreadyThere (id);
                    });
        }

        juce::StringArray out;
        for (auto& id : raw)
            out.addIfNotAlreadyThere (License::normalise (id));

        if (out.isEmpty())
        {
            // ultimo recurso: nao e ideal (muda se a maquina for renomeada), mas
            // e melhor do que nao conseguir ativar de jeito nenhum.
            TL_TRACE ("machine-id: nenhuma fonte de hardware respondeu — usando fallback");
            out.add (License::normalise ("fallback:" + juce::SystemStats::getComputerName()
                                         + ":" + juce::SystemStats::getFullUserName()));
        }

        TL_TRACE ("machine-id: " + out.joinIntoString (", "));
        return out;
    }

    static const juce::StringArray& cachedMachineIds()
    {
        static const juce::StringArray ids = computeMachineIds();
        return ids;
    }

    juce::String License::getMachineId()
    {
        const auto& ids = cachedMachineIds();
        return ids.isEmpty() ? juce::String ("TL-0000-0000-0000") : ids[0];
    }

    juce::StringArray License::getAllMachineIds()
    {
        return cachedMachineIds();
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
            const auto pasta = f.getParentDirectory();
            if (pasta.isDirectory() && ! pasta.hasWriteAccess())
                info.error = "A pasta de dados esta bloqueada para o seu usuario ("
                           + pasta.getFullPathName() + ") — por isso a ativacao nao grava.";
            else
                info.error = "Nenhuma licença instalada nesta máquina.";
            return info;
        }
        return verify (f.loadFileAsString());
    }

    LicenseInfo License::install (const juce::String& licenseText)
    {
        auto info = verify (licenseText);
        if (! info.valid)
            return info;

        // Gravar pode falhar — e ja falhou de verdade: um instalador criou esta
        // pasta como root e o usuario ficou sem permissao de escrita. Antes o
        // resultado era ignorado, entao clicar em Ativar nao fazia nada e a tela
        // voltava a dizer "nenhuma licenca instalada". Agora o motivo aparece.
        const auto f = getLicenseFile();
        const auto pasta = f.getParentDirectory();

        if (! pasta.isDirectory())
        {
            const auto r = pasta.createDirectory();
            if (r.failed())
            {
                info.valid = false;
                info.error = "A licenca e valida, mas nao consegui criar a pasta "
                             + pasta.getFullPathName() + " (" + r.getErrorMessage().trim() + ").";
                return info;
            }
        }

        if (! f.replaceWithText (licenseText.trim() + juce::newLine))
        {
            info.valid = false;
            info.error = juce::String ("A licenca e valida, mas nao consegui gravar em ")
                       + f.getFullPathName()
                       + (pasta.hasWriteAccess()
                            ? "."
                            : juce::String (" — a pasta nao tem permissao de escrita para o seu usuario."));
            return info;
        }

        // le de volta: melhor descobrir agora do que na proxima abertura
        if (verify (f.loadFileAsString()).valid == false)
        {
            info.valid = false;
            info.error = "A licenca foi gravada mas nao pode ser lida de volta em "
                       + f.getFullPathName() + ".";
        }
        return info;
    }

    void License::uninstall()
    {
        getLicenseFile().deleteFile();
    }
}
