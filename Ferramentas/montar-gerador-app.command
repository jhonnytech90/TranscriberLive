#!/bin/bash
#
# Transforma o Gerador de Licencas num .app do macOS.
#
#   Duplo clique aqui, ou:  ./montar-gerador-app.command
#
# Sai em:  saida-app/Gerador de Licencas.app
#
# ---------------------------------------------------------------------------
# Chave privada DENTRO do app (padrao)
# ---------------------------------------------------------------------------
# A chave e copiada para Contents/Resources, entao o app abre e ja assina, sem
# depender de pasta nenhuma. Para montar SEM a chave dentro:
#
#     ./montar-gerador-app.command --sem-chave
#
# Neste caso o app le a chave de ~/TranscriberLive-Licencas/ em tempo de
# execucao. Essa pasta fica na raiz da pasta pessoal e nao em Documentos, que
# na maioria das maquinas esta sincronizado com o iCloud Drive.
#
# Regra unica, valendo para o app com chave dentro: ele nao sai desta maquina.
# Nao mandar por AirDrop, nao zipar para e-mail, nao deixar em pasta que
# sincroniza com nuvem. O licenciamento e offline e nao tem revogacao.
#
set -e
cd "$(dirname "$0")"

verm()  { printf '\033[31m%s\033[0m\n' "$1"; }
verde() { printf '\033[32m%s\033[0m\n' "$1"; }
cinza() { printf '\033[90m%s\033[0m\n' "$1"; }
morre() { verm "$1"; echo ""; read -r -p "Enter para fechar..." _; exit 1; }

COM_CHAVE=1
[ "${1:-}" = "--sem-chave" ] && COM_CHAVE=0

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

# ---------------------------------------------------------------- 5. a chave
# Copiada DEPOIS do PyInstaller, direto para Contents/Resources: assim ela nao
# passa pelo cache nem pelo .spec dele, e nao fica esquecida em build-app/.
if [ "$COM_CHAVE" = "1" ]; then
    CHAVE=""
    for c in "$HOME/TranscriberLive-Licencas/transcriberlive-private.key" \
             "./transcriberlive-private.key" \
             "$HOME/transcriberlive-private.key"; do
        [ -f "$c" ] && { CHAVE="$c"; break; }
    done

    if [ -n "$CHAVE" ]; then
        cp "$CHAVE" "$APP/Contents/Resources/transcriberlive-private.key"
        chmod 600 "$APP/Contents/Resources/transcriberlive-private.key"
        cinza "chave    : embutida (de $CHAVE)"
        COM_CHAVE_OK=1
    else
        verm "AVISO: nao achei transcriberlive-private.key -- app montado SEM chave dentro."
        verm "       Ele vai procurar em ~/TranscriberLive-Licencas/ ao abrir."
        COM_CHAVE_OK=0
    fi
else
    cinza "chave    : fora do app (--sem-chave)"
    COM_CHAVE_OK=0
fi

# Assinatura ad-hoc POR ULTIMO: qualquer arquivo colocado no pacote depois de
# assinar invalida a assinatura, e o macOS recusa abrir com erro generico.
# Foi exatamente esse o problema no instalador do plugin, com o lipo.
codesign --force --deep --sign - "$APP" >/dev/null 2>&1 || true
codesign --verify "$APP" >/dev/null 2>&1 || verm "AVISO: a assinatura nao validou."

echo ""
verde "Pronto: $(cd "$(dirname "$APP")" && pwd)/$(basename "$APP")"
cinza "tamanho: $(du -sh "$APP" | cut -f1)"
echo ""
if [ "${COM_CHAVE_OK:-0}" = "1" ]; then
    verm "A CHAVE PRIVADA ESTA DENTRO DESTE APP."
    verm "Ele nao sai desta maquina: sem AirDrop, sem zip por e-mail, sem pasta de nuvem."
    echo ""
fi
echo "As licencas emitidas e o historico ficam em:"
echo "    ~/TranscriberLive-Licencas/"
echo ""
read -r -p "Enter para fechar..." _
