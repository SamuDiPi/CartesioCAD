#!/bin/sh
# Aggiorna translations/cartesiocad_en.ts con le frasi dell'interfaccia (tr() nel C++, qsTr() nel QML di src/app):
# le frasi nuove restano da tradurre, quelle tolte spariscono. Poi tradurre le voci "unfinished" (Qt Linguist o un
# editor di testo) e ricompilare: CMake mette la traduzione nel programma.
#   scripts/update-translations.sh
set -eu
cd "$(dirname "$0")/.."
lupdate -silent -locations none -no-obsolete src/app -extensions cpp,h,qml -ts translations/cartesiocad_en.ts
grep -c 'type="unfinished"' translations/cartesiocad_en.ts | sed 's/^/Frasi da tradurre: /' || true
