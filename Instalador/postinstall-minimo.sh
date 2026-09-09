#!/bin/bash
#
# Transcriber Live — postinstall MINIMO (versao enxuta, no estilo do
# instalador do SpotifyDownload: o payload deixa um zip em /Applications,
# o script move para um temporario, extrai e distribui.)
#
# Este arquivo NAO e o usado pelo instalador. O oficial e ./postinstall,
# que faz o mesmo com log, verificacao de espaco, quarentena, assinatura
# ad-hoc e dono correto dos arquivos.
#
# Guarde este aqui para depuracao: se algo der errado, rode ele na mao e
# veja onde para. E curto o bastante para ler de uma vez.
#

ZIP_ORIGEM="/Applications/TranscriberLive-payload.zip"
TMP="/private/tmp/TranscriberLive-Install"

DEST_VST3="/Library/Audio/Plug-Ins/VST3"
DEST_AU="/Library/Audio/Plug-Ins/Components"
DEST_APP="/Applications"

# quem esta usando o Mac (o script roda como root)
USUARIO=$(/usr/bin/stat -f%Su /dev/console 2>/dev/null)
CASA=$(/usr/bin/dscl . -read "/Users/$USUARIO" NFSHomeDirectory 2>/dev/null | /usr/bin/awk '{print $2}')
DEST_MODELOS="$CASA/Library/Application Support/TranscriberLive/models"

if [ ! -f "$ZIP_ORIGEM" ]; then
    echo "ZIP nao encontrado: $ZIP_ORIGEM"
    exit 1
fi

# move o zip para o temporario primeiro (assim ele nao fica em /Applications)
/bin/rm -rf "$TMP"
/bin/mkdir -p "$TMP" || exit 1
/bin/mv -f "$ZIP_ORIGEM" "$TMP/payload.zip" || exit 1

# extrai
/usr/bin/ditto -x -k "$TMP/payload.zip" "$TMP" || exit 1
/bin/rm -rf "$TMP/__MACOSX" "$TMP/payload.zip" 2>/dev/null

# distribui
mover() {
    origem="$1"; destino="$2"
    [ -d "$origem" ] || return 0
    /bin/mkdir -p "$destino"
    for item in "$origem"/*; do
        [ -e "$item" ] || continue
        nome="$(basename "$item")"
        /bin/rm -rf "$destino/$nome"
        /usr/bin/ditto "$item" "$destino/$nome" || echo "falhou: $nome"
        /usr/bin/xattr -dr com.apple.quarantine "$destino/$nome" 2>/dev/null
        echo "  $destino/$nome"
    done
}

mover "$TMP/VST3"         "$DEST_VST3"
mover "$TMP/Components"   "$DEST_AU"
mover "$TMP/Applications" "$DEST_APP"

# modelos: nunca sobrescreve o que o usuario ja baixou
if [ -d "$TMP/models" ]; then
    /bin/mkdir -p "$DEST_MODELOS"
    for m in "$TMP"/models/*.bin; do
        [ -e "$m" ] || continue
        nome="$(basename "$m")"
        [ -f "$DEST_MODELOS/$nome" ] && { echo "  ja existe: $nome"; continue; }
        /usr/bin/ditto "$m" "$DEST_MODELOS/$nome" && echo "  $DEST_MODELOS/$nome"
    done
    [ -n "$USUARIO" ] && /usr/sbin/chown -R "$USUARIO" "$CASA/Library/Application Support/TranscriberLive"
fi

/bin/rm -rf "$TMP"
/usr/bin/killall -9 AudioComponentRegistrar 2>/dev/null

echo "Instalacao concluida."
exit 0
