# -*- coding: utf-8 -*-
r"""Troca acento CRU dentro de literal C++ por \xNN, sem tocar em comentário.

Por que importa: o arquivo é UTF-8 sem BOM. O clang lê como UTF-8; o MSVC,
dependendo da página de código, pode ler os mesmos bytes como outra coisa —
e aí a mesma linha de código vira um texto DIFERENTE no Windows e no macOS.
Como a chave de tradução é o próprio texto, bastaria isso para a tradução
casar numa plataforma e falhar na outra, calada.

Escapando, os dois compiladores produzem exatamente os mesmos bytes.

O \xNN seguido de dígito hexadecimal se funde ao escape anterior (erro C7744
do MSVC), então nesse caso o literal é cortado e reaberto com  " ".
"""
import io, sys

HEX = set('0123456789abcdefABCDEF')


def escapar_arquivo(caminho):
    orig = io.open(caminho, encoding='utf-8').read()
    fora = []
    i, n = 0, len(orig)
    dentro = False          # dentro de literal ""
    trocas = 0
    anterior_foi_hex = False

    while i < n:
        c = orig[i]

        if not dentro:
            # pula comentários inteiros: lá acento é bem-vindo
            if c == '/' and i + 1 < n and orig[i+1] == '/':
                j = orig.find('\n', i)
                j = n if j < 0 else j
                fora.append(orig[i:j]); i = j; continue
            if c == '/' and i + 1 < n and orig[i+1] == '*':
                j = orig.find('*/', i)
                j = n if j < 0 else j + 2
                fora.append(orig[i:j]); i = j; continue
            if c == "'":                      # literal de caractere
                j = i + 1
                while j < n and orig[j] != "'":
                    j += 2 if orig[j] == '\\' else 1
                fora.append(orig[i:j+1]); i = j + 1; continue
            if c == '"':
                dentro = True; anterior_foi_hex = False
                fora.append(c); i += 1; continue
            fora.append(c); i += 1; continue

        # dentro do literal
        if c == '\\':
            seq = orig[i:i+2]
            anterior_foi_hex = (len(seq) == 2 and seq[1] == 'x')
            if anterior_foi_hex:
                j = i + 2
                while j < n and orig[j] in HEX:
                    j += 1
                fora.append(orig[i:j]); i = j; continue
            fora.append(seq); i += 2; continue
        if c == '"':
            dentro = False
            fora.append(c); i += 1; continue
        if ord(c) < 128:
            if anterior_foi_hex and c in HEX:
                fora.append('" "')
            fora.append(c); anterior_foi_hex = False; i += 1; continue

        # o caso que interessa: acento cru
        for b in c.encode('utf-8'):
            fora.append('\\x%02x' % b)
        anterior_foi_hex = True
        trocas += 1
        i += 1

    novo = ''.join(fora)
    if novo != orig:
        io.open(caminho, 'w', encoding='utf-8').write(novo)
    return trocas


if __name__ == '__main__':
    for arq in sys.argv[1:]:
        print(arq, '->', escapar_arquivo(arq), 'caracteres escapados')
