// Teste do licenciamento: mostra o ID desta maquina e verifica/instala uma licenca.
//   LicenseTest                       -> mostra IDs e a licenca instalada
//   LicenseTest arquivo.txt           -> verifica o arquivo
//   LicenseTest arquivo.txt install   -> verifica e instala
#include <juce_core/juce_core.h>
#include "Common/License.h"
#include <cstdio>

int main (int argc, char** argv)
{
    std::printf ("machine-id : %s\n", tl::License::getMachineId().toRawUTF8());
    std::printf ("aceitos    : %s\n", tl::License::getAllMachineIds().joinIntoString (", ").toRawUTF8());
    std::printf ("arquivo    : %s\n", tl::License::getLicenseFile().getFullPathName().toRawUTF8());

    if (argc > 1)
    {
        const auto f = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
        const auto txt = f.loadFileAsString();
        const auto info = tl::License::verify (txt);
        std::printf ("\nverify     : %s\n", info.valid ? "VALIDA" : "INVALIDA");
        if (info.valid)
            std::printf ("  titular  : %s <%s>\n  maquina  : %s\n  serial   : %s\n  validade : %s\n",
                         info.name.toRawUTF8(), info.email.toRawUTF8(), info.machineId.toRawUTF8(),
                         info.serial.toRawUTF8(), info.isPerpetual() ? "perpetua" : info.expires.toRawUTF8());
        else
            std::printf ("  motivo   : %s\n", info.error.toRawUTF8());

        if (argc > 2 && juce::String (argv[2]) == "install" && info.valid)
        {
            const auto res = tl::License::install (txt);
            std::printf ("install   : %s%s\n", res.valid ? "OK" : "FALHOU - ",
                         res.valid ? "" : res.error.toRawUTF8());
            std::printf ("  -> instalada\n");
        }
    }

    const auto cur = tl::License::loadInstalled();
    std::printf ("\ninstalada  : %s\n", cur.valid ? ("SIM (" + cur.name + ")").toRawUTF8() : ("NAO - " + cur.error).toRawUTF8());
    return 0;
}
