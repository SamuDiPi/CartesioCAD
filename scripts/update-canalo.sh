#!/bin/sh
# Aggiorna la copia di CanaloCAD distribuita con CartesioCAD (plugins/CanaloCAD/Canalo.html)
# con la build del repository di CanaloCAD: scripts/update-canalo.sh [cartella di CanaloCAD]
set -eu
src="${1:-$HOME/Documents/Canalo}"
dest="$(dirname "$0")/../plugins/CanaloCAD"
if [ ! -f "$src/Canalo.html" ]; then
    echo "Canalo.html non trovato in $src (eseguire prima 'npm run build' in CanaloCAD)" >&2
    exit 1
fi
mkdir -p "$dest"
cp "$src/Canalo.html" "$dest/Canalo.html"

# Motore per CartesioCAD: i moduli di calcolo, importazione, scena e DXF della pagina (senza interfaccia), eseguiti
# con il motore JavaScript di Qt (app/CanaloEngine) accanto a engine/host.js: stessa tavola DXF della pagina.
mkdir -p "$dest/engine"
{
    echo "// Generato da scripts/update-canalo.sh dai sorgenti di CanaloCAD: non modificare a mano."
    for f in 00-utils.js 01-data.js 02-core.js 03-loadcurve-cables.js 04-import-cad.js 05-import-pdf.js 06-scene-iso.js 07-scene-2d.js \
             09-database-ui.js 10-sections.js 11-dxf-template.js 11-export.js; do
        echo "// ---- $f"
        cat "$src/src/js/$f"
    done
} | # Il motore JavaScript di Qt non conosce async/await: le usano solo le funzioni di file e download della
    # pagina (aprire un DWG o un PDF, salvare il progetto), che il motore non chiama. Diventano funzioni normali.
    sed -E 's/\basync (function|\()/\1/g; s/\bawait //g' > "$dest/engine/canalo-engine.js"
commit="$(git -C "$src" log -1 --format='%h %s' 2>/dev/null || echo 'sconosciuto')"
version="$(sed -n 's/.*const APP_VERSION = "\(.*\)";.*/\1/p' "$src/src/js/00-utils.js" 2>/dev/null || true)"
cat > "$dest/VERSIONE.txt" <<EOF
CanaloCAD V${version:-?} - https://github.com/SamuDiPi/Canalo
Commit: $commit
Copiato da scripts/update-canalo.sh: Canalo.html è generato, non si modifica qui.
EOF
echo "CanaloCAD aggiornato: $commit"
