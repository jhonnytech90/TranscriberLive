#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Transcriber Live — gerador de licencas (uso do DESENVOLVEDOR, nunca distribuir).

Precisa apenas de Python 3 (o macOS ja tem).

  1) Uma vez, para criar seu par de chaves:
         python3 licenca.py init
     -> grava  transcriberlive-private.key   (SEGREDO: nunca compartilhe, faca backup)
     -> grava  transcriberlive-public.txt     (vai embutida no plugin)

  2) Para cada cliente (ele te manda o "ID desta maquina" que aparece no plugin):
         python3 licenca.py nova --nome "Joao da Silva" --email joao@x.com --maquina TL-1A2B-3C4D-5E6F
     -> imprime a licenca e grava  licenca-joao-da-silva.txt   (mande esse texto ao cliente)

     Opcional:
         --dias 30        licenca temporaria (aluguel/festival); sem isso e perpetua
         --nota "Pedido 123"

  3) Para conferir uma licenca:
         python3 licenca.py verificar --arquivo licenca-joao-da-silva.txt
"""

import argparse, base64, hashlib, json, os, re, secrets, sys, unicodedata
from datetime import datetime, timedelta, timezone

PRIV_FILE = "transcriberlive-private.key"
PUB_FILE  = "transcriberlive-public.txt"
E = 65537            # expoente publico
BITS = 2048          # tamanho do modulo
HEADER = "-----TRANSCRIBER LIVE LICENSE-----"
FOOTER = "-----FIM DA LICENCA-----"


# ---------------------------------------------------------------- RSA (stdlib)
def _is_probable_prime(n, rounds=40):
    if n < 2:
        return False
    for p in (2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37):
        if n % p == 0:
            return n == p
    d, r = n - 1, 0
    while d % 2 == 0:
        d //= 2
        r += 1
    for _ in range(rounds):
        a = secrets.randbelow(n - 3) + 2
        x = pow(a, d, n)
        if x in (1, n - 1):
            continue
        for _ in range(r - 1):
            x = x * x % n
            if x == n - 1:
                break
        else:
            return False
    return True


def _gen_prime(bits):
    while True:
        c = secrets.randbits(bits) | (1 << (bits - 1)) | 1
        if _is_probable_prime(c):
            return c


def gerar_par(bits=BITS):
    while True:
        p = _gen_prime(bits // 2)
        q = _gen_prime(bits - bits // 2)
        if p == q:
            continue
        phi = (p - 1) * (q - 1)
        if phi % E == 0:
            continue
        n = p * q
        if n.bit_length() != bits:
            continue
        d = pow(E, -1, phi)
        return {"n": n, "e": E, "d": d}


# --------------------------------------------------------------- formato
def _payload_bytes(payload: dict) -> bytes:
    # JSON canonico (chaves ordenadas, sem espacos) — o plugin refaz o hash sobre estes bytes
    return json.dumps(payload, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def assinar(payload: dict, priv: dict) -> str:
    raw = _payload_bytes(payload)
    digest = int.from_bytes(hashlib.sha256(raw).digest(), "big")
    sig = pow(digest, priv["d"], priv["n"])
    blob = base64.b64encode(raw).decode("ascii") + "." + format(sig, "x").upper()
    linhas = [blob[i:i + 64] for i in range(0, len(blob), 64)]
    return "\n".join([HEADER] + linhas + [FOOTER])


def conferir(texto: str, pub: dict):
    limpo = "".join(l.strip() for l in texto.splitlines() if "-----" not in l)
    if "." not in limpo:
        return None, "formato invalido"
    b64, sighex = limpo.rsplit(".", 1)
    try:
        raw = base64.b64decode(b64)
        payload = json.loads(raw.decode("utf-8"))
    except Exception as exc:
        return None, "conteudo ilegivel (%s)" % exc
    digest = int.from_bytes(hashlib.sha256(_payload_bytes(payload)).digest(), "big")
    if pow(int(sighex, 16), pub["e"], pub["n"]) != digest:
        return None, "assinatura invalida"
    return payload, None


# --------------------------------------------------------------- arquivos
def salvar_priv(priv, caminho=PRIV_FILE):
    with open(caminho, "w") as f:
        json.dump({"n": format(priv["n"], "x"), "e": priv["e"], "d": format(priv["d"], "x")}, f, indent=1)
    os.chmod(caminho, 0o600)


def carregar_priv(caminho=PRIV_FILE):
    if not os.path.exists(caminho):
        sys.exit("Nao achei %s — rode 'python3 licenca.py init' primeiro." % caminho)
    with open(caminho) as f:
        k = json.load(f)
    return {"n": int(k["n"], 16), "e": int(k["e"]), "d": int(k["d"], 16)}


def linha_publica(priv):
    """Formato que o plugin espera (juce::RSAKey): "expoente,modulo" em hexadecimal."""
    return "%s,%s" % (format(priv["e"], "x").upper(), format(priv["n"], "x").upper())


def slug(s):
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode("ascii").lower()
    return re.sub(r"[^a-z0-9]+", "-", s).strip("-") or "cliente"


# --------------------------------------------------------------- comandos
def cmd_init(args):
    if os.path.exists(PRIV_FILE) and not args.forcar:
        sys.exit("%s ja existe. Use --forcar para sobrescrever (isso INVALIDA todas as licencas ja vendidas)." % PRIV_FILE)
    print("Gerando par de chaves de %d bits (pode levar alguns segundos)..." % BITS)
    priv = gerar_par()
    salvar_priv(priv)
    pub = linha_publica(priv)
    with open(PUB_FILE, "w") as f:
        f.write(pub + "\n")
    print("\nPrivada : %s   <-- SEGREDO. Backup em lugar seguro. Sem ela voce nao emite licencas." % PRIV_FILE)
    print("Publica : %s   <-- vai embutida no plugin\n" % PUB_FILE)
    print(pub)


def cmd_nova(args):
    priv = carregar_priv(args.chave)
    mid = args.maquina.strip().upper()
    if not re.fullmatch(r"TL(-[0-9A-F]{4}){3}", mid):
        sys.exit("ID de maquina invalido: esperado TL-XXXX-XXXX-XXXX (o plugin mostra e copia esse ID).")

    agora = datetime.now(timezone.utc)
    payload = {
        "v": 1,
        "prod": "transcriber-live",
        "nome": args.nome.strip(),
        "email": args.email.strip(),
        "mid": mid,
        "serial": secrets.token_hex(4).upper(),
        "iat": agora.strftime("%Y-%m-%d"),
    }
    if args.dias:
        payload["exp"] = (agora + timedelta(days=args.dias)).strftime("%Y-%m-%d")
    if args.nota:
        payload["nota"] = args.nota.strip()

    lic = assinar(payload, priv)
    destino = args.saida or "licenca-%s.txt" % slug(args.nome)
    with open(destino, "w") as f:
        f.write(lic + "\n")

    print("Licenca gravada em %s\n" % destino)
    print("titular : %s <%s>" % (payload["nome"], payload["email"]))
    print("maquina : %s" % mid)
    print("serial  : %s" % payload["serial"])
    print("validade: %s" % ("perpetua" if "exp" not in payload else "ate " + payload["exp"]))
    print("\n" + lic)


def cmd_verificar(args):
    priv = carregar_priv(args.chave)
    pub = {"e": priv["e"], "n": priv["n"]}
    texto = open(args.arquivo).read() if args.arquivo else sys.stdin.read()
    payload, erro = conferir(texto, pub)
    if erro:
        sys.exit("INVALIDA: " + erro)
    print("VALIDA (assinatura ok)")
    for k in ("nome", "email", "mid", "serial", "iat", "exp", "nota"):
        if k in payload:
            print("  %-7s %s" % (k, payload[k]))


def main():
    ap = argparse.ArgumentParser(description="Gerador de licencas do Transcriber Live")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("init", help="cria o par de chaves (uma vez)")
    p.add_argument("--forcar", action="store_true")
    p.set_defaults(func=cmd_init)

    p = sub.add_parser("nova", help="emite uma licenca para um cliente")
    p.add_argument("--nome", required=True)
    p.add_argument("--email", required=True)
    p.add_argument("--maquina", required=True, help="ID que aparece no plugin: TL-XXXX-XXXX-XXXX")
    p.add_argument("--dias", type=int, help="licenca temporaria de N dias (padrao: perpetua)")
    p.add_argument("--nota")
    p.add_argument("--saida")
    p.add_argument("--chave", default=PRIV_FILE)
    p.set_defaults(func=cmd_nova)

    p = sub.add_parser("verificar", help="confere uma licenca emitida")
    p.add_argument("--arquivo")
    p.add_argument("--chave", default=PRIV_FILE)
    p.set_defaults(func=cmd_verificar)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
