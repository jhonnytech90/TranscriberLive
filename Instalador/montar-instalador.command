#!/bin/bash
#
# Transcriber Live — monta o payload e gera o instalador .pkg (macOS)
#
# Duplo clique aqui, ou:  ./montar-instalador.command [pasta-com-os-binarios]
#
# O que ele faz:
#   1. procura os .vst3, .component e o .app
#   2. junta com os modelos num TranscriberLive-payload.zip
#   3. gera um .pkg SEM payload (so scripts) — por isso nao esbarra na protecao
#      do volume do sistema; quem copia os arquivos e o postinstall
#
set -u
cd "$(dirname "$0")" || exit 1

VERSAO="0.4"
ID="com.jhonatanmiikael.transcriberlive"
SAIDA="saida"
PKG="$SAIDA/TranscriberLive-$VERSAO.pkg"

verde() { printf "\033[0;32m%s\033[0m\n" "$1"; }
verm()  { printf "\033[0;31m%s\033[0m\n" "$1"; }
cinza() { printf "\033[0;90m%s\033[0m\n" "$1"; }

echo ""
echo "================================================================"
echo " Transcriber Live $VERSAO — montador do instalador"
echo "================================================================"

# ---------------------------------------------------------------- 1. binarios
ORIGEM="${1:-}"
if [ -z "$ORIGEM" ]; then
    for tentativa in "./binarios" "../build" "$HOME/Downloads/TranscriberLive-macOS" "$HOME/Downloads"; do
        if [ -d "$tentativa" ] && [ -n "$(/usr/bin/find "$tentativa" -maxdepth 4 -name '*.vst3' -print -quit 2>/dev/null)" ]; then
            ORIGEM="$tentativa"; break
        fi
    done
fi

if [ -z "$ORIGEM" ] || [ ! -d "$ORIGEM" ]; then
    verm "Nao achei os binarios."
    echo ""
    echo "Coloque os .vst3 / .component / .app numa pasta chamada 'binarios' aqui do lado"
    echo "(ou passe a pasta como argumento). Se voce baixou do GitHub Actions, e so"
    echo "descompactar o zip TranscriberLive-macOS dentro de 'binarios'."
    echo ""
    read -r -p "Enter para fechar..." _
    exit 1
fi

cinza "procurando binarios em: $ORIGEM"

TMP="$(/usr/bin/mktemp -d)"
trap '/bin/rm -rf "$TMP"' EXIT
/bin/mkdir -p "$TMP/VST3" "$TMP/Components" "$TMP/Applications" "$TMP/models"

achados=0
copiar_bundles() {
    padrao="$1"; destino="$2"
    while IFS= read -r bundle; do
        [ -z "$bundle" ] && continue
        # ignora copias dentro de outros bundles
        case "$bundle" in *".app/Contents/"*) continue;; esac
        nome="$(basename "$bundle")"
        if [ -e "$destino/$nome" ]; then continue; fi
        /usr/bin/ditto "$bundle" "$destino/$nome" && { echo "  + $nome"; achados=$((achados+1)); }
    done < <(/usr/bin/find "$ORIGEM" -maxdepth 6 -name "$padrao" -type d 2>/dev/null | /usr/bin/sort)
}

echo ""
echo "Plugins e aplicativo:"
copiar_bundles "*.vst3"      "$TMP/VST3"
copiar_bundles "*.component" "$TMP/Components"
copiar_bundles "*.app"       "$TMP/Applications"

if [ "$achados" -eq 0 ]; then
    verm "Nenhum plugin encontrado em $ORIGEM."
    read -r -p "Enter para fechar..." _
    exit 1
fi

# ---------------------------------------------------------------- 2. modelos
echo ""
echo "Modelos:"
MODELOS_ORIGEM=""
for tentativa in "./modelos" "$HOME/Library/Application Support/TranscriberLive/models"; do
    if [ -d "$tentativa" ] && [ -n "$(/bin/ls "$tentativa"/ggml-*.bin 2>/dev/null)" ]; then
        MODELOS_ORIGEM="$tentativa"; break
    fi
done

if [ -n "$MODELOS_ORIGEM" ]; then
    cinza "  de: $MODELOS_ORIGEM"
    for m in "$MODELOS_ORIGEM"/ggml-*.bin; do
        [ -e "$m" ] || continue
        /usr/bin/ditto "$m" "$TMP/models/$(basename "$m")" && echo "  + $(basename "$m")"
    done
else
    echo "  (nenhum modelo — o instalador vai sair sem eles; o cliente baixa depois)"
    echo "  para incluir, crie uma pasta 'modelos' aqui com os ggml-*.bin"
fi

# ---------------------------------------------------------------- 3. zip
echo ""
echo "Compactando payload..."
/bin/mkdir -p scripts
/bin/rm -f scripts/TranscriberLive-payload.zip
( cd "$TMP" && /usr/bin/ditto -c -k --sequesterRsrc . "$OLDPWD/scripts/TranscriberLive-payload.zip" ) \
    || { verm "falha ao compactar."; read -r -p "Enter..." _; exit 1; }
cinza "  scripts/TranscriberLive-payload.zip  ($(/usr/bin/du -h scripts/TranscriberLive-payload.zip | /usr/bin/awk '{print $1}'))"

/bin/chmod +x scripts/postinstall 2>/dev/null

# ---------------------------------------------------------------- 4. pkg
echo ""
echo "Gerando o instalador..."
/bin/mkdir -p "$SAIDA" recursos
[ -f Introducao.txt ] && /bin/cp Introducao.txt recursos/
[ -f Licenca-de-uso.txt ] && /bin/cp Licenca-de-uso.txt recursos/

/usr/bin/pkgbuild \
    --identifier "$ID.core" \
    --version "$VERSAO" \
    --nopayload \
    --scripts scripts \
    --install-location / \
    "$SAIDA/core.pkg" >/dev/null || { verm "pkgbuild falhou."; read -r -p "Enter..." _; exit 1; }

# distribution.xml: tela de introducao + exigencia de macOS 11
{
cat <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Transcriber Live</title>
    <organization>$ID</organization>
XML
[ -f recursos/Introducao.txt ]     && echo '    <welcome file="Introducao.txt" mime-type="text/plain"/>'
[ -f recursos/Licenca-de-uso.txt ] && echo '    <license file="Licenca-de-uso.txt" mime-type="text/plain"/>'
cat <<XML
    <options customize="never" require-scripts="true" hostArchitectures="arm64,x86_64"/>
    <volume-check>
        <allowed-os-versions><os-version min="11.0"/></allowed-os-versions>
    </volume-check>
    <choices-outline><line choice="default"/></choices-outline>
    <choice id="default" title="Transcriber Live"><pkg-ref id="$ID.core"/></choice>
    <pkg-ref id="$ID.core" version="$VERSAO" auth="Root">core.pkg</pkg-ref>
</installer-gui-script>
XML
} > "$SAIDA/distribution.xml"

/usr/bin/productbuild \
    --distribution "$SAIDA/distribution.xml" \
    --package-path "$SAIDA" \
    --resources recursos \
    "$PKG" >/dev/null || { verm "productbuild falhou."; read -r -p "Enter..." _; exit 1; }

/bin/rm -f "$SAIDA/core.pkg" "$SAIDA/distribution.xml"

echo ""
verde "Pronto: $PKG  ($(/usr/bin/du -h "$PKG" | /usr/bin/awk '{print $1}'))"
echo ""
echo "Teste antes de mandar para alguem:"
echo "  sudo installer -pkg \"$PKG\" -target /"
echo "  cat /private/tmp/transcriberlive-install.log"
echo ""
cinza "Sem assinatura, o macOS do cliente vai pedir 'Abrir mesmo assim' em"
cinza "Ajustes > Privacidade e Seguranca. Para evitar isso e preciso conta de"
cinza "desenvolvedor Apple (productsign + notarytool)."
echo ""
/usr/bin/open "$SAIDA" 2>/dev/null
read -r -p "Enter para fechar..." _
