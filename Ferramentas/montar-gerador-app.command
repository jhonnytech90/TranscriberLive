#!/bin/bash
#
# Transforma o Gerador de Licencas num .app do macOS.
#
#   Duplo clique aqui, ou:  ./montar-gerador-app.command
#
# Sai em:  saida-app/Gerador de Licencas.app
#
# ---------------------------------------------------------------------------
# O que este script NAO faz, de proposito
# ---------------------------------------------------------------------------
# Nao embute a chave privada no aplicativo. Um .app e um pacote que se copia,
# se compartilha e se manda por AirDrop sem pensar; chave de assinatura dentro
# dele seria um acidente esperando acontecer. O app LE a chave da pasta de
# dados em tempo de execucao:
#
#     ~/TranscriberLive-Licencas/
#
# Essa pasta fica na RAIZ da pasta pessoal e nao em Documentos, porque com o
# iCloud Drive sincronizando Documentos (o padrao na maioria das maquinas) a
# chave privada iria para os servidores da Apple. Como o licenciamento e
# offline e nao tem revogacao, um vazamento dessa chave obriga a republicar o
# plugin com chave nova -- invalidando TODA licenca ja vendida.
#
set -e
cd "$(dirname "$0")"

verm()  { printf '\033[31m%s\033[0m\n' "$1"; }
verde() { printf '\033[32m%s\033[0m\n' "$1"; }
cinza() { printf '\033[90m%s\033[0m\n' "$1"; }
morre() { verm "$1"; echo ""; read -r -p "Enter para fechar..." _; exit 1; }

echo ""
echo "Gerador de Licencas -> .app"
echo "==========================="

# ---------------------------------------------------------------- 1. requisitos
PY=""
for cand in /Library/Frameworks/Python.framework/Versions/Current/bin/python3 \
            /usr/local/bin/python3 /opt/homebrew/bin/python3 python3; do
    if command -v "$cand" >/dev/null 2>&1 && "$cand" -c 'import tkinter' >/dev/null 2>&1; then
        PY="$cand"; break
    fi
done
[ -n "$PY" ] || morre "Nao achei um Python 3 COM tkinter. Instale o Python do python.org (ele ja vem com tkinter)."

cinza "python   : $($PY -V 2>&1)  ($PY)"

if ! "$PY" -m PyInstaller --version >/dev/null 2>&1; then
    echo "PyInstaller nao esta instalado. Instalando..."
    "$PY" -m pip install --quiet pyinstaller || morre "Falhou instalar o PyInstaller."
fi
cinza "pyinstaller: $($PY -m PyInstaller --version 2>&1)"

for f in licenca_gui.py licenca.py; do
    [ -f "$f" ] || morre "Nao achei $f nesta pasta."
done

# ---------------------------------------------------------------- 2. icone
ICONE=""
for c in ../Resources/brand/TranscriberLive.icns ../Resources/icon_1024.png; do
    [ -f "$c" ] && { ICONE="$c"; break; }
done
[ -n "$ICONE" ] && cinza "icone    : $ICONE" || cinza "icone    : (sem icone, usa o padrao)"

# ---------------------------------------------------------------- 3. monta
rm -rf build-app saida-app "Gerador de Licencas.spec"
mkdir -p build-app/pyi-cache

# Cache do PyInstaller numa pasta NOSSA, e nao na do usuario.
#
# O padrao e ~/Library/Application Support/pyinstaller. Se alguma vez o
# PyInstaller rodou com sudo naquela maquina, subpastas dali ficam com dono
# root -- e a montagem falha com "Permission denied" num caminho que nada tem
# a ver com este projeto. Apontando o cache para ca, o problema deixa de
# existir e nao precisamos mexer em permissao de sistema nenhuma.
export PYINSTALLER_CONFIG_DIR="$PWD/build-app/pyi-cache"

ARGS=(--noconfirm --windowed
      --name "Gerador de Licencas"
      --distpath saida-app
      --workpath build-app
      --osx-bundle-identifier com.jhonatanmiikael.transcriberlive.gerador)

# licenca.py entra como MODULO (o gui faz "import licenca"), nao como dado
ARGS+=(--hidden-import licenca --paths .)

[ -n "$ICONE" ] && ARGS+=(--icon "$ICONE")

echo ""
echo "Montando (leva 1 a 2 minutos)..."
"$PY" -m PyInstaller "${ARGS[@]}" licenca_gui.py >/tmp/gerador-app.log 2>&1 \
    || { verm "PyInstaller falhou. Ultimas linhas:"; tail -25 /tmp/gerador-app.log; morre ""; }

APP="saida-app/Gerador de Licencas.app"
[ -d "$APP" ] || { tail -25 /tmp/gerador-app.log; morre "O .app nao foi gerado."; }

# ---------------------------------------------------------------- 4. confere
# O binario TEM que estar executavel: ja perdemos um dia por causa disso no
# instalador do plugin (ZIP nao guarda permissao de Unix).
BIN="$APP/Contents/MacOS/Gerador de Licencas"
[ -x "$BIN" ] || chmod +x "$BIN"
[ -x "$BIN" ] || morre "O binario do app nao esta executavel."

# assinatura ad-hoc: sem isso o macOS pode recusar abrir
codesign --force --deep --sign - "$APP" >/dev/null 2>&1 || true

echo ""
verde "Pronto: $(cd "$(dirname "$APP")" && pwd)/$(basename "$APP")"
cinza "tamanho: $(du -sh "$APP" | cut -f1)"
echo ""
echo "Os DADOS (chave, licencas, historico) ficam em:"
echo "    ~/TranscriberLive-Licencas/"
echo ""
echo "Se ainda nao moveu, leve para la a chave e o historico que voce ja tem:"
echo "    transcriberlive-private.key"
echo "    licencas-emitidas.csv"
echo "    licencas/"
echo ""
read -r -p "Enter para fechar..." _
