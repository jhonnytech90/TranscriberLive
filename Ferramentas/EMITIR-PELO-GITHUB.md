# Emitir licença pelo celular, sem o Mac ligado

O GitHub Actions faz o papel do servidor. Você abre o app do GitHub no celular,
preenche quatro campos e recebe a licença pronta. O Mac pode estar desligado.

## Por que não dá para fazer no GitHub Pages

O Pages só serve arquivos estáticos — não roda código seu. Para assinar uma
licença é preciso a **chave privada**, então um "gerador no Pages" só
funcionaria colocando a chave dentro da página. Qualquer pessoa abriria o
código-fonte, pegaria a chave e passaria a emitir licenças infinitas para
sempre. Isso não é um risco teórico: é o fim do licenciamento.

O Actions é diferente porque o código roda **no servidor do GitHub**, e a chave
fica num *segredo* criptografado que nunca aparece no repositório nem no log.

## Configuração (uma vez, uns 3 minutos)

### 1. Guardar a chave privada como segredo

No seu Mac, copie o conteúdo da chave:

```bash
pbcopy < ~/Documents/TranscriberLive-Licencas/transcriberlive-private.key
```

No GitHub, no repositório **TranscriberLive**:

*Settings → Secrets and variables → Actions → New repository secret*

- **Name:** `TRANSCRIBER_PRIVATE_KEY`
- **Secret:** cole (Cmd+V)

Depois de salvar, nem você consegue ler o valor de volta — só sobrescrever.
**Mantenha o backup da chave no seu Mac e num pendrive**, porque o GitHub não
devolve.

### 2. Exigir sua aprovação a cada emissão (recomendado)

*Settings → Environments → New environment* → nome **`licencas`** →
marque **Required reviewers** e coloque você mesmo.

A partir daí, toda emissão fica parada esperando um toque seu de aprovação. É
sua rede de proteção caso alguém consiga disparar o workflow.

### 3. Receber a licença no Telegram (opcional, mas vale muito)

Sem isso, você baixa o arquivo pelo app do GitHub. Com isso, a licença chega
sozinha no seu Telegram, pronta para encaminhar ao cliente.

1. No Telegram, fale com **@BotFather** → `/newbot` → guarde o token.
2. Mande qualquer mensagem para o seu bot novo.
3. Abra `https://api.telegram.org/bot<SEU_TOKEN>/getUpdates` e pegue o número em
   `"chat":{"id":...}`.
4. Crie dois segredos no repositório: `TELEGRAM_BOT_TOKEN` e `TELEGRAM_CHAT_ID`.

Esse bot é **só de saída** — ele não fica ligado esperando mensagem, então não
precisa de servidor nenhum.

## Emitindo uma licença

No app do GitHub (ou no site):

1. Repositório **TranscriberLive** → aba **Actions**
2. **Emitir licença** → botão **Run workflow**
3. Preencha:
   - **ID da máquina** — o que o cliente copiou do plugin. Aceita
     `TL-1147-89DF-849C`, `114789df849c`, com ou sem espaços.
   - **Nome** e **e-mail** do titular
   - **Validade** — Vitalícia, Mensal, Trimestral, Anual, Teste de 3 ou 7 dias
   - **Observação** — opcional (ex.: "pago via Pix", "segunda máquina")
4. **Run workflow**

Em uns 30 segundos a licença está pronta:

- no **Telegram**, se você configurou;
- em **Artifacts**, no rodapé da página do run (arquivo `.txt`);
- e a venda entra em `registro/licencas-emitidas.csv`, versionado no repositório
  — vira seu histórico de vendas, com data, titular, máquina, serial e validade.

O resumo do run mostra tudo em tabela, então dá para conferir no celular antes
de mandar para o cliente.

## O teste de 3 dias

É só escolher **Teste (3 dias)**. A licença carrega uma data de expiração; o
Receiver para de transcrever quando vence e o áudio continua passando normal.
Não existe trial automático — o cliente precisa te mandar o ID da máquina, o que
na prática ajuda: você fica com o contato.

## O que você está aceitando ao usar isto

Vale entender o que muda em relação a emitir no Mac:

- **A chave passa a viver no GitHub.** Está criptografada e não aparece em log,
  mas quem tiver acesso de escrita ao repositório pode alterar um workflow para
  imprimir o segredo. Na prática: **acesso ao repositório = acesso à chave.**
- **Não existe revogação.** A verificação é offline, dentro do plugin. Se a chave
  vazar, a única saída é publicar uma versão nova do plugin com outra chave
  pública — e **todas as licenças já vendidas param de valer**. Por isso o
  cuidado abaixo não é frescura.

O que fazer para dormir tranquilo:

1. **Ligue 2FA** na sua conta do GitHub. Sem isso, o resto não importa.
2. **Não dê acesso de escrita** desse repositório para ninguém. Se um dia
   precisar de ajuda no código, mova a emissão para um repositório privado
   separado (é só copiar `Ferramentas/licenca.py`, `Ferramentas/emitir_ci.py` e
   este workflow, e criar o segredo lá).
3. **Revogue tokens pessoais antigos.** Um token clássico com escopo `repo` abre
   a mesma porta que a sua senha. Se você já colou algum token em conversa,
   e-mail ou anotação, revogue em *Settings → Developer settings → Personal
   access tokens*.
4. **Mantenha o backup da chave** fora do GitHub, em pelo menos dois lugares.
5. Use o **Environment `licencas` com aprovação** do passo 2.

Se preferir o máximo de segurança e não se importar de ligar o Mac, o
`Gerador de Licencas.command` continua funcionando exatamente como antes, com a
chave nunca saindo do seu computador. Os dois caminhos convivem: a mesma chave,
o mesmo formato de licença.
