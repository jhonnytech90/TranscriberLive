# Transcriber Live

Dois plugins **VST3 / AU** (JUCE + whisper.cpp) para comunicação por texto entre palco e
técnica durante o show: o cantor/músico fala no microfone, o texto aparece na tela da técnica
(e no celular/tablet de quem quiser). Tudo roda **offline**, na máquina do host. O áudio passa
intacto pelos dois plugins — eles só "escutam".

| Plugin | Onde vai | O que faz |
|---|---|---|
| **Transcriber Live Receiver** | Um em cada canal de mic (cantor, baixo, bateria...) | Transcreve a voz falada daquele canal e manda para o Display com nome, cor, importância e flash |
| **Transcriber Live Display** | Em qualquer canal, ou como **app** (sem precisar de áudio) | Mostra tudo como conversa em balões coloridos, barra de participantes com filtro, flash de tela |

O servidor para celular/tablet e o receptor UDP sobem **automaticamente com qualquer parte da
ferramenta** (basta um Receiver carregado no host — não precisa abrir o Display). Se o app
Display estiver aberto na mesma máquina, ele assume e o host espelha o estado dele.

```
 canal 1 ─ Receiver "Cantor"  ─┐
 canal 2 ─ Receiver "Baixo"   ─┼─ UDP 47800 (JSON) ─▶  Display  ─ HTTP 47801 ─▶ celular / tablet
 canal 3 ─ Receiver "Bateria" ─┘                        (balões, flash)          (mesma conversa, Wi-Fi)
```

---

## Índice

1. [O que você precisa baixar](#1-o-que-você-precisa-baixar)
2. [Compilar no macOS](#2-compilar-no-macos)
3. [Compilar no Windows](#3-compilar-no-windows)
4. [Compilar pelo GitHub Actions (sem instalar nada)](#4-compilar-pelo-github-actions-sem-instalar-nada)
5. [Instalar os plugins](#5-instalar-os-plugins)
6. [Baixar os modelos (obrigatório)](#6-baixar-os-modelos-obrigatório)
7. [Usar no show](#7-usar-no-show)
8. [Celular / tablet](#8-celular--tablet)
9. [Rede e portas](#9-rede-e-portas)
10. [Parâmetros automatizáveis](#10-parâmetros-automatizáveis-receiver)
11. [Solução de problemas](#11-solução-de-problemas)
12. [Como funciona por dentro](#12-como-funciona-por-dentro)
13. [Estrutura do código](#13-estrutura-do-código)

---

## 1. O que você precisa baixar

### Ferramentas de build

| Ferramenta | macOS | Windows |
|---|---|---|
| Compilador | **Xcode 14 ou mais novo** (App Store, grátis, ~12 GB). Depois de instalar, abra uma vez e aceite a licença. | **Visual Studio 2022 Community** (grátis): https://visualstudio.microsoft.com/vs/community/ — na instalação marque a carga de trabalho **"Desenvolvimento para desktop com C++"** |
| CMake ≥ 3.22 | `brew install cmake` (instale o Homebrew antes: https://brew.sh) ou https://cmake.org/download/ | https://cmake.org/download/ → *Windows x64 Installer*. Na instalação marque **"Add CMake to the system PATH"** |
| Git | Já vem com o Xcode (`xcode-select --install` se faltar) | https://git-scm.com/download/win |

### Dependências do código (JUCE e whisper.cpp)

**Não precisa baixar nada à mão.** Na primeira vez que rodar o `cmake -B build`, o próprio CMake
baixa o JUCE 8 e o whisper.cpp para dentro da pasta `build/_deps` (precisa de internet só nessa
primeira vez; são ~200 MB).

Se preferir usar cópias já clonadas (ou estiver sem internet), aponte para elas:
```
cmake -B build -DJUCE_DIR=/caminho/JUCE -DWHISPER_DIR=/caminho/whisper.cpp
```

### Modelos de IA (para rodar, não para compilar)

Ver a [seção 6](#6-baixar-os-modelos-obrigatório) — dois arquivos `.bin`, ~470 MB no total.

---

## 2. Compilar no macOS

Testado com Apple Silicon e Intel; gera binário **universal** (arm64 + x86_64), macOS 11+.
Usa **Metal** (GPU) para o Whisper automaticamente.

```bash
# 1. Ferramentas (uma vez)
xcode-select --install          # se ainda não tiver as ferramentas de linha de comando
brew install cmake              # se ainda não tiver o CMake

# 2. Código
git clone https://github.com/SEU-USUARIO/TranscriberLive.git
cd TranscriberLive

# 3. Configurar (baixa JUCE e whisper.cpp na primeira vez — demora alguns minutos)
cmake -B build -G Xcode

# 4. Compilar os dois plugins (VST3 + AU) e o app Display
cmake --build build --config Release --target \
    TranscriberLive_VST3 TranscriberLive_AU \
    TranscriberLiveDisplay_VST3 TranscriberLiveDisplay_AU TranscriberLiveDisplayApp
```

A primeira compilação leva de 10 a 25 minutos (o whisper.cpp e o JUCE são grandes). As
seguintes, poucos segundos.

Onde os arquivos ficam:

```
build/TranscriberLive_artefacts/Release/VST3/Transcriber Live Receiver.vst3
build/TranscriberLive_artefacts/Release/AU/Transcriber Live Receiver.component
build/TranscriberLiveDisplay_artefacts/Release/VST3/Transcriber Live Display.vst3
build/TranscriberLiveDisplay_artefacts/Release/AU/Transcriber Live Display.component
build/TranscriberLiveDisplayApp_artefacts/Release/Transcriber Live Display.app     (app, sem áudio)
```

Por padrão o build **já copia** os plugins para `~/Library/Audio/Plug-Ins/VST3` e
`~/Library/Audio/Plug-Ins/Components`. Para não copiar: `cmake -B build -G Xcode -DTRANSCRIBER_COPY_PLUGIN=OFF`.

Também dá para abrir `build/TranscriberLive.xcodeproj` no Xcode e compilar por lá
(scheme `TranscriberLive_VST3`, etc., configuração *Release*).

> **Gatekeeper**: os plugins não são assinados/notarizados. Se o host recusar carregar, rode uma
> vez: `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Transcriber\ Live\ *.vst3`
> (e o mesmo para `Components/*.component`). Para AU, o Logic/GarageBand exigem também
> `killall -9 AudioComponentRegistrar` ou reiniciar.

---

## 3. Compilar no Windows

Testado com Visual Studio 2022, Windows 10/11 x64.

Abra o **"x64 Native Tools Command Prompt for VS 2022"** (menu Iniciar → Visual Studio 2022)
ou o PowerShell normal se o CMake estiver no PATH.

```bat
:: 1. Código
git clone https://github.com/SEU-USUARIO/TranscriberLive.git
cd TranscriberLive

:: 2. Configurar (baixa JUCE e whisper.cpp na primeira vez)
cmake -B build -G "Visual Studio 17 2022" -A x64

:: 3. Compilar os dois plugins (VST3) e o app Display
cmake --build build --config Release --target TranscriberLive_VST3 TranscriberLiveDisplay_VST3 TranscriberLiveDisplayApp
```

Onde os arquivos ficam:

```
build\TranscriberLive_artefacts\Release\VST3\Transcriber Live Receiver.vst3\
build\TranscriberLiveDisplay_artefacts\Release\VST3\Transcriber Live Display.vst3\
build\TranscriberLiveDisplayApp_artefacts\Release\Transcriber Live Display.exe     (app, sem áudio)
```

Por padrão o build tenta copiar os `.vst3` para `C:\Program Files\Common Files\VST3\` — isso
exige **prompt como Administrador**. Se não quiser, configure com
`-DTRANSCRIBER_COPY_PLUGIN=OFF` e copie à mão (seção 5).

Também dá para abrir `build\TranscriberLive.sln` no Visual Studio, escolher *Release* e
compilar os projetos `TranscriberLive_VST3` e `TranscriberLiveDisplay_VST3`.

> **AU não existe no Windows** (é formato só da Apple). O Windows usa o VST3.

> **CPU sem AVX2** (máquinas bem antigas): se o plugin fechar o host ao carregar o modelo,
> configure com `-DGGML_AVX2=OFF -DGGML_FMA=OFF` e recompile.

---

## 4. Compilar pelo GitHub Actions (sem instalar nada)

O repositório tem um workflow em `.github/workflows/build.yml`. Cada `git push` na branch
`main` compila **os dois plugins no macOS e no Windows** nos servidores do GitHub.

1. Faça o push (ou vá em **Actions → Build plugin → Run workflow**).
2. Espere ~15–25 minutos.
3. Em **Actions**, abra a execução e baixe os artefatos `TranscriberLive-macOS` e
   `TranscriberLive-Windows` (zips com VST3/AU/app).
4. Instale conforme a seção 5.

Funciona em repositório privado dentro da cota gratuita do GitHub (2.000 min/mês; o build
do macOS conta 10× — ainda sobra bastante para alguns builds por mês).

---

## 5. Instalar os plugins

| Formato | macOS | Windows |
|---|---|---|
| VST3 | `~/Library/Audio/Plug-Ins/VST3/` (só seu usuário) ou `/Library/Audio/Plug-Ins/VST3/` (todos) | `C:\Program Files\Common Files\VST3\` |
| AU | `~/Library/Audio/Plug-Ins/Components/` | — |
| App Display | Qualquer pasta (ex.: `/Applications`) | Qualquer pasta |

Copie a pasta `Transcriber Live Receiver.vst3` e `Transcriber Live Display.vst3` inteiras
(no Windows um `.vst3` é uma pasta). Depois faça o host **re-escanear plugins**:

- **SuperRack Performer** (Waves): Settings → Plugins → *Rescan* (precisa da versão **V14 ou
  mais nova**, que aceita VST3 de terceiros). O SuperRack **SoundGrid** roda só plugins Waves —
  não serve.
- **REAPER**: Options → Preferences → Plug-ins → VST → *Re-scan*.
- **Live Professor**: Preferences → Plugins → *Scan*.
- **Logic / MainStage**: reinicie; se o AU não aparecer, `killall -9 AudioComponentRegistrar`.

---

## 6. Baixar os modelos (obrigatório)

O Receiver precisa de dois arquivos na pasta abaixo. O VAD é carregado sozinho; os modelos
Whisper que estiverem na pasta aparecem na lista **Modelo** dentro do Receiver (pode deixar
vários e trocar na hora). Sem escolher pasta, sem diálogo de arquivo.

| Sistema | Pasta |
|---|---|
| macOS | `~/Library/Application Support/TranscriberLive/models/` |
| Windows | `%APPDATA%\TranscriberLive\models\` (= `C:\Users\SEU-NOME\AppData\Roaming\TranscriberLive\models\`) |

(Crie a pasta se não existir. Só arquivos que começam com `ggml-` são reconhecidos.)

| Arquivo | Tamanho | Para quê | Link direto |
|---|---|---|---|
| `ggml-silero-v5.1.2.bin` | 0,9 MB | **VAD** (detector de voz) — é o que rejeita ruído e vazamento. Sempre use. | https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v5.1.2.bin |
| `ggml-small.bin` | 466 MB | Whisper — bom equilíbrio para PT-BR em CPU. **Comece por este.** | https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin |
| `ggml-medium.bin` | 1,5 GB | Melhor qualidade; Apple Silicon (Metal) aguenta em tempo real | https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium.bin |
| `ggml-large-v3-turbo.bin` | 1,6 GB | Melhor ainda e rápido em Apple Silicon | https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo.bin |
| `ggml-small-q5_1.bin` | 190 MB | Versão leve do small — Windows sem GPU / notebook fraco | https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small-q5_1.bin |

Lista completa: https://huggingface.co/ggerganov/whisper.cpp/tree/main

> `ggml-base.bin` e `ggml-tiny.bin` funcionam mas erram bastante em português.

Pelo terminal (macOS):
```bash
mkdir -p ~/Library/Application\ Support/TranscriberLive/models
cd ~/Library/Application\ Support/TranscriberLive/models
curl -L -O https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v5.1.2.bin
curl -L -O https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin
```
PowerShell (Windows):
```powershell
$d = "$env:APPDATA\TranscriberLive\models"; New-Item -ItemType Directory -Force $d | Out-Null
Invoke-WebRequest https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v5.1.2.bin -OutFile "$d\ggml-silero-v5.1.2.bin"
Invoke-WebRequest https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin -OutFile "$d\ggml-small.bin"
```

---

## 7. Usar no show

### Receiver (um por canal de mic)

1. Insira **Transcriber Live Receiver** no canal do microfone. O host já entrega o sinal do
   canal — não há seleção de entrada no plugin (se vier estéreo, ele soma).
2. **Nome** (ex.: Cantor, Baixo), **Cor** (clique no botão), **Importância**
   (Normal / Importante / Urgente — muda o destaque do balão e a intensidade do flash),
   **Flash** (liga/desliga o piscar da tela do Display quando este canal fala).
3. **Modelo**: lista o que está na pasta de modelos; escolha o que rodar melhor na máquina.
   A linha de status confirma "Modelo: ... | VAD ok".
4. Os três knobs (arraste vertical, bom para touch): **Gate** (linha no medidor — deixe um
   pouco acima do vazamento do palco com o mic aberto e ninguém falando), **Sensib. VAD**
   (mais alta pega fala mais baixa, mas deixa passar mais coisa; comece em 0,5) e
   **Fim de frase** (silêncio que fecha a frase, 700 ms; quem fala pausado: aumente).
5. **Parciais**: mostra o texto enquanto a pessoa ainda fala (desligue em CPU fraca).
6. **Rede...**: só se o Display estiver em **outro computador** — coloque o IP dele
   (ex.: `192.168.0.20:47800`). Na mesma máquina, deixe vazio.
7. Para parar de transcrever, dê bypass ou remova o plugin no host (não há botão liga/desliga).
8. A linha de baixo mostra a última frase reconhecida, só para conferência — o
   acompanhamento é no Display e no celular.

### Display (um por computador da técnica)

1. Insira **Transcriber Live Display** em qualquer canal, ou abra o **app** Display (não usa
   áudio). O celular funciona mesmo sem Display aberto — basta um Receiver no host.
2. A linha de status mostra `UDP 47800 ok` e o endereço para o celular (ou "espelhando o
   Display principal desta máquina", quando outro programa — ex.: o app — já tem as portas).
   Os Receivers aparecem na barra lateral em poucos segundos (heartbeat a cada 2 s; "offline"
   após 7 s sem sinal).
3. Balões: cor da pessoa; Importante = negrito + "!"; Urgente = borda vermelha + fonte maior.
   Parcial (a pessoa ainda falando) aparece apagada e em itálico, e vira a frase final no lugar.
4. Clique num participante para ver só ele; **Todos** volta ao normal.
5. **Flash** liga/desliga o piscar (1× normal, 2× importante, 3× urgente, na cor do canal).
6. **Limpar tudo**, **A- / A+**, **Rede...** (portas e liga/desliga o servidor web).

### Limitação importante

VAD + filtros rejeitam bem ruído, palmas, instrumentos sem voz e vazamento fraco. **Canto e
música com vocal** dentro do mic são "voz" para o detector, e o Whisper vai tentar
transcrevê-los. Na prática: TRANSCREVER ligado entre músicas / em passagens faladas, ou
controlado por snapshot/MIDI.

---

## 8. Celular / tablet

No mesmo Wi-Fi do computador do Display, abra no navegador o endereço que aparece na linha
de status do Display, por exemplo `http://192.168.0.20:47801`. A página mostra a mesma
conversa ao vivo, com barra de participantes, filtro por clique, flash, fonte A-/A+ e Limpar.
Vários dispositivos ao mesmo tempo. Dica: "Adicionar à Tela de Início" (iOS/Android) abre em
tela cheia.

---

## 9. Rede e portas

| Porta | Protocolo | Quem usa |
|---|---|---|
| **47800** | UDP | Receivers → Display. Um Display por máquina nessa porta (mude em **Rede...**). |
| **47801** | TCP/HTTP | Display → celular/tablet. Libere no firewall do computador do Display. |

Tudo fica na rede local; nada sai para a internet. O celular precisa estar no **mesmo Wi-Fi**
(e o roteador não pode ter "isolamento de clientes/AP isolation" ligado).

Firewall no Windows: na primeira execução o Windows pergunta — marque "Redes privadas" e
permita. Se não perguntou: Segurança do Windows → Firewall → Permitir um aplicativo → adicione
o host (SuperRack/REAPER) ou o `Transcriber Live Display.exe`.

Firewall no macOS: Ajustes → Rede → Firewall → Opções → permitir o host.

---

## 10. Parâmetros automatizáveis (Receiver)

| ID | Nome | Faixa | Padrão |
|---|---|---|---|
| `gate` | Gate | −80 … −10 dBFS | −45 |
| `hold` | Fim de frase | 300 … 2000 ms | 700 |
| `vadsens` | Sensibilidade VAD | 0 … 1 | 0,5 |
| `partials` | Parciais | on/off | on |

Nome, cor, importância, flash e endereço do Display são salvos no estado do plugin (sessão /
snapshot do host), não são parâmetros automatizáveis.

---

## 11. Solução de problemas

| Sintoma | O que fazer |
|---|---|
| `cmake` não encontrado | Instale o CMake e marque "Add to PATH" (Windows) / `brew install cmake` (macOS). Feche e reabra o terminal. |
| Windows: "No CMAKE_CXX_COMPILER could be found" | Instale o Visual Studio 2022 com a carga "Desenvolvimento para desktop com C++" e use o *x64 Native Tools Command Prompt*. |
| macOS: "xcodebuild requires Xcode" | Abra o Xcode uma vez; `sudo xcode-select -s /Applications/Xcode.app`. |
| Configuração trava baixando JUCE/whisper | Internet/proxy. Clone os dois à mão e use `-DJUCE_DIR=... -DWHISPER_DIR=...`. |
| Erro de permissão ao copiar o `.vst3` (Windows) | Rode o prompt como Administrador, ou `-DTRANSCRIBER_COPY_PLUGIN=OFF` e copie à mão. |
| O plugin não aparece no host | Re-escaneie; confira a pasta de instalação; no macOS remova a quarentena (`xattr -dr com.apple.quarantine ...`). SuperRack: precisa ser **Performer V14+**. |
| "Nenhum modelo carregado" / lista Modelo vazia | Coloque os `.bin` na pasta de modelos (seção 6). A lista atualiza sozinha em 5 s. |
| O host ou o app fechava sozinho quando o celular desconectava (v0.2) | Corrigido na v0.3: os sockets agora ignoram `SIGPIPE` (o macOS matava o processo ao escrever num socket fechado). |
| Transcreve com atraso grande / o host engasga | Modelo grande demais para a CPU. Use `small` ou `small-q5_1`. Desligue "Mostrar parciais". |
| Muita coisa errada / inventada | Suba o **Gate**, baixe a **Sensib. VAD**, confira se o VAD Silero está carregado (linha de status). Desligue TRANSCREVER durante a música. |
| Display diz "em uso por outro programa - tentando..." e não espelha | Outro programa (não o Transcriber) usa a porta 47801. Mude as portas em **Rede...** (vale para todos os plugins e o app na máquina). |
| Receivers não aparecem no Display | Confira o IP:porta no Receiver; firewall; se estão em computadores diferentes, os dois no mesmo Wi-Fi/cabo. |
| Celular não abre a página | Mesmo Wi-Fi; firewall liberado na porta 47801; digite `http://` (não `https`). |

---

## 12. Como funciona por dentro

```
host (SuperRack Performer / REAPER / Live Professor)  →  canal do mic
      │
      ▼  processBlock (thread de áudio — nunca bloqueia)
  entrada do host (mono; estéreo é somado)  →  medidor  →  anti-alias 7 kHz  →  reamostra p/ 16 kHz  →  FIFO lock-free
      │
      ▼  worker thread (TranscriptionEngine)
  janelas de 32 ms  →  gate por nível (dBFS)  +  VAD Silero (whisper.cpp)
      │
      ├─ início de fala: começa segmento com 250 ms de pré-roll
      ├─ enquanto fala: a cada 1,5 s roda Whisper e manda texto PARCIAL
      └─ 700 ms de silêncio (ou 10 s): roda Whisper → frase FINAL
                                         │
                                         ▼
      filtros anti-alucinação (no_speech_prob, frases-clichê do Whisper,
      repetição de n-gramas, razão de janelas com voz no segmento)
                                         │
                                         ▼
      UDP 47800 (JSON: hello / msg / clear)  →  Hub (em qualquer processo da ferramenta)
                                                  →  Display (balões) + HTTP 47801 (SSE) p/ celular
```

---

## 13. Estrutura do código

```
CMakeLists.txt                     dois targets: TranscriberLive (Receiver) e TranscriberLiveDisplay
.github/workflows/build.yml        build automático macOS + Windows
Source/Common/
  TranscriptionEngine.*            FIFO, gate, VAD Silero, segmentação, Whisper, filtros
  Protocol.h                       mensagens JSON Receiver -> Display (hello / msg / clear)
  MessageBus.h                     BusSender (UDP) / BusListener
  MessageStore.h                   participantes + histórico (Display)
  HttpServer.h                     servidor web embutido (/, /state, /events SSE, /clear)
  Hub.*                            singleton por processo: store + UDP + web; primário/espelho
  SocketSafety.h                   proteção SIGPIPE (SO_NOSIGPIPE / SIG_IGN)
  DarkLookAndFeel.h                tema escuro neutro dos dois plugins
Source/Receiver/                   processor (medidor, reamostragem, identidade) + editor
Source/Display/                    processor + editor (balões, sidebar, flash) + DisplayApp.cpp (app sem áudio)
Resources/display.html             página para celular/tablet (embutida no binário)
Resources/icon.svg / icon_*.png    ícone
Tests/EngineTest.cpp               teste de console do motor: EngineTest modelo vad audio.wav [gateDb] [idioma] [nomeCanal]
Tests/ReceiverTest.cpp             app de teste do Receiver inteiro (processor + hub + editor): ReceiverTest audio.wav [idioma] [nome]
```

Teste de console (opcional): `cmake -B build -DTRANSCRIBER_BUILD_TESTS=ON` e
`cmake --build build --target EngineTest`. Com `nomeCanal`, ele também manda as frases para
um Display em `127.0.0.1:47800` — bom para testar o Display sem mic.

## Próximos passos possíveis

- Classificador fala × canto (ex.: YAMNet/PANNs via ONNX) para não transcrever a música.
- Talkback reverso (técnica → cantor) por texto-para-voz no IEM.
- Log das frases em arquivo `.txt` por show.
