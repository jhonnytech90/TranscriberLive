# -*- coding: utf-8 -*-
r"""Gera Source/Common/TraducaoEn.h a partir do catálogo.

Por que gerar em vez de escrever o .h na mão: o formato do
juce::LocalisedStrings é  "chave" = "tradução"  por linha, e errar uma aspa
ali não dá erro de compilação — dá texto faltando na tela do cliente.

São DOIS escapes em cima do mesmo texto, e confundi-los foi o primeiro erro
desta ferramenta:

  1. o do formato do JUCE  -- aspa e quebra de linha dentro do texto;
  2. o do literal C++      -- a linha inteira, aspas do formato incluídas.

E o texto sai como bytes UTF-8 em \xNN de propósito: o MSVC recusa acento
direto dependendo da página de código. Com um porém que já nos custou um
build (erro C7744): \xNN seguido de um dígito hexadecimal vira um escape só.
Quando isso ia acontecer, o literal é cortado e reaberto.
"""
import io, os, sys

AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, AQUI)
from catalogo import EN

RAIZ = os.path.abspath(os.path.join(AQUI, '..', '..'))
SAIDA = os.path.join(RAIZ, 'Source', 'Common', 'TraducaoEn.h')
HEX = set('0123456789abcdefABCDEF')


def para_o_juce(s):
    """Escape do formato de tradução do JUCE."""
    return s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n')


def para_o_cpp(s):
    """Escape do literal C++, com corte quando \\xNN encostaria num hexa."""
    fora = []
    anterior_foi_hex = False
    for ch in s:
        if ch == '\\':
            fora.append('\\\\'); anterior_foi_hex = False
        elif ch == '"':
            fora.append('\\"'); anterior_foi_hex = False
        elif 32 <= ord(ch) < 127:
            if anterior_foi_hex and ch in HEX:
                fora.append('" "')          # corta o literal e reabre
            fora.append(ch); anterior_foi_hex = False
        else:
            for b in ch.encode('utf-8'):
                fora.append('\\x%02x' % b)
            anterior_foi_hex = True
    return ''.join(fora)


linhas = ['language: English', 'countries: us gb ca au nz ie', '']
for pt, en in EN.items():
    linhas.append('"%s" = "%s"' % (para_o_juce(pt), para_o_juce(en)))

corpo = '\n'.join('            "%s\\n"' % para_o_cpp(l) for l in linhas)

h = '''#pragma once

#include "Idioma.h"

/*  GERADO POR Ferramentas/idioma/gerar.py -- NAO EDITE NA MAO.
    Edite Ferramentas/idioma/catalogo.py e gere de novo, senao a proxima
    geracao apaga a correcao feita aqui.

    %d textos.
*/
namespace tl
{
    inline juce::String tabelaIngles()
    {
        return utf8 (
%s
        );
    }
}
''' % (len(EN), corpo)

io.open(SAIDA, 'w', encoding='ascii').write(h)
print('TraducaoEn.h:', len(EN), 'textos,', len(h), 'bytes')
