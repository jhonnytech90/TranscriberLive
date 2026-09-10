# Instaladores do macOS

Dois pacotes, gerados juntos:

| Arquivo | O que faz |
|---|---|
| `TranscriberLive-0.4-macOS.pkg` | instala tudo — **os modelos vêm embutidos, o cliente não baixa nada** |
| `TranscriberLive-0.4-macOS-Desinstalador.pkg` | remove tudo, **preservando a licença** |

## O caminho fácil: deixar o GitHub montar

O job `installer-macos` do `build.yml` roda depois da compilação: baixa os
binários, baixa os modelos (conferindo o SHA-256), monta os dois pacotes,
**instala e desinstala de verdade no runner** para provar que funcionam, e
publica na Release **`macos-latest`**.

O link é fixo, então serve para mandar ao cliente:

```
github.com/jhonnytech90/TranscriberLive/releases/tag/macos-latest
```

Publicamos em Release em vez de artefato de propósito: o instalador tem ~450 MB
e a cota de artefatos de conta free é 500 MB — dois builds e a cota estoura.
Release não conta nessa cota.

## Montando no seu Mac

```bash
./montar-instalador.command [pasta-dos-binarios] [pasta-dos-modelos]
```

Sem argumentos ele procura sozinho:

- **binários:** `./binarios` → `/Applications/arquivos` → `../build`
- **modelos:** `./modelos` → `/Applications/arquivos/models` → `/Library/Application Support/TranscriberLive/models`

Os dois `.pkg` saem em `saida/`.

## Como o instalador funciona

É **payload puro**: o macOS coloca cada arquivo no lugar sozinho. Sem zip, sem
`mv`, sem download.

| Conteúdo | Destino |
|---|---|
| `.vst3` | `/Library/Audio/Plug-Ins/VST3` |
| `.component` | `/Library/Audio/Plug-Ins/Components` |
| `.app` | `/Applications` |
| `ggml-*.bin` | `/Library/Application Support/TranscriberLive/models` |

Os modelos vão para a pasta **de sistema**, não para a home. Duas razões: o
payload de um `.pkg` pode escrever ali (o erro *"instalar conteúdo no volume do
sistema"* só aparece mirando a pasta do usuário), e vale para todos os usuários
do Mac. O plugin já procura nessa pasta.

O `postinstall` faz só o que o payload não consegue: tira a quarentena, ajusta
permissões, reinicia o `AudioComponentRegistrar` para o AU aparecer sem
reiniciar o Mac, e conserta a pasta de dados do usuário caso ela esteja com o
dono errado (herança de uma versão antiga que a criava como root — era isso que
travava a ativação da licença).

## Como o desinstalador funciona

Remove os quatro plugins, o aplicativo, os modelos, as preferências e os
recibos do `pkgutil`.

**A licença é preservada de propósito.** Ela é presa àquela máquina; apagar
significaria o cliente pedir outra numa reinstalação. O texto de abertura do
desinstalador diz isso e mostra o caminho, para quem quiser apagar na mão.

> **Payload mínimo — não tire.** O desinstalador carrega um arquivo-marcador
> só para ter payload. Com `pkgbuild --nopayload` o `PackageInfo` sai sem o
> elemento `<payload>`, e dentro de uma distribuição do `productbuild` o
> Installer trata o pacote como "nada a fazer": **não roda o script e conclui
> na hora**. O próprio script apaga o marcador no fim.

## Por que desinstalador separado, e não uma opção no instalador

O Installer do macOS não tem modo "remover". A opção viraria uma caixinha na
tela *Personalizar* — e cliente apressado desinstala achando que instala. Todo
mundo que vende plugin (Waves, iZotope, Antares) entrega um desinstalador à
parte, pelo mesmo motivo.

## Testando

```bash
sudo installer -pkg "saida/TranscriberLive-0.4-macOS.pkg" -target /
tail -f /private/tmp/transcriberlive-install.log

sudo installer -pkg "saida/TranscriberLive-0.4-macOS-Desinstalador.pkg" -target /
tail -f /private/tmp/transcriberlive-desinstalar.log
```

O CI já faz exatamente isso a cada build, e falha se o desinstalador deixar
qualquer coisa para trás.

## Assinatura

Os `.pkg` saem **sem assinatura**: o macOS do cliente pede *Abrir mesmo assim*
em Ajustes → Privacidade e Segurança. Para eliminar isso é preciso conta de
desenvolvedor Apple (99 dólares/ano):

```bash
productsign --sign "Developer ID Installer: SEU NOME (TEAMID)" \
            saida/TranscriberLive-0.4-macOS.pkg saida/assinado.pkg
xcrun notarytool submit saida/assinado.pkg \
      --apple-id SEU@EMAIL --team-id TEAMID --password SENHA-DE-APP --wait
xcrun stapler staple saida/assinado.pkg
```

Vale assinar também os `.vst3`, `.component` e o `.app` **antes** de montar.
