#include "Adaptadores.h"
#include "Idioma.h"

#if JUCE_MAC
 #include <SystemConfiguration/SystemConfiguration.h>
 #include <ifaddrs.h>
 #include <net/if.h>
 #include <netinet/in.h>
 #include <arpa/inet.h>
#elif JUCE_WINDOWS
 #include <winsock2.h>
 #include <ws2tcpip.h>
 #include <iphlpapi.h>
 #pragma comment(lib, "iphlpapi.lib")
#endif

namespace tl
{

juce::String Adaptadores::rotuloDoTipo (Tipo t)
{
    switch (t)
    {
        case Tipo::semFio:   return tr ("Wi-Fi");
        case Tipo::cabeada:  return tr ("Cabeada");
        case Tipo::ponte:    return tr ("Ponte");
        case Tipo::virtual_: return tr ("Virtual");
        default:             return {};
    }
}

#if JUCE_MAC

namespace
{
    juce::String deCF (CFStringRef s)
    {
        if (s == nullptr)
            return {};

        if (auto* c = CFStringGetCStringPtr (s, kCFStringEncodingUTF8))
            return juce::String::fromUTF8 (c);

        char buf[512] = {};
        if (CFStringGetCString (s, buf, sizeof (buf), kCFStringEncodingUTF8))
            return juce::String::fromUTF8 (buf);

        return {};
    }

    /*  O tipo vem como texto ("IEEE80211", "Ethernet", "Bridge"...).

        Comparar pelo texto, e nao pelas constantes kSCNetworkInterfaceType*,
        e de proposito: parte delas nao esta declarada no SDK que o CI usa, e
        o build quebrou exatamente nisso. O texto e estavel ha mais de uma
        decada e nao depende de qual SDK compilou.                        */
    Adaptadores::Tipo tipoCF (CFStringRef t)
    {
        const auto nome = deCF (t);

        if (nome == "IEEE80211")                       return Adaptadores::Tipo::semFio;
        if (nome == "Ethernet")                        return Adaptadores::Tipo::cabeada;
        if (nome == "Bridge" || nome == "Bond")        return Adaptadores::Tipo::ponte;
        if (nome == "VPN" || nome == "IPSec" || nome == "PPP" || nome == "L2TP")
                                                       return Adaptadores::Tipo::virtual_;

        return Adaptadores::Tipo::desconhecida;
    }
}

static juce::Array<Adaptadores::Placa> lerDoSistema()
{
    juce::Array<Adaptadores::Placa> fora;

    // 1) quem tem qual IP -- isto so o getifaddrs sabe
    struct ifaddrs* raiz = nullptr;
    if (getifaddrs (&raiz) != 0 || raiz == nullptr)
        return fora;

    for (auto* it = raiz; it != nullptr; it = it->ifa_next)
    {
        if (it->ifa_addr == nullptr || it->ifa_addr->sa_family != AF_INET)
            continue;

        if ((it->ifa_flags & IFF_UP) == 0 || (it->ifa_flags & IFF_LOOPBACK) != 0)
            continue;

        char texto[INET_ADDRSTRLEN] = {};
        const auto* v4 = reinterpret_cast<const struct sockaddr_in*> (it->ifa_addr);
        if (inet_ntop (AF_INET, &v4->sin_addr, texto, sizeof (texto)) == nullptr)
            continue;

        Adaptadores::Placa p;
        p.ip      = juce::String::fromUTF8 (texto);
        p.sistema = juce::String::fromUTF8 (it->ifa_name);
        fora.add (p);
    }

    freeifaddrs (raiz);

    // 2) o nome bonito e o tipo -- isto so o SystemConfiguration sabe
    if (auto todas = SCNetworkInterfaceCopyAll())
    {
        const auto quantas = CFArrayGetCount (todas);

        for (CFIndex i = 0; i < quantas; ++i)
        {
            auto placa = (SCNetworkInterfaceRef) CFArrayGetValueAtIndex (todas, i);
            if (placa == nullptr)
                continue;

            const auto bsd = deCF (SCNetworkInterfaceGetBSDName (placa));
            if (bsd.isEmpty())
                continue;

            const auto nome = deCF (SCNetworkInterfaceGetLocalizedDisplayName (placa));
            const auto tipo = tipoCF (SCNetworkInterfaceGetInterfaceType (placa));

            for (auto& p : fora)
                if (p.sistema == bsd)
                {
                    p.nome = nome;
                    p.tipo = tipo;
                }
        }

        CFRelease (todas);
    }

    return fora;
}

#elif JUCE_WINDOWS

static juce::Array<Adaptadores::Placa> lerDoSistema()
{
    juce::Array<Adaptadores::Placa> fora;

    ULONG tamanho = 16 * 1024;
    juce::HeapBlock<char> area;

    // O tamanho certo so se descobre tentando; a API avisa de quanto precisa.
    for (int tentativa = 0; tentativa < 4; ++tentativa)
    {
        area.calloc (tamanho);
        auto* lista = reinterpret_cast<IP_ADAPTER_ADDRESSES*> (area.getData());

        const auto r = GetAdaptersAddresses (AF_INET,
                                             GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST
                                               | GAA_FLAG_SKIP_DNS_SERVER,
                                             nullptr, lista, &tamanho);

        if (r == ERROR_BUFFER_OVERFLOW)
            continue;

        if (r != NO_ERROR)
            return fora;

        for (auto* a = lista; a != nullptr; a = a->Next)
        {
            if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
                continue;

            for (auto* u = a->FirstUnicastAddress; u != nullptr; u = u->Next)
            {
                if (u->Address.lpSockaddr == nullptr || u->Address.lpSockaddr->sa_family != AF_INET)
                    continue;

                char texto[INET_ADDRSTRLEN] = {};
                const auto* v4 = reinterpret_cast<const sockaddr_in*> (u->Address.lpSockaddr);
                if (inet_ntop (AF_INET, (PVOID) &v4->sin_addr, texto, sizeof (texto)) == nullptr)
                    continue;

                Adaptadores::Placa p;
                p.ip      = juce::String::fromUTF8 (texto);
                p.sistema = juce::String (a->AdapterName);
                p.nome    = juce::String (a->FriendlyName);

                switch (a->IfType)
                {
                    case IF_TYPE_IEEE80211:        p.tipo = Adaptadores::Tipo::semFio;  break;
                    case IF_TYPE_ETHERNET_CSMACD:  p.tipo = Adaptadores::Tipo::cabeada; break;
                    case IF_TYPE_PPP:
                    case IF_TYPE_TUNNEL:           p.tipo = Adaptadores::Tipo::virtual_; break;
                    default:                       break;
                }

                // Adaptador de maquina virtual se anuncia como Ethernet; o nome
                // entrega. Dizer "Cabeada" ali so confundiria.
                const auto n = p.nome.toLowerCase();
                if (n.contains ("virtual") || n.contains ("vmware") || n.contains ("hyper-v")
                      || n.contains ("virtualbox") || n.contains ("vethernet") || n.contains ("loopback"))
                    p.tipo = Adaptadores::Tipo::virtual_;

                fora.add (p);
            }
        }

        return fora;
    }

    return fora;
}

#else

static juce::Array<Adaptadores::Placa> lerDoSistema() { return {}; }

#endif

juce::Array<Adaptadores::Placa> Adaptadores::listar()
{
    static juce::CriticalSection trava;
    static juce::Array<Placa> cache;
    static juce::uint32 quando = 0;

    const juce::ScopedLock sl (trava);
    const auto agora = juce::Time::getMillisecondCounter();

    if (cache.isEmpty() || agora - quando > 5000)
    {
        cache = lerDoSistema();
        quando = agora;
    }

    return cache;
}

Adaptadores::Placa Adaptadores::doEndereco (const juce::String& ip)
{
    for (auto& p : listar())
        if (p.ip == ip)
            return p;

    return {};
}

}
