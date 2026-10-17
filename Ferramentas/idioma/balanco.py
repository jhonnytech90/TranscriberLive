# -*- coding: utf-8 -*-
"""Confere parênteses/chaves ignorando comentário e literal.

Contar com grep dá falso alarme: parêntese dentro de comentário ou de texto
conta igual. Aqui o scanner pula os dois, então o que sobrar é código mesmo.
"""
import io, sys

def balanco(caminho):
    s = io.open(caminho, encoding='utf-8').read()
    i, n = 0, len(s)
    par = chv = col = 0
    linha = 1
    pilha = []
    while i < n:
        c = s[i]
        if c == '\n': linha += 1; i += 1; continue
        if c == '/' and i+1 < n and s[i+1] == '/':
            j = s.find('\n', i); i = n if j < 0 else j; continue
        if c == '/' and i+1 < n and s[i+1] == '*':
            j = s.find('*/', i); 
            linha += s.count('\n', i, n if j < 0 else j)
            i = n if j < 0 else j+2; continue
        if c == '"':
            j = i+1
            while j < n and s[j] != '"':
                j += 2 if s[j] == '\\' else 1
            i = j+1; continue
        if c == "'":
            j = i+1
            while j < n and s[j] != "'":
                j += 2 if s[j] == '\\' else 1
            i = j+1; continue
        if c in '([{': pilha.append((c, linha))
        elif c in ')]}':
            if not pilha: print('  %s:%d fecha %s sem abrir' % (caminho, linha, c)); return False
            a, l = pilha.pop()
            if '([{'.index(a) != ')]}'.index(c):
                print('  %s:%d fecha %s mas aberto %s na linha %d' % (caminho, linha, c, a, l)); return False
        i += 1
    if pilha:
        for a, l in pilha[:5]:
            print('  %s: %s aberto na linha %d e nao fechado' % (caminho, a, l))
        return False
    return True

ok = True
for f in sys.argv[1:]:
    bom = balanco(f)
    print(('OK   ' if bom else 'ERRO ') + f)
    ok = ok and bom
sys.exit(0 if ok else 1)
