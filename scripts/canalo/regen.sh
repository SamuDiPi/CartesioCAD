#!/bin/sh
# Rigenera i dati e i test dei calcoli di CanaloCAD dai sorgenti della pagina (serve Node):
#   src/core/CanaloData.inc   catalogo cavi, serie, staffaggi (01-data.js)
#   tests/canalo_expected.inc casi di prova calcolati dalla pagina (02-core.js, 04-import-cad.js)
# scripts/canalo/regen.sh [cartella src/js di CanaloCAD]   (predefinita: Documenti/Canalo/src/js)
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../.."
[ $# -gt 0 ] && export CANALO_SRC="$1"
node "$here/gendata.js" > "$root/src/core/CanaloData.inc"
node "$here/gentests.js" > "$root/tests/canalo_expected.inc"
node "$here/gensection.js" >> "$root/tests/canalo_expected.inc"
echo "Rigenerati CanaloData.inc e canalo_expected.inc: ricompilare ed eseguire cartesio_core_tests."
