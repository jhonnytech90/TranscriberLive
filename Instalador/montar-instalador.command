#!/bin/bash
#
# Transcriber Live — monta os pacotes do macOS
#
#   saida/TranscriberLive-<versao>-macOS.pkg                instalador (offline)
#   saida/TranscriberLive-<versao>-macOS-Desinstalador.pkg  desinstalador
#
# Duplo clique aqui, ou:
#   ./montar-instalador.command [pasta-com-os-binarios] [pasta-com-os-modelos]
#
# Sem argumentos ele procura sozinho, nesta ordem:
#   binarios:  ./binarios  ->  /Applications/arquivos  ->  ../build
#   modelos:   ./modelos   ->  /Applications/arquivos/models
#              ->  /Library/Application Support/TranscriberLive/models
#
# O instalador e PAYLOAD PURO: o macOS coloca cada arquivo no lugar sozinho.
# Nada de zip, nada de mv, nada de download. Os modelos vao para
# /Library/Application Support/TranscriberLive/models — pasta de sistema, que o
# payload pode escrever (o erro "instalar conteudo no volume do sistema" so
# acontece mirando a pasta do usuario) e que o plugin ja procura.
#
# Nao interativo (CI):  TL_NAO_INTERATIVO=1 ./montar-instalador.command ...
#
set -u
cd "$(dirname "$0")" || exit 1

VERSAO="0.4"
ID="com.jhonatanmiikael.transcriberlive"
SAIDA="saida"
LOTE="${TL_NAO_INTERATIVO:-0}"

PKG_INST="$SAIDA/TranscriberLive-$VERSAO-macOS.pkg"
PKG_DESI="$SAIDA/TranscriberLive-$VERSAO-macOS-Desinstalador.pkg"

verde() { printf "\033[0;32m%s\033[0m\n" "$1"; }
verm()  { printf "\033[0;31m%s\033[0m\n" "$1"; }
cinza() { printf "\033[0;90m%s\033[0m\n" "$1"; }
pausa() { [ "$LOTE" = "1" ] || read -r -p "${1:-Enter para fechar...}" _; }
morre() { verm "$1"; pausa; exit 1; }

echo ""
echo "================================================================"
echo " Transcriber Live $VERSAO — pacotes do macOS"
echo "================================================================"

# ---------------------------------------------------------------- 1. origens
BIN="${1:-}"
if [ -z "$BIN" ]; then
    for t in "./binarios" "/Applications/arquivos" "../build"; do
        if [ -d "$t" ] && [ -n "$(/usr/bin/find "$t" -maxdepth 5 -name '*.vst3' -print -quit 2>/dev/null)" ]; then
            BIN="$t"; break
        fi
    done
fi
[ -n "$BIN" ] && [ -d "$BIN" ] || morre "Nao achei os binarios (.vst3/.component/.app). Passe a pasta como argumento."

MOD="${2:-}"
if [ -z "$MOD" ]; then
    for t in "./modelos" "/Applications/arquivos/models" "/Library/Application Support/TranscriberLive/models"; do
        if [ -d "$t" ] && [ -n "$(/bin/ls "$t"/ggml-*.bin 2>/dev/null)" ]; then MOD="$t"; break; fi
    done
fi

cinza "binarios : $BIN"
cinza "modelos  : ${MOD:-(nenhum — o instalador sai sem modelos)}"

TMP="$(/usr/bin/mktemp -d)"
trap '/bin/rm -rf "$TMP"' EXIT
R="$TMP/root"
/bin/mkdir -p "$R/Library/Audio/Plug-Ins/VST3" \
              "$R/Library/Audio/Plug-Ins/Components" \
              "$R/Applications" \
              "$R/Library/Application Support/TranscriberLive/models" \
              "$SAIDA"

# ---------------------------------------------------------------- 2. payload
achados=0
pegar() {   # pegar <padrao> <pasta destino no payload>
    padrao="$1"; destino="$2"
    while IFS= read -r B; do
        [ -z "$B" ] && continue
        # ignora o que estiver DENTRO de outro bundle
        case "$B" in *".vst3/"*|*".component/"*|*".app/"*) continue;; esac
        nome="$(basename "$B")"
        [ -e "$destino/$nome" ] && continue
        /usr/bin/ditto "$B" "$destino/$nome" || continue
        echo "  + $nome"; achados=$((achados+1))
    done < <(/usr/bin/find "$BIN" -maxdepth 5 -name "$padrao" -type d 2>/dev/null | /usr/bin/sort)
}

echo ""
echo "Plugins e aplicativo:"
pegar "*.vst3"      "$R/Library/Audio/Plug-Ins/VST3"
pegar "*.component" "$R/Library/Audio/Plug-Ins/Components"
pegar "*.app"       "$R/Applications"
[ "$achados" -eq 0 ] && morre "Nenhum plugin encontrado em $BIN."

echo ""
echo "Modelos (embutidos, o cliente nao baixa nada):"
if [ -n "$MOD" ]; then
    for m in "$MOD"/ggml-*.bin; do
        [ -e "$m" ] || continue
        /usr/bin/ditto "$m" "$R/Library/Application Support/TranscriberLive/models/$(basename "$m")" \
            && echo "  + $(basename "$m")  ($(/usr/bin/du -h "$m" | /usr/bin/cut -f1))"
    done
else
    echo "  (nenhum)"
fi

# lixo do Finder e copias aninhadas de instalacoes feitas com 'mv'
/usr/bin/find "$R" -name '.DS_Store' -delete 2>/dev/null
/usr/bin/find "$R" -mindepth 2 \( -name '*.vst3' -o -name '*.component' -o -name '*.app' \) -print0 2>/dev/null |
while IFS= read -r -d '' X; do
    case "$X" in
        *.vst3/*.vst3|*.component/*.component|*.app/*.app)
            echo "  - aninhado removido: $(basename "$X")"; /bin/rm -rf "$X" ;;
    esac
done

# ---------------------------------------------------------------- 3. conferencia
echo ""
echo "Conferindo o payload:"
ok=1
for B in "$R/Library/Audio/Plug-Ins/VST3/"*.vst3 \
         "$R/Library/Audio/Plug-Ins/Components/"*.component \
         "$R/Applications/"*.app; do
    [ -e "$B" ] || continue
    EXE="$(/bin/ls "$B/Contents/MacOS/" 2>/dev/null | /usr/bin/head -1)"
    if [ -z "$EXE" ]; then verm "  ERRO: $(basename "$B") sem binario em Contents/MacOS"; ok=0
    else echo "  ok  $(basename "$B")"; fi
done
[ "$ok" = "1" ] || morre "Payload invalido — nao vou empacotar isso."

# ---------------------------------------------------------------- 4. recursos
/bin/mkdir -p recursos
[ -f capa.png ]        && /bin/cp capa.png recursos/
[ -f capa-dark.png ]   && /bin/cp capa-dark.png recursos/
[ -f Introducao.txt ]  && /bin/cp Introducao.txt recursos/

fundo() {
    [ -f recursos/capa.png ] && \
        echo '    <background file="capa.png" mime-type="image/png" alignment="bottomleft" scaling="proportional"/>'
    [ -f recursos/capa-dark.png ] && \
        echo '    <background-darkAqua file="capa-dark.png" mime-type="image/png" alignment="bottomleft" scaling="proportional"/>'
}

# ---------------------------------------------------------------- 5. instalador
echo ""
echo "Gerando o instalador..."
/bin/chmod +x scripts/postinstall 2>/dev/null
/bin/rm -f "$SAIDA"/*.pkg

# --- trava a RELOCACAO DE BUNDLE (senao o app some da pasta Aplicativos) -----
#
# O Installer do macOS tem um comportamento que nao esta em lugar nenhum da
# interface: se ja existe, em QUALQUER pasta do disco, um bundle com o mesmo
# CFBundleIdentifier registrado no Launch Services, ele instala EM CIMA
# daquela copia em vez do caminho declarado no pacote. Silenciosamente.
#
# Aconteceu de verdade. Numa maquina que tinha uma copia do app em
# /Applications/arquivos, o /var/log/install.log registrou:
#
#   PackageKit: Applications/Transcriber Live Display.app relocated to
#               Applications/arquivos/Transcriber Live Display.app
#
# Resultado: os plugins instalaram certo, o app "sumiu" -- estava instalado,
# mas dentro da pasta antiga. O cliente ve "parou de abrir".
#
# Basta uma copia perdida em Downloads para isso acontecer com qualquer um.
# A cura e declarar cada bundle como nao-relocavel.
PLIST="$TMP/componentes.plist"
/usr/bin/pkgbuild --analyze --root "$R" "$PLIST" >/dev/null \
    || morre "pkgbuild --analyze falhou."

/usr/bin/python3 - "$PLIST" <<'PYEOF' || morre "nao consegui marcar os bundles como nao-relocaveis."
import plistlib, sys
caminho = sys.argv[1]
with open(caminho, "rb") as f:
    comps = plistlib.load(f)
for c in comps:
    c["BundleIsRelocatable"] = False
with open(caminho, "wb") as f:
    plistlib.dump(comps, f)
print("  %d bundles marcados como nao-relocaveis" % len(comps))
PYEOF

/usr/bin/pkgbuild --identifier "$ID" --version "$VERSAO" \
    --root "$R" --scripts scripts --component-plist "$PLIST" \
    --ownership recommended --install-location / \
    "$TMP/base.pkg" >/dev/null || morre "pkgbuild (instalador) falhou."

{
echo '<?xml version="1.0" encoding="utf-8"?>'
echo '<installer-gui-script minSpecVersion="2">'
echo "    <title>Transcriber Live</title>"
echo "    <organization>$ID</organization>"
fundo
[ -f recursos/Introducao.txt ] && echo '    <welcome file="Introducao.txt" mime-type="text/plain"/>'
echo '    <options customize="never" require-scripts="true" hostArchitectures="arm64,x86_64"/>'
echo '    <volume-check><allowed-os-versions><os-version min="11.0"/></allowed-os-versions></volume-check>'
echo '    <choices-outline><line choice="tudo"/></choices-outline>'
echo "    <choice id=\"tudo\" title=\"Transcriber Live\"><pkg-ref id=\"$ID\"/></choice>"
echo "    <pkg-ref id=\"$ID\" version=\"$VERSAO\" auth=\"Root\">base.pkg</pkg-ref>"
echo '</installer-gui-script>'
} > "$TMP/dist-inst.xml"

/usr/bin/productbuild --distribution "$TMP/dist-inst.xml" \
    --package-path "$TMP" --resources recursos \
    "$PKG_INST" >/dev/null || morre "productbuild (instalador) falhou."
verde "  $PKG_INST  ($(/usr/bin/du -h "$PKG_INST" | /usr/bin/cut -f1))"

# ---------------------------------------------------------------- 6. desinstalador
echo ""
echo "Gerando o desinstalador..."
/bin/chmod +x desinstalador/scripts/postinstall 2>/dev/null

# PAYLOAD MINIMO — nao tire.
# Com --nopayload o PackageInfo sai sem o elemento <payload>, e dentro de uma
# distribuicao do productbuild o Installer trata o pacote como "nada a fazer":
# nao roda o script e conclui na hora. O proprio script apaga este marcador.
DR="$TMP/root-desi/Library/Application Support/TranscriberLive"
/bin/mkdir -p "$DR"
echo "desinstalacao solicitada em $(date)" > "$DR/.desinstalar"

/usr/bin/pkgbuild --identifier "$ID.desinstalador" --version "$VERSAO" \
    --root "$TMP/root-desi" --scripts desinstalador/scripts \
    --ownership recommended --install-location / \
    "$TMP/desi.pkg" >/dev/null || morre "pkgbuild (desinstalador) falhou."

/bin/mkdir -p recursos-desi
[ -f capa.png ]      && /bin/cp capa.png recursos-desi/
[ -f capa-dark.png ] && /bin/cp capa-dark.png recursos-desi/
[ -f desinstalador/Desinstalar.txt ] && /bin/cp desinstalador/Desinstalar.txt recursos-desi/

{
echo '<?xml version="1.0" encoding="utf-8"?>'
echo '<installer-gui-script minSpecVersion="2">'
echo "    <title>Desinstalar o Transcriber Live</title>"
echo "    <organization>$ID</organization>"
[ -f recursos-desi/capa.png ] && \
    echo '    <background file="capa.png" mime-type="image/png" alignment="bottomleft" scaling="proportional"/>'
[ -f recursos-desi/capa-dark.png ] && \
    echo '    <background-darkAqua file="capa-dark.png" mime-type="image/png" alignment="bottomleft" scaling="proportional"/>'
[ -f recursos-desi/Desinstalar.txt ] && echo '    <welcome file="Desinstalar.txt" mime-type="text/plain"/>'
echo '    <options customize="never" require-scripts="true" hostArchitectures="arm64,x86_64"/>'
echo '    <choices-outline><line choice="remover"/></choices-outline>'
echo "    <choice id=\"remover\" title=\"Remover o Transcriber Live\"><pkg-ref id=\"$ID.desinstalador\"/></choice>"
echo "    <pkg-ref id=\"$ID.desinstalador\" version=\"$VERSAO\" auth=\"Root\">desi.pkg</pkg-ref>"
echo '</installer-gui-script>'
} > "$TMP/dist-desi.xml"

/usr/bin/productbuild --distribution "$TMP/dist-desi.xml" \
    --package-path "$TMP" --resources recursos-desi \
    "$PKG_DESI" >/dev/null || morre "productbuild (desinstalador) falhou."
verde "  $PKG_DESI  ($(/usr/bin/du -h "$PKG_DESI" | /usr/bin/cut -f1))"

# ---------------------------------------------------------------- 7. fim
echo ""
echo "Teste antes de mandar para alguem:"
echo "  sudo installer -pkg \"$PKG_INST\" -target /"
echo "  tail -f /private/tmp/transcriberlive-install.log"
echo ""
cinza "Sem assinatura, o macOS do cliente pede 'Abrir mesmo assim' em"
cinza "Ajustes > Privacidade e Seguranca (conta de desenvolvedor Apple resolve)."
echo ""
[ "$LOTE" = "1" ] || /usr/bin/open "$SAIDA" 2>/dev/null
pausa
