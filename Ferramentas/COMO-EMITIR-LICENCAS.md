# Como emitir licenças (uso interno)

Resumo do esquema: cada licença é um texto **assinado com sua chave RSA privada** e
**presa ao computador do cliente**. O plugin traz só a chave pública e verifica
localmente — funciona offline, sem servidor, sem internet no show. Perpétua por padrão,
1 computador por licença.

## O que você tem

Na pasta `Documentos/TranscriberLive-Licencas` do seu Mac:

| Arquivo | O que é |
|---|---|
| `licenca.py` | o gerador (só Python 3, que o macOS já tem) |
| `transcriberlive-private.key` | **sua chave privada — o segredo do negócio** |
| `transcriberlive-public.txt` | a chave pública (já está embutida no plugin) |

**Faça backup da chave privada** (pendrive + nuvem privada). Se você perdê-la, não consegue
mais emitir licenças novas nem para clientes existentes. Se ela vazar, qualquer pessoa passa
a gerar licenças válidas. Ela nunca deve ir para o GitHub (o `.gitignore` já bloqueia) nem
para dentro do plugin.

## Vendendo: o passo a passo

1. O cliente instala o plugin. Sem licença, o Receiver abre no painel **LICENÇA**, mostrando
   o **ID desta máquina** (algo como `TL-1147-89DF-849C`) e um botão **Copiar**.
2. Ele te manda esse ID, o nome completo e o e-mail.
3. Você emite:

```bash
cd ~/Documents/TranscriberLive-Licencas
python3 licenca.py nova --nome "Joao da Silva" --email joao@exemplo.com --maquina TL-1147-89DF-849C
```

Isso imprime a licença e grava `licenca-joao-da-silva.txt`.

4. Você manda o **conteúdo do arquivo** (o texto todo, das linhas `-----` inclusive) por
   e-mail/WhatsApp. O cliente cola no campo do plugin e clica **Ativar**. Pronto — vale para
   sempre naquele computador, sem internet.

## Variações

```bash
# aluguel / festival: vale 30 dias
python3 licenca.py nova --nome "Fulano" --email f@x.com --maquina TL-.... --dias 30

# anotar o pedido dentro da licença (só você vê, mas fica assinado junto)
python3 licenca.py nova ... --nota "Pedido 1234 - Hotmart"

# conferir uma licença que você emitiu
python3 licenca.py verificar --arquivo licenca-joao-da-silva.txt
```

## Segundo computador

O modelo é 1 licença = 1 computador. Se o cliente trocar de máquina (ou pedir o notebook
também), ele te manda o novo ID e você emite outra licença. Nada impede você de emitir de
graça uma segunda para o mesmo cliente — é sua decisão comercial, o sistema não limita.

## Onde a licença fica na máquina do cliente

| Sistema | Arquivo |
|---|---|
| macOS | `~/Library/Application Support/TranscriberLive/license.key` |
| Windows | `%APPDATA%\TranscriberLive\license.key` |

Vale para todos os hosts e todas as instâncias daquele computador — ativa uma vez, funciona
em qualquer lugar. Para desativar, o botão **Remover licença** no plugin (ou apagar o arquivo).

## O que o cliente pode e não pode fazer

- **Display e a página do celular são livres** — não pedem licença. Sem um Receiver
  licenciado não há nada para mostrar, então o bloqueio fica onde está o valor. Isso é bom
  comercialmente: o computador da técnica pode rodar o Display sem custo.
- Sem licença o **Receiver não carrega o modelo e não transcreve**, mas o **áudio passa
  intacto** — nunca deixa o canal mudo no show.
- Passar a licença para outro computador não funciona: a assinatura confere, mas o ID da
  máquina não, e o plugin recusa dizendo para qual computador ela foi emitida.
- Editar a licença (nome, validade, ID) invalida a assinatura na hora.

## Limites honestos deste esquema

Ele impede a cópia casual — que é o caso real, um técnico passando o arquivo para o outro.
Não impede alguém com conhecimento de engenharia reversa de modificar o binário; nenhum
esquema de licença impede isso (nem os pagos, tipo iLok), e tentar impedir custa caro e
atrapalha o cliente honesto. Se algum dia o produto crescer e isso virar problema, o próximo
passo natural é assinar/notarizar os binários e registrar as ativações num servidor seu.

## Se você quiser trocar as chaves

```bash
python3 licenca.py init --forcar
```

Isso gera um par novo e **invalida todas as licenças já vendidas** — você teria que reemitir
todas e publicar uma versão nova do plugin com a chave pública nova. Só faça isso se
desconfiar que a chave privada vazou.
