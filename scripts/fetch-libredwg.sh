#!/usr/bin/env bash
# Scarica i convertitori DWG di LibreDWG (GNU, GPL-3.0) per Windows e li mette in
# third_party/libredwg-<versione>, da dove CMake li copia accanto a CartesioCAD.exe.
#
# Uso (da MSYS2 o Git Bash, nella cartella del progetto):  scripts/fetch-libredwg.sh
set -euo pipefail

VERSION="0.14"
ARCHIVE="libredwg-${VERSION}-win64.zip"
URL="https://github.com/LibreDWG/libredwg/releases/download/${VERSION}/${ARCHIVE}"
# Impronta del pacchetto ufficiale, firmato dal responsabile di LibreDWG (chiave nel portachiavi GNU).
SHA256="1ad7e15344d20b3426c3435b078d82fb84b35062815946b2cca9c5fc9810fea8"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${ROOT}/third_party/libredwg-${VERSION}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "Scarico ${URL}"
curl -fsSL -o "${WORK}/${ARCHIVE}" "${URL}"

echo "Verifico l'impronta SHA-256"
echo "${SHA256}  ${WORK}/${ARCHIVE}" | sha256sum -c -

mkdir -p "${DEST}"
unzip -q -o -j "${WORK}/${ARCHIVE}" dwg2dxf.exe dxf2dwg.exe libredwg-0.dll -d "${DEST}"
echo "LibreDWG ${VERSION} pronta in ${DEST}"
echo "Ora riconfigurare e ricompilare:  cmake -S . -B build && cmake --build build"
