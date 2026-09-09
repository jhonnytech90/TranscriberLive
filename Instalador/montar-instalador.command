#!/bin/bash
#
# Transcriber Live — monta o instalador .pkg do macOS
#
# Duplo clique aqui, ou:  ./montar-instalador.command [pasta-com-os-binarios]
#
# O que sai: um unico TranscriberLive-0.4.pkg que
#   - instala os VST3, os AU e o aplicativo Display
#   - mostra a tela "Personalizar" com os modelos de voz em caixinhas
#   - baixa do Hugging Face so os modelos marcados, conferindo o SHA-256
#
# Como funciona por dentro:
#   core.pkg          -> payload = um zip em /Applications; o postinstall move tudo
#   model-<nome>.pkg  -> sem payload; o postinstall baixa aquele modelo
#   distribution.xml  -> junta tudo, com a capa lateral e as escolhas
#
# Nao interativo (para o CI):  TL_NAO_INTERATIVO=1 ./montar-instalador.command pasta
#
set -u
cd "$(dirname "$0")" || exit 1

VERSAO="0.4"
ID="com.jhonatanmiikael.transcriberlive"
SAIDA="saida"
PKG="$SAIDA/TranscriberLive-$VERSAO.pkg"
LOTE="${TL_NAO_INTERATIVO:-0}"

verde() { printf "\033[0;32m%s\033[0m\n" "$1"; }
verm()  { printf "\033[0;31m%s\033[0m\n" "$1"; }
cinza() { printf "\033[0;90m%s\033[0m\n" "$1"; }
pausa() { [ "$LOTE" = "1" ] || read -r -p "${1:-Enter para fechar...}" _; }
morre() { verm "$1"; pausa; exit 1; }

echo ""
echo "================================================================"
echo " Transcriber Live $VERSAO — montador do instalador (macOS)"
echo "================================================================"

# ---------------------------------------------------------------- 0. modelos
# nome|url|sha256|rotulo (o VAD nao entra aqui: vai dentro do core, obrigatorio)
HF="https://huggingface.co/ggerganov/whisper.cpp/resolve/main"
MODELOS=(
"tiny|$HF/ggml-tiny.bin|be07e048e1e599ad46341c8d2a135645097a538221678b7acdd1b1919c6e1b21|tiny — 74 MB — o mais leve, erra mais"
"base|$HF/ggml-base.bin|60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe|base — 141 MB — bom para Mac modesto"
"small|$HF/ggml-small.bin|1be3a9b2063867b937e64e2ec7483364a79917e157fa98c5d94b5c1fffea987b|small — 465 MB — recomendado para show"
"medium|$HF/ggml-medium.bin|6c14d5adee5f86394037b4e4e8b59f1673b6cee10e3cf0b11bbdbee79c156208|medium — 1,4 GB — mais preciso, exige CPU boa"
"large-v3-turbo|$HF/ggml-large-v3-turbo.bin|1fc70f774d38eb169993ac391eea357ef47c88757ef72ee5943879b7e8e2bc69|large-v3-turbo — 1,5 GB — o melhor, so em Mac forte"
)
PADRAO="small"       # este vem marcado; os outros, desmarcados

VAD_URL="https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v5.1.2.bin"
VAD_SHA="29940d98d42b91fbd05ce489f3ecf7c72f0a42f027e4875919a28fb4c04ea2cf"

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
    echo "Coloque os .vst3 / .component / .app numa pasta chamada 'binarios' aqui do"
    echo "lado (ou passe a pasta como argumento). Se voce baixou do GitHub Actions, e"
    echo "so descompactar o zip TranscriberLive-macOS dentro de 'binarios'."
    echo ""
    pausa; exit 1
fi

cinza "binarios : $ORIGEM"

TMP="$(/usr/bin/mktemp -d)"
trap '/bin/rm -rf "$TMP"' EXIT
/bin/mkdir -p "$TMP/payload/VST3" "$TMP/payload/Components" "$TMP/payload/Applications" "$TMP/payload/models"

achados=0
copiar_bundles() {
    padrao="$1"; destino="$2"
    while IFS= read -r bundle; do
        [ -z "$bundle" ] && continue
        case "$bundle" in *".app/Contents/"*) continue;; esac
        nome="$(basename "$bundle")"
        [ -e "$destino/$nome" ] && continue
        /usr/bin/ditto "$bundle" "$destino/$nome" || continue

        # O zip do GitHub Actions NAO preserva o bit de execucao. Sem isso o
        # binario dentro do bundle nao roda e o host reclama de plugin quebrado.
        /bin/chmod -R a+rX "$destino/$nome" 2>/dev/null
        for exe in "$destino/$nome"/Contents/MacOS/*; do
            [ -f "$exe" ] && /bin/chmod a+x "$exe" 2>/dev/null
        done

        echo "  + $nome"; achados=$((achados+1))
    done < <(/usr/bin/find "$ORIGEM" -maxdepth 6 -name "$padrao" -type d 2>/dev/null | /usr/bin/sort)
}

echo ""
echo "Plugins e aplicativo:"
copiar_bundles "*.vst3"      "$TMP/payload/VST3"
copiar_bundles "*.component" "$TMP/payload/Components"
copiar_bundles "*.app"       "$TMP/payload/Applications"
[ "$achados" -eq 0 ] && morre "Nenhum plugin encontrado em $ORIGEM."

# ---------------------------------------------------------------- 2. VAD (obrigatorio)
# 0,9 MB: vai dentro do instalador mesmo, para o Receiver funcionar mesmo que o
# Mac do cliente esteja sem internet na hora da instalacao.
echo ""
echo "Detector de voz (Silero VAD):"
VAD_LOCAL=""
for t in "./modelos/ggml-silero-v5.1.2.bin" "$HOME/Library/Application Support/TranscriberLive/models/ggml-silero-v5.1.2.bin"; do
    [ -f "$t" ] && { VAD_LOCAL="$t"; break; }
done
if [ -z "$VAD_LOCAL" ]; then
    cinza "  baixando do Hugging Face..."
    if /usr/bin/curl -fL --retry 3 --connect-timeout 20 -o "$TMP/vad.bin" "$VAD_URL" 2>/dev/null; then
        VAD_LOCAL="$TMP/vad.bin"
    fi
fi
if [ -n "$VAD_LOCAL" ]; then
    got="$(/usr/bin/shasum -a 256 "$VAD_LOCAL" | /usr/bin/awk '{print $1}')"
    if [ "$got" = "$VAD_SHA" ]; then
        /usr/bin/ditto "$VAD_LOCAL" "$TMP/payload/models/ggml-silero-v5.1.2.bin"
        echo "  + ggml-silero-v5.1.2.bin"
    else
        verm "  SHA-256 do VAD nao confere — o instalador vai sair sem ele."
    fi
else
    verm "  nao consegui obter o VAD (sem internet?). O instalador sai sem ele."
fi

# modelos grandes que voce queira embutir (opcional): pasta ./modelos
if [ -d "./modelos" ]; then
    for m in ./modelos/ggml-*.bin; do
        [ -e "$m" ] || continue
        case "$(basename "$m")" in *silero*|*vad*) continue;; esac
        /usr/bin/ditto "$m" "$TMP/payload/models/$(basename "$m")" && echo "  + $(basename "$m") (embutido)"
    done
fi

# ---------------------------------------------------------------- 3. core.pkg
echo ""
echo "Compactando o payload..."
/bin/mkdir -p scripts "$SAIDA" recursos
/bin/rm -f scripts/TranscriberLive-payload.zip
( cd "$TMP/payload" && /usr/bin/ditto -c -k --sequesterRsrc . "$OLDPWD/scripts/TranscriberLive-payload.zip" ) \
    || morre "falha ao compactar."
cinza "  scripts/TranscriberLive-payload.zip  ($(/usr/bin/du -h scripts/TranscriberLive-payload.zip | /usr/bin/awk '{print $1}'))"
/bin/chmod +x scripts/postinstall 2>/dev/null

echo ""
echo "Gerando os pacotes..."
/bin/rm -f "$SAIDA"/*.pkg
ROOT="$TMP/root"
/bin/mkdir -p "$ROOT/Applications"
/bin/cp scripts/TranscriberLive-payload.zip "$ROOT/Applications/TranscriberLive-payload.zip"

/usr/bin/pkgbuild \
    --identifier "$ID.core" \
    --version "$VERSAO" \
    --root "$ROOT" \
    --scripts scripts \
    --install-location / \
    "$SAIDA/core.pkg" >/dev/null || morre "pkgbuild (core) falhou."
cinza "  core.pkg"

# ---------------------------------------------------------------- 4. um pkg por modelo
# Cada um e um pacote SEM payload: so um postinstall que baixa aquele arquivo.
# Assim o Installer.app mostra as caixinhas nativas na tela "Personalizar".
for linha in "${MODELOS[@]}"; do
    IFS='|' read -r nome url sha rotulo <<< "$linha"
    dir="$TMP/scripts-$nome"
    /bin/mkdir -p "$dir"

    /bin/cat > "$dir/postinstall" <<SCRIPT
#!/bin/bash
#
# Transcriber Live — baixa o modelo ggml-$nome.bin
#
# Pacote sem payload: quem coloca o arquivo no disco e este script.
# Regras: nunca sobrescreve um modelo existente, confere o SHA-256, e NUNCA
# falha a instalacao — os plugins ja estao no lugar, um download que nao deu
# nao pode desfazer isso.
#
ARQUIVO="ggml-$nome.bin"
URL="$url"
SHA="$sha"
DEST="\${TL_TEST_ROOT:-}/Library/Application Support/TranscriberLive/models"
LOG="/private/tmp/transcriberlive-install.log"

exec >> "\$LOG" 2>&1
echo ""
echo "--- modelo \$ARQUIVO em \$(date '+%H:%M:%S') ---"

/bin/mkdir -p "\$DEST" || { echo "ERRO: nao consegui criar \$DEST"; exit 0; }

if [ -f "\$DEST/\$ARQUIVO" ]; then
    echo "ja existe, mantido: \$DEST/\$ARQUIVO"
    exit 0
fi

# avisa quem esta na frente do Mac (o Installer so mostra "executando scripts")
USUARIO=\$(/usr/bin/stat -f%Su /dev/console 2>/dev/null)
avisar() {
    if [ -n "\${TL_TEST_ROOT:-}" ]; then return 0; fi
    if [ -z "\$USUARIO" ] || [ "\$USUARIO" = "root" ]; then return 0; fi
    UID_U=\$(/usr/bin/id -u "\$USUARIO" 2>/dev/null) || return 0
    /bin/launchctl asuser "\$UID_U" /usr/bin/sudo -u "\$USUARIO" \\
        /usr/bin/osascript -e "display notification \"\$1\" with title \"Transcriber Live\"" \\
        >/dev/null 2>&1
}

avisar "Baixando o modelo $nome..."
echo "baixando \$URL"

TMPD=\$(/usr/bin/mktemp -d /private/tmp/tl-modelo-XXXXXX) || exit 0
/usr/bin/curl -fL --retry 3 --retry-delay 5 --connect-timeout 30 --progress-bar \\
    -o "\$TMPD/\$ARQUIVO" "\$URL"
CURL=\$?

if [ "\$CURL" -ne 0 ] || [ ! -s "\$TMPD/\$ARQUIVO" ]; then
    echo "ERRO: download falhou (curl \$CURL)"
    avisar "Nao consegui baixar o modelo $nome. Veja o Leia-me."
    /bin/rm -rf "\$TMPD"
    exit 0
fi

GOT=\$(/usr/bin/shasum -a 256 "\$TMPD/\$ARQUIVO" | /usr/bin/awk '{print \$1}')
if [ "\$GOT" != "\$SHA" ]; then
    echo "ERRO: SHA-256 nao confere"
    echo "  esperado \$SHA"
    echo "  obtido   \$GOT"
    avisar "O modelo $nome baixou corrompido e foi descartado."
    /bin/rm -rf "\$TMPD"
    exit 0
fi

/bin/mv -f "\$TMPD/\$ARQUIVO" "\$DEST/\$ARQUIVO" && echo "instalado: \$DEST/\$ARQUIVO"
/bin/rm -rf "\$TMPD"

if [ -z "\${TL_TEST_ROOT:-}" ]; then
    /usr/sbin/chown root:admin "\$DEST/\$ARQUIVO" 2>/dev/null
    /bin/chmod ug+rw,o+r "\$DEST/\$ARQUIVO" 2>/dev/null
fi
avisar "Modelo $nome pronto."
exit 0
SCRIPT

    /bin/chmod +x "$dir/postinstall"
    /usr/bin/pkgbuild \
        --identifier "$ID.model.$nome" \
        --version "$VERSAO" \
        --nopayload \
        --scripts "$dir" \
        --install-location / \
        "$SAIDA/model-$nome.pkg" >/dev/null || morre "pkgbuild (model-$nome) falhou."
    cinza "  model-$nome.pkg"
done

# ---------------------------------------------------------------- 5. distribution.xml
[ -f Introducao.txt ]     && /bin/cp Introducao.txt recursos/
[ -f Licenca-de-uso.txt ] && /bin/cp Licenca-de-uso.txt recursos/
[ -f capa.png ]           && /bin/cp capa.png recursos/
[ -f capa-dark.png ]      && /bin/cp capa-dark.png recursos/

{
cat <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Transcriber Live</title>
    <organization>$ID</organization>
XML
[ -f recursos/capa.png ] && \
    echo '    <background file="capa.png" mime-type="image/png" alignment="bottomleft" scaling="proportional"/>'
[ -f recursos/capa-dark.png ] && \
    echo '    <background-darkAqua file="capa-dark.png" mime-type="image/png" alignment="bottomleft" scaling="proportional"/>'
[ -f recursos/Introducao.txt ]     && echo '    <welcome file="Introducao.txt" mime-type="text/plain"/>'
[ -f recursos/Licenca-de-uso.txt ] && echo '    <license file="Licenca-de-uso.txt" mime-type="text/plain"/>'
cat <<XML
    <options customize="always" require-scripts="true" hostArchitectures="arm64,x86_64"/>
    <volume-check>
        <allowed-os-versions><os-version min="11.0"/></allowed-os-versions>
    </volume-check>

    <choices-outline>
        <line choice="core"/>
        <line choice="modelos">
XML
for linha in "${MODELOS[@]}"; do
    IFS='|' read -r nome _ _ _ <<< "$linha"
    echo "            <line choice=\"m_$nome\"/>"
done
cat <<XML
        </line>
    </choices-outline>

    <choice id="core" title="Plugins e aplicativo"
            description="Receiver e Display (VST3 e AU), o aplicativo Display e o detector de voz. Obrigatorio."
            enabled="false" selected="true">
        <pkg-ref id="$ID.core"/>
    </choice>

    <choice id="modelos" title="Modelos de reconhecimento de voz"
            description="Baixados do Hugging Face durante a instalacao, com o SHA-256 conferido. Modelo maior acerta mais e pesa mais na CPU. Modelos que voce ja tem nao sao baixados de novo."/>
XML
for linha in "${MODELOS[@]}"; do
    IFS='|' read -r nome _ _ rotulo <<< "$linha"
    sel="false"; [ "$nome" = "$PADRAO" ] && sel="true"
    cat <<XML
    <choice id="m_$nome" title="$rotulo" start_selected="$sel">
        <pkg-ref id="$ID.model.$nome"/>
    </choice>
XML
done
cat <<XML

    <pkg-ref id="$ID.core" version="$VERSAO" auth="Root">core.pkg</pkg-ref>
XML
for linha in "${MODELOS[@]}"; do
    IFS='|' read -r nome _ _ _ <<< "$linha"
    echo "    <pkg-ref id=\"$ID.model.$nome\" version=\"$VERSAO\" auth=\"Root\">model-$nome.pkg</pkg-ref>"
done
echo '</installer-gui-script>'
} > "$SAIDA/distribution.xml"

/usr/bin/xmllint --noout "$SAIDA/distribution.xml" 2>/dev/null || cinza "  (xmllint indisponivel — XML nao verificado)"

# ---------------------------------------------------------------- 6. productbuild
/usr/bin/productbuild \
    --distribution "$SAIDA/distribution.xml" \
    --package-path "$SAIDA" \
    --resources recursos \
    "$PKG" >/dev/null || morre "productbuild falhou."

/bin/rm -f "$SAIDA"/core.pkg "$SAIDA"/model-*.pkg "$SAIDA/distribution.xml"

echo ""
verde "Pronto: $PKG  ($(/usr/bin/du -h "$PKG" | /usr/bin/awk '{print $1}'))"
echo ""
echo "Teste antes de mandar para alguem:"
echo "  sudo installer -pkg \"$PKG\" -target /"
echo "  tail -f /private/tmp/transcriberlive-install.log"
echo ""
cinza "Sem assinatura, o macOS do cliente vai pedir 'Abrir mesmo assim' em"
cinza "Ajustes > Privacidade e Seguranca. Para evitar isso e preciso conta de"
cinza "desenvolvedor Apple (productsign + notarytool)."
echo ""
[ "$LOTE" = "1" ] || /usr/bin/open "$SAIDA" 2>/dev/null
pausa
