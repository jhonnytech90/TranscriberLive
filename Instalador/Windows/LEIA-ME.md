# Instalador do Windows (.exe)

Um `.exe` único que instala os dois plugins VST3, o aplicativo Display e **baixa
os modelos de voz do Hugging Face durante a instalação**, com o SHA-256 de cada
arquivo conferido no fim do download.

## O caminho fácil: deixar o GitHub montar

O `build.yml` tem um job `installer-windows` que roda depois da compilação. Ele
baixa os binários, instala o Inno Setup e monta o setup sozinho. O resultado sai
como artefato **`TranscriberLive-Windows-Setup`** na mesma página da build.

Você não precisa de nada instalado no seu PC para isso.

## Montar na sua máquina

1. Instale o Inno Setup 6.3 ou mais novo — é gratuito:

   ```
   winget install JRSoftware.InnoSetup
   ```

   (ou baixe em jrsoftware.org/isdl.php)

2. Baixe o zip `TranscriberLive-Windows` do GitHub Actions e descompacte numa
   pasta `binarios` aqui do lado.

3. Duplo clique em `montar-instalador.bat`.

O setup sai em `saida\TranscriberLive-0.4-Windows-Setup.exe`.

Se preferir a linha de comando:

```
iscc /DBIN=C:\caminho\dos\binarios TranscriberLive.iss
```

O script aceita a pasta `build` do CMake, o zip do Actions descompactado, ou uma
pasta com os três itens soltos — ele procura nos três formatos.

## O que o instalador faz, e onde

| Conteúdo | Destino |
|---|---|
| `Transcriber Live Receiver.vst3` | `C:\Program Files\Common Files\VST3` |
| `Transcriber Live Display.vst3` | `C:\Program Files\Common Files\VST3` |
| `Transcriber Live Display.exe` | `C:\Program Files\Transcriber Live` |
| `ggml-*.bin` (modelos) | `C:\ProgramData\TranscriberLive\models` |

A pasta de dados é criada com permissão de escrita para todos os usuários — é
onde os plugins gravam a licença e leem os modelos. `C:\ProgramData` foi escolhido
de propósito em vez de `%APPDATA%`: o instalador roda elevado, e com AppData o
arquivo poderia acabar no perfil errado. O plugin já procura modelos nas duas
pastas, então quem instalou na mão antes não perde nada.

## Modelos oferecidos

O instalador tem uma página de seleção. O detector de voz Silero é obrigatório e
vem marcado sem opção de desmarcar; o `small` vem marcado por padrão, que é o que
o plugin escolheria sozinho.

| Modelo | Tamanho | Quando usar |
|---|---|---|
| Silero VAD | 0,9 MB | sempre — é ele que separa fala de vazamento |
| tiny | 74 MB | máquina fraca, aceita errar |
| base | 141 MB | PC modesto |
| **small** | **465 MB** | **recomendado para show** |
| medium | 1,4 GB | mais preciso, exige CPU boa |
| large-v3-turbo | 1,5 GB | o melhor, só em máquina forte |

Modelos que já estão no disco aparecem marcados como *"já está instalado"* e não
são baixados de novo — e nunca são sobrescritos.

Se o download falhar (internet caiu, Hugging Face fora do ar), **a instalação
continua**: os plugins ficam instalados e a mensagem diz onde colocar os
`ggml-*.bin` depois. Não faz sentido perder a instalação inteira por causa de um
download.

## Desinstalação

Remove os plugins e o aplicativo, e **pergunta** se deve apagar os modelos —
responder Não evita rebaixar gigabytes numa reinstalação. A licença fica no
disco: ela é sua e está presa àquela máquina.

## Assinatura

O `.exe` sai **sem assinatura**, então o SmartScreen vai mostrar "O Windows
protegeu o seu PC" e o cliente precisa clicar em *Mais informações → Executar
assim mesmo*. Isso é o atrito número um de quem vende software fora de loja.

Para eliminar: um certificado de assinatura de código. O comum (OV) custa na
faixa de 200–400 dólares/ano e ainda leva um tempo até o SmartScreen ganhar
reputação; o EV é mais caro e já nasce com reputação. Com o certificado em mão:

```
signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 ^
         "saida\TranscriberLive-0.4-Windows-Setup.exe"
```

Vale assinar também os `.vst3` e o `.exe` do Display **antes** de montar o setup.
Enquanto não tiver o certificado, escreva no e-mail de entrega como liberar o
instalador — é a dúvida que mais chega.
