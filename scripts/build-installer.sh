#!/usr/bin/env bash
# Crea l'installatore Windows di CartesioCAD con Inno Setup 7 (installer/CartesioCAD.iss).
#
# Uso (Git Bash o MSYS2, nella cartella del progetto), dopo "cmake --install build --prefix dist/CartesioCAD":
#   scripts/build-installer.sh [cartella preparata] [cartella di uscita]
# ISCC si cerca in $ISCC, poi nell'installazione per utente e in Programmi.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SOURCE="${1:-$ROOT/dist/CartesioCAD}"
OUT="${2:-$ROOT/dist}"
VERSION="$(sed -n 's/^ *VERSION \([0-9][0-9.]*\)$/\1/p' "$ROOT/CMakeLists.txt" | head -1)"
[ -n "$VERSION" ] || { echo "Versione non trovata in CMakeLists.txt" >&2; exit 1; }
[ -f "$SOURCE/CartesioCAD.exe" ] || { echo "Manca $SOURCE/CartesioCAD.exe (prima cmake --install)" >&2; exit 1; }

ISCC="${ISCC:-}"
for candidate in "$ISCC" "${LOCALAPPDATA:-}/Programs/Inno Setup 7/ISCC.exe" "/c/Program Files/Inno Setup 7/ISCC.exe" \
                 "/c/Program Files (x86)/Inno Setup 7/ISCC.exe"; do
    if [ -n "$candidate" ] && [ -f "$candidate" ]; then ISCC="$candidate"; break; fi
done
[ -f "$ISCC" ] || { echo "Inno Setup 7 non trovato (variabile ISCC)" >&2; exit 1; }

win() { cygpath -w "$1" 2>/dev/null || echo "$1"; }
mkdir -p "$OUT"
MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*" "$ISCC" /Q "/DVersione=$VERSION" "/DCartella=$(win "$SOURCE")" "/O$(win "$OUT")" "$(win "$ROOT/installer/CartesioCAD.iss")"
echo "Installatore: $OUT/CartesioCAD-$VERSION-installazione.exe"
