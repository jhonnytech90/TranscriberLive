# Emissão de licenças — NÃO fica neste repositório

Este repositório é **público**. A emissão de licenças saiu daqui de propósito,
e não por organização: em repositório público ela vazaria dados de cliente por
três caminhos, e dois deles não têm conserto por edição de arquivo.

| O que vaza | Por quê | Dá para corrigir aqui? |
|---|---|---|
| Nome e e-mail do cliente | são `inputs` do `workflow_dispatch`, e a página do run é pública | **não** |
| O arquivo da licença assinada | vai como artefato, e artefato de repo público é baixável por qualquer um | **não** |
| Registro da venda (CSV + mensagem do commit) | commitados no repositório | sim, mas os dois de cima continuam |

Por isso a emissão vive em outro lugar. Duas opções, as duas válidas:

1. **Repositório privado só para emissão.** O job é Linux e dura segundos —
   cabe folgado na cota gratuita. Continua dando para emitir pelo celular,
   que era o motivo de ter saído do Mac.
2. **No próprio Mac**, com `Ferramentas/Gerador de Licencas.command`. Não
   depende de nada online, mas exige o Mac ligado.

## Se for montar o repositório privado

Copie para lá `Ferramentas/emitir_ci.py` e o workflow (que está no histórico
deste repositório, no commit anterior a este arquivo), e crie o secret
`TL_CHAVE_PRIVADA` com o conteúdo de `transcriberlive-private.key`.

## A chave privada

Nunca esteve neste repositório — conferido nos 25 commits do histórico. Ela
existe como **secret**, e secret não vaza ao tornar o repositório público.
Ainda assim, apague o secret daqui depois de mover a emissão: um secret que
ninguém usa é só risco parado.
