#!/usr/bin/env bash
# Alternativa a scripts/build-libredwg.sh (quello usato per le versioni pubblicate): binari ufficiali, SENZA le
# correzioni di patches/libredwg. Scarica i convertitori DWG di LibreDWG (GNU, GPL-3.0) per Windows e li mette in
# third_party/libredwg-<versione>, da dove CMake li copia accanto a CartesioCAD.exe.
#
# Uso (da MSYS2 o Git Bash, nella cartella del progetto):  scripts/fetch-libredwg.sh
set -euo pipefail

VERSION="0.14.8413"   # istantanea dopo la 0.14 stabile: corregge CVE-2026-15520 e CVE-2026-62250
ARCHIVE="libredwg-${VERSION}-win64.zip"
URL="https://github.com/LibreDWG/libredwg/releases/download/${VERSION}/${ARCHIVE}"
# Impronta del pacchetto ufficiale (pubblicata da GitHub per il file del rilascio).
SHA256="0bd11dcdef710f09110922f9d667bec29bbba21eff3598cb1855de14a16f0197"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${ROOT}/third_party/libredwg-${VERSION}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "Scarico ${URL}"
curl -fsSL -o "${WORK}/${ARCHIVE}" "${URL}"

echo "Verifico l'impronta SHA-256"
echo "${SHA256}  ${WORK}/${ARCHIVE}" | sha256sum -c -

mkdir -p "${DEST}"
# Anche libiconv-2.dll: senza, i convertitori non partono su un PC senza MSYS2 nel PATH.
unzip -q -o -j "${WORK}/${ARCHIVE}" dwg2dxf.exe dxf2dwg.exe libredwg-0.dll libiconv-2.dll -d "${DEST}"
echo "LibreDWG ${VERSION} pronta in ${DEST}"
echo "Ora riconfigurare e ricompilare:  cmake -S . -B build && cmake --build build"
