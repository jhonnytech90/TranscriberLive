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
                 models/        -> ~/Library/Application Support/TranscriberLive/models
             -> apaga o zip
```

## Arquivos aqui

| Arquivo | Para que serve |
|---|---|
| `montar-instalador.command` | **duplo clique** — monta o payload e gera o `.pkg` inteiro |
| `scripts/postinstall` | o script que distribui tudo depois da instalação |
| `postinstall-minimo.sh` | a mesma lógica em 60 linhas, só para depurar na mão |
| `Introducao.txt` | tela de introdução do instalador |
| `capa.png` | **capa lateral** do instalador (modo claro) |
| `capa-dark.png` | capa lateral no modo escuro |
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

## O caminho automático (recomendado)

1. Baixe os binários do GitHub Actions e descompacte dentro de uma pasta `binarios`
   aqui do lado. Não precisa organizar — o script procura sozinho.
2. Se quiser que o instalador já traga os modelos, crie uma pasta `modelos` com os
   `ggml-*.bin`. Sem ela, o instalador sai leve e o cliente baixa os modelos depois.
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

Se por algum motivo você preferir o pacote **sem payload nenhum** (o zip viajando dentro
de `scripts/`), rode:

```bash
TL_SEM_PAYLOAD=1 ./montar-instalador.command
```

O `postinstall` procura o zip nos dois lugares, então funciona igual nos dois modos.

## O que o script instala, e onde

| Conteúdo | Destino |
|---|---|
| `.vst3` | `/Library/Audio/Plug-Ins/VST3` |
| `.component` (AU) | `/Library/Audio/Plug-Ins/Components` |
| `.app` | `/Applications` |
| `ggml-*.bin` | `~/Library/Application Support/TranscriberLive/models` do usuário logado |

Ele ainda: descobre qual usuário está logado (o script roda como root, então isso é
necessário para a pasta de modelos e para a licença), remove a quarentena dos bundles,
aplica assinatura ad-hoc se algum bundle vier sem assinatura, reinicia o
`AudioComponentRegistrar` para o AU aparecer sem reiniciar o Mac, e avisa se algum host
estiver aberto na hora.

Modelos que já existem **não são sobrescritos** — quem já tem um `ggml-medium` não perde
o download ao reinstalar.

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
