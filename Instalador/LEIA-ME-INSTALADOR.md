# Instalador do Transcriber Live (macOS)

## Por que deu "o pacote está tentando instalar conteúdo no volume do sistema"

Desde o Catalina o macOS recusa pacotes cujo *payload* aponte para dentro da pasta
do usuário (`~/Library/Application Support`, por exemplo). O instalador roda como root,
antes de qualquer sessão de usuário, então "a home do usuário" não existe para ele —
e o macOS bloqueia o pacote inteiro em vez de adivinhar.

A solução é a mesma do seu instalador do SpotifyDownload: **o payload leva só um zip
para uma pasta permitida** (`/Applications`), e o `postinstall` move esse zip para um
temporário, descompacta e leva cada coisa para o lugar certo — inclusive a pasta do
usuário, que o payload não consegue tocar. No fim o zip é apagado, então não sobra nada
em `/Applications`.

Fluxo:

```
payload  ->  /Applications/TranscriberLive-payload.zip
postinstall -> move para /private/tmp, extrai e distribui:
                 VST3/          -> /Library/Audio/Plug-Ins/VST3
                 Components/    -> /Library/Audio/Plug-Ins/Components
                 Applications/  -> /Applications
                 models/        -> /Library/Application Support/TranscriberLive/models
             -> apaga o zip
```

## Arquivos aqui

| Arquivo | Para que serve |
|---|---|
| `montar-instalador.command` | **duplo clique** — monta o payload e gera o `.pkg` inteiro |
| `capa.png` / `capa-dark.png` | capa lateral da janela do Installer (claro / escuro) |
| `scripts/postinstall` | o script que distribui tudo depois da instalação |
| `postinstall-minimo.sh` | a mesma lógica em 60 linhas, só para depurar na mão |
| `Introducao.txt` | tela de introdução do instalador |
| `binarios/` | *(você cria)* onde ficam os `.vst3`, `.component` e `.app` |
| `modelos/` | *(você cria, opcional)* `ggml-*.bin` para já sair instalado |

## A capa lateral

O painel da esquerda da janela do instalador é o elemento `<background>` do
`distribution.xml`, e o `montar-instalador.command` já o gera para você: basta os
arquivos `capa.png` e `capa-dark.png` existirem aqui.

- `capa.png` — arte escura sobre fundo transparente, para o modo claro do macOS
- `capa-dark.png` — a versão esmaecida/clara, para o modo escuro

As duas já estão prontas aqui, em 700 × 1400 px (PNG com transparência). O painel real
tem cerca de 165 × 380 pt, e o alinhamento usado é `bottomleft` com escala
`proportional` — ou seja, a arte encosta embaixo à esquerda e é reduzida sem distorcer.
Por isso o logo em cima e o QR embaixo funcionam bem, e o miolo vazio some.

Se quiser trocar a arte, mantenha a proporção perto de 1:2 e deixe transparência no
lugar do fundo: fundo branco chapado aparece como um retângulo branco no modo escuro.

## O caminho mais fácil: deixar o GitHub montar

O `build.yml` tem um job `installer-macos` que roda depois da compilação: baixa os
binários, monta o `.pkg` completo e sobe como artefato **`TranscriberLive-macOS-Installer`**
na mesma página da build. Não precisa de nada instalado no seu Mac.

## Montando no seu Mac

1. Baixe os binários do GitHub Actions e descompacte dentro de uma pasta `binarios`
   aqui do lado. Não precisa organizar — o script procura sozinho.
2. Opcional: crie uma pasta `modelos` com `ggml-*.bin` para **embutir** algum modelo
   no `.pkg` em vez de deixar o cliente baixar. Sem ela, o instalador sai leve
   (só o VAD, 0,9 MB) e o cliente escolhe o que baixar na tela Personalizar.
3. Duplo clique em `montar-instalador.command`.
4. O `.pkg` sai em `saida/TranscriberLive-0.4.pkg`.

Não precisa do Packages nem de nenhum app extra — usa o `pkgbuild` e o `productbuild`
que já vêm no macOS.

## Se preferir usar o Packages (interface gráfica)

Agora fica simples, porque o zip vai por payload:

1. Novo projeto → **Raw Package**.
2. Aba **Payload**: em `/Applications`, arraste o `scripts/TranscriberLive-payload.zip`
   (rode o `montar-instalador.command` uma vez para gerá-lo). **Só o zip**, nada dentro
   de `~/Library` — é isso que resolve o erro.
3. Aba **Scripts** → *Post-installation*: escolha `scripts/postinstall`.
4. Aba **Settings**: identificador `com.jhonatanmiikael.transcriberlive`, versão 0.4,
   "Require admin password" ligado.
5. Aba **Presentation** → *Background*: escolha `capa.png` (e `capa-dark.png` no campo
   do modo escuro), alinhamento **Bottom Left**, escala **Proportional**.
   Em *Introduction*, aponte o `Introducao.txt`.

O `postinstall` procura o zip em vários lugares (`/Applications`, ao lado do script,
`/private/tmp`), então funciona tanto pelo caminho automático quanto pelo Packages.

Pelo Packages você perde a tela de escolha dos modelos — ela vem do
`distribution.xml` que o `montar-instalador.command` gera. Se quiser as duas coisas,
use o caminho automático.

## O que o script instala, e onde

| Conteúdo | Destino |
|---|---|
| `.vst3` | `/Library/Audio/Plug-Ins/VST3` |
| `.component` (AU) | `/Library/Audio/Plug-Ins/Components` |
| `.app` | `/Applications` |
| `ggml-*.bin` | `/Library/Application Support/TranscriberLive/models` |

Ele ainda: remove a quarentena dos bundles,
aplica assinatura ad-hoc se algum bundle vier sem assinatura, reinicia o
`AudioComponentRegistrar` para o AU aparecer sem reiniciar o Mac, e avisa se algum host
estiver aberto na hora.

Modelos que já existem **não são sobrescritos** — quem já tem um `ggml-medium` não perde
o download ao reinstalar.

## Modelos de voz: escolha na instalação

O `.pkg` mostra a tela **Personalizar** com os modelos em caixinhas. Isso não é
um truque: cada modelo é um sub-pacote sem payload cujo `postinstall` baixa
aquele arquivo do Hugging Face. É o mesmo mecanismo que o Installer usa para
qualquer instalação opcional, então a tela é nativa.

| Modelo | Tamanho | Quando usar |
|---|---|---|
| Silero VAD | 0,9 MB | **vai dentro do `.pkg`**, sempre instalado |
| tiny | 74 MB | Mac fraco, aceita errar |
| base | 141 MB | Mac modesto |
| **small** | **465 MB** | **marcado por padrão — recomendado para show** |
| medium | 1,4 GB | mais preciso, exige CPU boa |
| large-v3-turbo | 1,5 GB | o melhor, só em Mac forte |

O VAD é embutido de propósito: são 0,9 MB e sem ele o Receiver não separa fala
de vazamento. Assim o plugin funciona mesmo instalando num Mac sem internet.

Cada download tem o **SHA-256 conferido**; arquivo corrompido é descartado em vez
de instalado. Modelo que já existe na pasta não é baixado nem sobrescrito. E um
download que falha **não derruba a instalação** — os plugins já estão no lugar, e
o log diz onde colocar o `ggml-*.bin` depois.

Os modelos vão para `/Library/Application Support/TranscriberLive/models`, não
para a home do usuário. O instalador roda como root, e "a home certa" é um chute
quando existe mais de uma conta ou quando alguém digita a senha de outro admin.
O plugin procura nas duas pastas, então quem já tinha modelos na pasta antiga
não perde nada.

**Uma coisa honesta sobre a experiência:** enquanto baixa, o Installer.app só
mostra "Executando scripts do pacote" com a barra indeterminada — ele não deixa
um pacote desenhar progresso próprio sem um plugin de instalador em Objective-C.
Para o cliente não achar que travou, o script dispara notificações do macOS
("Baixando o modelo small...", "Modelo small pronto") e escreve o progresso no
log. Com o `small` são uns 2 minutos numa internet boa; com o `large-v3-turbo`,
bem mais.

Para acompanhar o download durante um teste:

```bash
tail -f /private/tmp/transcriberlive-install.log
```

## Testando antes de mandar para alguém

```bash
sudo installer -pkg "saida/TranscriberLive-0.4.pkg" -target /
cat /private/tmp/transcriberlive-install.log
```

O log mostra cada arquivo copiado e qualquer erro. Se algo falhar, o instalador acusa
falha em vez de dizer "sucesso" com o sistema pela metade.

Para testar o script sozinho, sem tocar no sistema:

```bash
TL_TEST_ROOT=/tmp/teste bash scripts/postinstall
find /tmp/teste
```

## Assinatura e notarização

O `.pkg` sai **sem assinatura**. No Mac do cliente o macOS vai reclamar de
"desenvolvedor não identificado" e ele terá que liberar em Ajustes → Privacidade e
Segurança → "Abrir mesmo assim".

Para vender sem esse atrito você precisa de uma conta de desenvolvedor Apple
(99 dólares/ano). Com ela, o fluxo é assinar os bundles, assinar o pkg e notarizar:

```bash
codesign --force --options runtime --sign "Developer ID Application: SEU NOME (TEAMID)" \
         "Transcriber Live Receiver.vst3"
productsign --sign "Developer ID Installer: SEU NOME (TEAMID)" \
            saida/TranscriberLive-0.4.pkg saida/TranscriberLive-0.4-assinado.pkg
xcrun notarytool submit saida/TranscriberLive-0.4-assinado.pkg \
      --apple-id SEU@EMAIL --team-id TEAMID --password SENHA-DE-APP --wait
xcrun stapler staple saida/TranscriberLive-0.4-assinado.pkg
```

Enquanto não tiver a conta, vale escrever no e-mail de entrega como liberar o
instalador — é a dúvida número um de quem compra plugin não assinado.
