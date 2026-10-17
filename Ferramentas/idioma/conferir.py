# -*- coding: utf-8 -*-
r"""Confere o catálogo contra o código.

Dois jeitos de a tradução falhar sem ninguém perceber:

  1. o código chama tr("algo") que não está no catálogo  -> fica em português
     no meio da interface em inglês;
  2. o catálogo tem entrada que não existe mais no código -> texto traduzido
     que nunca aparece, e que confunde quem for revisar.

Nenhum dos dois dá erro de compilação. Por isso isto roda na CI.
"""
import io, os, re, sys, glob

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.abspath(os.path.join(AQUI, '..', '..'))
sys.path.insert(0, AQUI)
from catalogo import EN

# tr ("...") ou tr (("...")), com literais colados e escapes \xNN
CHAMADA = re.compile(r'\btr\s*\(\s*\(?\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\)?')
LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')


def desescapar(s):
    bruto = bytearray()
    i = 0
    while i < len(s):
        if s[i] == '\\' and i + 1 < len(s):
            c = s[i+1]
            if c == 'x':
                bruto.append(int(s[i+2:i+4], 16)); i += 4; continue
            mapa = {'n': 10, 't': 9, '"': 34, '\\': 92, "'": 39, '0': 0}
            if c in mapa:
                bruto.append(mapa[c]); i += 2; continue
            bruto.append(ord(c)); i += 2; continue
        bruto.extend(s[i].encode('utf-8')); i += 1
    return bruto.decode('utf-8', 'replace')


usadas = {}
for arq in glob.glob(os.path.join(RAIZ, 'Source', '**', '*.*'), recursive=True):
    if not arq.endswith(('.cpp', '.h')):
        continue
    if arq.endswith('TraducaoEn.h'):
        continue
    texto = io.open(arq, encoding='utf-8').read()
    texto = re.sub(r'/\*.*?\*/', '', texto, flags=re.S)
    texto = re.sub(r'//[^\n]*', '', texto)
    for m in CHAMADA.finditer(texto):
        inteiro = ''.join(LITERAL.findall(m.group(1)))
        usadas.setdefault(desescapar(inteiro), []).append(os.path.relpath(arq, RAIZ))

# Modelos.h guarda rotulo e detalhe numa TABELA de dados: o tr() recebe
# p->rotulo, nao um literal, entao o scanner acima nao os ve. Aqui pegamos
# os literais da tabela -- id de modelo e uma palavra minuscula sem espaco,
# entao qualquer literal com espaco ou acento e texto de tela.
mod = io.open(os.path.join(RAIZ, 'Source', 'Common', 'Modelos.h'), encoding='utf-8').read()
mod = re.sub(r'/\*.*?\*/', '', mod, flags=re.S)
mod = re.sub(r'//[^\n]*', '', mod)
mod = re.sub(r'#\s*include[^\n]*', '', mod)   # nome de header nao e texto de tela
# literais colados viram um texto so, como o compilador faz
GRUDADOS = re.compile(r'((?:"(?:[^"\\]|\\.)*"\s*)+)')
for m in GRUDADOS.finditer(mod):
    inteiro = desescapar(''.join(LITERAL.findall(m.group(1))))
    # Decodificado ANTES de filtrar: no literal cru, \xe2\x80\x94 carrega as
    # letras x, e, a dos proprios escapes, e todo separador passaria por texto.
    ehId = re.fullmatch(r'[a-z0-9_.\\-]+', inteiro) is not None
    temLetra = re.search(r'[A-Za-z]', inteiro) is not None
    if temLetra and not ehId:
        usadas.setdefault(inteiro, []).append('Source/Common/Modelos.h (tabela)')

sem_traducao = sorted(k for k in usadas if k not in EN)
sem_uso      = sorted(k for k in EN if k not in usadas)

print('tr() no codigo: %d  |  catalogo: %d' % (len(usadas), len(EN)))
if sem_traducao:
    print('\nFALTA NO CATALOGO (%d) -- vao ficar em portugues:' % len(sem_traducao))
    for k in sem_traducao:
        print('   %-60r  %s' % (k[:58], usadas[k][0]))
if sem_uso:
    print('\nNO CATALOGO MAS SEM USO (%d):' % len(sem_uso))
    for k in sem_uso:
        print('   %r' % (k[:70],))

sys.exit(1 if sem_traducao else 0)
