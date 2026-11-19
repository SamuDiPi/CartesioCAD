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
commit="$(git -C "$src" log -1 --format='%h %s' 2>/dev/null || echo 'sconosciuto')"
version="$(sed -n 's/.*const APP_VERSION = "\(.*\)";.*/\1/p' "$src/src/js/00-utils.js" 2>/dev/null || true)"
cat > "$dest/VERSIONE.txt" <<EOF
CanaloCAD V${version:-?} - https://github.com/SamuDiPi/Canalo
Commit: $commit
Copiato da scripts/update-canalo.sh: Canalo.html è generato, non si modifica qui.
EOF
echo "CanaloCAD aggiornato: $commit"
