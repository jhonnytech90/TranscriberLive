# Instalador do Transcriber Live (macOS)

## Por que deu "o pacote está tentando instalar conteúdo no volume do sistema"

Desde o Catalina o macOS recusa pacotes cujo *payload* aponte para dentro da pasta
do usuário (`~/Library/Application Support`, por exemplo). O instalador roda como root,
antes de qualquer sessão de usuário, então "a home do usuário" não existe para ele —
e o macOS bloqueia o pacote inteiro em vez de adivinhar.

A solução é a que você já pensou, e é a que os plugins comerciais usam: **o pacote não
instala nada por payload**. Ele carrega apenas um zip e um script; o script descompacta
num temporário e move cada coisa para o lugar certo, incluindo a pasta do usuário.

## Arquivos aqui

| Arquivo | Para que serve |
|---|---|
| `montar-instalador.command` | **duplo clique** — monta o payload e gera o `.pkg` inteiro |
| `scripts/postinstall` | o script que copia tudo depois da instalação |
| `Introducao.txt` | tela de introdução do instalador |
| `binarios/` | *(você cria)* onde ficam os `.vst3`, `.component` e `.app` |
| `modelos/` | *(você cria, opcional)* `ggml-*.bin` para já sair instalado |

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

1. Novo projeto → **Raw Package**.
2. Aba **Payload**: deixe **vazia**. É isso que resolve o erro.
3. Aba **Scripts** → *Post-installation*: escolha `scripts/postinstall`.
4. Aba **Scripts** → *Additional Resources*: arraste o `TranscriberLive-payload.zip`
   (rode o `montar-instalador.command` uma vez só para gerar o zip; ele para de ser
   necessário depois que o pkg é gerado pelo Packages).
5. Aba **Settings**: identificador `com.jhonatanmiikael.transcriberlive`, versão 0.4,
   "Require admin password" ligado.

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
