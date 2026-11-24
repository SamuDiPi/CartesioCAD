#!/usr/bin/env bash
# Compila i convertitori DWG di GNU LibreDWG (GPL-3.0-or-later) per Windows dai sorgenti ufficiali, con le
# correzioni di sicurezza di patches/libredwg/, e li mette in third_party/libredwg-<versione>, da dove CMake li
# copia accanto a CartesioCAD.exe. Gli eseguibili sono del tutto statici (anche libiconv): partono da soli, senza
# DLL accanto e senza MSYS2 nel PATH. Lo script lo controlla con objdump.
#
# Si usa al posto di scripts/fetch-libredwg.sh quando una vulnerabilità nota non è ancora corretta nelle versioni
# pubblicate da LibreDWG (CVE-2026-111167, novembre 2026: non corretta nemmeno nelle istantanee).
#
# Uso (dalla cartella del progetto, con MSYS2 in C:\msys64):  scripts/build-libredwg.sh
set -euo pipefail

VERSION="0.14.8413"
ARCHIVE="libredwg-${VERSION}.tar.xz"
URL="https://github.com/LibreDWG/libredwg/releases/download/${VERSION}/${ARCHIVE}"
SHA256="7a1103de47fdc191882586a515d577876a71264ba607369e57751cd29cb2e0f4"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${ROOT}/third_party/libredwg-${VERSION}"
MSYS="${MSYS2_ROOT:-/c/msys64}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "Scarico ${URL}"
curl -fsSL -o "${WORK}/${ARCHIVE}" "${URL}"
echo "${SHA256}  ${WORK}/${ARCHIVE}" | sha256sum -c -
tar -xJf "${WORK}/${ARCHIVE}" -C "${WORK}"
SRC="${WORK}/libredwg-${VERSION}"
for p in "${ROOT}"/patches/libredwg/*.patch; do
    echo "Correzione: $(basename "$p")"
    patch -d "${SRC}" -p1 --forward < "$p"
done

# Compilazione nell'ambiente UCRT64 di MSYS2 (stesso compilatore di CartesioCAD).
win_src="$(cygpath -m "${SRC}")"
MSYSTEM=UCRT64 CHERE_INVOKING=1 "${MSYS}/usr/bin/bash.exe" -lc "
    set -e
    cd '${win_src}'
    ./configure --disable-shared --enable-static --disable-bindings --disable-python --enable-release \
        --host=x86_64-w64-mingw32 >/dev/null
    make -j\$(nproc) -C src >/dev/null
    # -all-static: libtool collega in modo statico anche le librerie di MSYS2 (libiconv).
    make -C programs dwg2dxf.exe dxf2dwg.exe LDFLAGS='-all-static' >/dev/null
"
rm -rf "${DEST}"
mkdir -p "${DEST}"
cp "${SRC}/programs/dwg2dxf.exe" "${SRC}/programs/dxf2dwg.exe" "${DEST}/"
"${MSYS}/ucrt64/bin/strip.exe" "${DEST}"/*.exe
# Solo DLL di Windows (KERNEL32 e il runtime UCRT api-ms-win-crt-*, presenti da Windows 10): niente da MSYS2.
for exe in "${DEST}"/*.exe; do
    extra="$("${MSYS}/ucrt64/bin/objdump.exe" -p "$exe" | sed -n 's/^[[:space:]]*DLL Name: //p' \
        | grep -v -i -E '^(KERNEL32|USER32|ADVAPI32|SHELL32|WS2_32|api-ms-win-crt-.*)\.dll$' || true)"
    if [ -n "$extra" ]; then
        echo "ERRORE: $(basename "$exe") dipende ancora da: $extra" >&2
        exit 1
    fi
done
cp "${SRC}/COPYING" "${DEST}/COPYING"
echo "LibreDWG ${VERSION} con le correzioni di patches/libredwg compilata in ${DEST}:"
ls -l "${DEST}"
