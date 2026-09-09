#!/usr/bin/env python3
"""
Emissao de licenca para rodar no GitHub Actions (ou em qualquer lugar sem tela).

Le tudo de variaveis de ambiente, escreve o .txt da licenca e registra a venda
num CSV. Nao imprime a chave privada em lugar nenhum.

    TL_CHAVE     caminho do arquivo da chave privada        (obrigatorio)
    TL_MAQUINA   ID da maquina, em qualquer formato          (obrigatorio)
    TL_NOME      titular                                     (obrigatorio)
    TL_EMAIL     e-mail do titular                           (obrigatorio)
    TL_DIAS      validade em dias; vazio ou 0 = perpetua
    TL_NOTA      observacao livre (ex.: "pago via Pix")
    TL_SAIDA     pasta onde gravar o .txt        (padrao: ./saida)
    TL_REGISTRO  CSV do historico     (padrao: ./registro/licencas-emitidas.csv)

Saida: imprime um resumo e, se GITHUB_OUTPUT existir, grava serial/arquivo/
validade para os passos seguintes do workflow.
"""

import csv
import os
import re
import secrets
import sys
from datetime import datetime, timedelta, timezone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import licenca as core


def normaliza_id(txt):
    """Aceita 'tl 1147 89df 849c', '114789DF849C', 'TL-1147-89DF-849C'..."""
    h = re.sub(r"[^0-9A-Fa-f]", "", re.sub(r"^\s*[Tt][Ll]", "", (txt or "").strip())).upper()
    if len(h) != 12:
        return None
    return "TL-%s-%s-%s" % (h[0:4], h[4:8], h[8:12])


def exige(nome):
    v = (os.environ.get(nome) or "").strip()
    if not v:
        sys.exit("ERRO: falta %s" % nome)
    return v


def main():
    caminho_chave = exige("TL_CHAVE")
    if not os.path.exists(caminho_chave):
        sys.exit("ERRO: nao achei a chave privada. No Actions isso quer dizer que o "
                 "segredo TRANSCRIBER_PRIVATE_KEY nao esta configurado no repositorio.")

    mid = normaliza_id(exige("TL_MAQUINA"))
    if not mid:
        sys.exit("ERRO: ID de maquina invalido. Esperado 12 digitos hexadecimais, "
                 "no formato TL-XXXX-XXXX-XXXX (o plugin mostra e copia esse ID).")

    nome = exige("TL_NOME")
    email = exige("TL_EMAIL")

    dias_txt = (os.environ.get("TL_DIAS") or "").strip()
    try:
        dias = int(dias_txt) if dias_txt else 0
    except ValueError:
        sys.exit("ERRO: TL_DIAS precisa ser um numero inteiro (ou vazio para perpetua).")
    if dias < 0 or dias > 3650:
        sys.exit("ERRO: TL_DIAS fora do intervalo (0 a 3650).")

    nota = (os.environ.get("TL_NOTA") or "").strip()
    pasta_saida = os.environ.get("TL_SAIDA") or "saida"
    registro = os.environ.get("TL_REGISTRO") or os.path.join("registro", "licencas-emitidas.csv")

    priv = core.carregar_priv(caminho_chave)

    agora = datetime.now(timezone.utc)
    payload = {
        "v": 1,
        "prod": "transcriber-live",
        "nome": nome,
        "email": email,
        "mid": mid,
        "serial": secrets.token_hex(4).upper(),
        "iat": agora.strftime("%Y-%m-%d"),
    }
    if dias:
        payload["exp"] = (agora + timedelta(days=dias)).strftime("%Y-%m-%d")
    if nota:
        payload["nota"] = nota

    lic = core.assinar(payload, priv)

    # confere a propria assinatura antes de entregar: se algo saiu torto, o
    # cliente nao pode ser o primeiro a descobrir
    conferido, erro = core.conferir(lic, {"e": priv["e"], "n": priv["n"]})
    if erro or conferido != payload:
        sys.exit("ERRO: a licenca gerada nao passou na propria verificacao (%s)." % (erro or "conteudo diferente"))

    os.makedirs(pasta_saida, exist_ok=True)
    arquivo = os.path.join(pasta_saida,
                           "licenca-%s-%s.txt" % (core.slug(nome), payload["serial"]))
    with open(arquivo, "w") as fh:
        fh.write(lic + "\n")

    validade = "perpetua" if not dias else "%d dias (ate %s)" % (dias, payload["exp"])

    # historico: so os dados da venda, nunca o texto da licenca
    os.makedirs(os.path.dirname(registro) or ".", exist_ok=True)
    novo = not os.path.exists(registro)
    with open(registro, "a", newline="") as fh:
        w = csv.writer(fh)
        if novo:
            w.writerow(["emitida_em", "nome", "email", "maquina", "serial",
                        "validade", "nota", "arquivo"])
        w.writerow([agora.strftime("%Y-%m-%d %H:%M UTC"), nome, email, mid,
                    payload["serial"], payload.get("exp", "perpetua"), nota,
                    os.path.basename(arquivo)])

    print("Licenca emitida")
    print("  titular : %s <%s>" % (nome, email))
    print("  maquina : %s" % mid)
    print("  serial  : %s" % payload["serial"])
    print("  validade: %s" % validade)
    print("  arquivo : %s" % arquivo)

    saida_gh = os.environ.get("GITHUB_OUTPUT")
    if saida_gh:
        with open(saida_gh, "a") as fh:
            fh.write("serial=%s\n" % payload["serial"])
            fh.write("arquivo=%s\n" % arquivo)
            fh.write("validade=%s\n" % validade)
            fh.write("maquina=%s\n" % mid)


if __name__ == "__main__":
    main()
