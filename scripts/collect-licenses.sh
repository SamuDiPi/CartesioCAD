#!/usr/bin/env bash
# Licenze dei componenti distribuiti con CartesioCAD.
#
# Per ogni DLL ed eseguibile della cartella di distribuzione cerca il pacchetto MSYS2 da cui viene, ne copia
# i testi di licenza in <cartella>/licenze/<pacchetto>/ e scrive <cartella>/LICENZE-TERZE-PARTI.txt con
# versione, licenza (SPDX), sito e indirizzo esatto dei sorgenti di quella versione. Aggiunge LibreDWG e il
# plugin CanaloCAD. Lo esegue "cmake --install"; a mano:
#
#   scripts/collect-licenses.sh <cartella di distribuzione> [--sorgenti <cartella>]
#
# Con --sorgenti scarica anche i pacchetti sorgente (GPL, LGPL, MPL...) nella cartella indicata, da
# allegare a una release: così i sorgenti restano disponibili anche se MSYS2 li togliesse dal suo archivio.
set -euo pipefail

DEST="${1:?uso: collect-licenses.sh <cartella di distribuzione> [--sorgenti <cartella>]}"
SOURCES=""
if [ "${2:-}" = "--sorgenti" ]; then
    SOURCES="${3:?manca la cartella per i sorgenti}"
fi
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MSYS="${MSYS2_ROOT-/c/msys64}"   # vuoto dentro la bash di MSYS2 (la radice è /)
if [ -z "$MSYS" ]; then export PATH="/usr/bin:$PATH"; fi
PACMAN="$MSYS/usr/bin/pacman.exe"
DB="$MSYS/var/lib/pacman/local"
LICDIR="$MSYS/ucrt64/share/licenses"
TEXTS="$LICDIR/qt6-base"   # testi SPDX standard (GPL, LGPL, MIT, Apache, MPL...) distribuiti con Qt
OUT="$DEST/LICENZE-TERZE-PARTI.txt"

if [ ! -x "$PACMAN" ]; then
    echo "collect-licenses: pacman non trovato in $MSYS: licenze di terze parti non raccolte" >&2
    exit 1
fi

rm -rf "$DEST/licenze"
mkdir -p "$DEST/licenze/testi"
cp "$ROOT/LICENSE" "$DEST/LICENSE.txt"

# Pacchetti delle DLL e degli eseguibili (tranne CartesioCAD.exe e LibreDWG, trattati a parte).
declare -A seen=()
declare -A files_of=()
packages=()
while IFS= read -r -d '' f; do
    rel="${f#"$DEST"/}"
    case "$rel" in
        CartesioCAD.exe|libredwg/*) continue ;;
    esac
    name="$(basename "$f")"
    owner=""
    for candidate in "/ucrt64/bin/$name" "/ucrt64/share/qt6/plugins/$rel"; do
        owner="$("$PACMAN" -Qqo "$candidate" 2>/dev/null || true)"
        [ -n "$owner" ] && break
    done
    if [ -z "$owner" ]; then
        echo "collect-licenses: pacchetto di $rel non trovato" >&2
        continue
    fi
    if [ -z "${seen[$owner]:-}" ]; then
        seen[$owner]=1
        packages+=("$owner")
    fi
    files_of[$owner]="${files_of[$owner]:-}$rel "
done < <(find "$DEST" -type f \( -iname '*.dll' -o -iname '*.exe' \) -print0)

spdx_texts() {   # copia i testi standard delle licenze SPDX citate
    local ids
    ids="$(echo "$1" | sed -e 's/spdx://g' -e 's/[()]/ /g' | tr ' ' '\n' | grep -v -E '^(AND|OR|WITH|)$' || true)"
    for id in $ids; do
        [ -f "$TEXTS/$id.txt" ] && cp -n "$TEXTS/$id.txt" "$DEST/licenze/testi/" || true
    done
}

{
    echo "CartesioCAD - componenti di terze parti"
    echo "========================================"
    echo
    echo "CartesioCAD è software libero distribuito secondo la GNU General Public License v3.0 (LICENSE.txt),"
    echo "senza alcuna garanzia. I sorgenti di CartesioCAD sono in https://github.com/SamuDiPi/CartesioCAD"
    echo "(la versione e il commit di questa copia sono indicati in Aiuto > Informazioni su CartesioCAD)."
    echo
    echo "Con CartesioCAD sono distribuiti i componenti seguenti, ciascuno con la sua licenza. I testi delle"
    echo "licenze sono nella cartella \"licenze\": per pacchetto (avvisi di copyright originali) e, in"
    echo "\"licenze/testi\", i testi standard indicati dai codici SPDX. Le librerie sono collegate come DLL"
    echo "separate e possono essere sostituite con altre versioni compatibili (LGPL)."
    echo
    echo "I sorgenti corrispondenti a ciascun componente sono all'indirizzo indicato. Su richiesta (una"
    echo "segnalazione su https://github.com/SamuDiPi/CartesioCAD/issues) li forniamo anche direttamente, per"
    echo "almeno tre anni dalla distribuzione di questa versione."
    echo
    echo "------------------------------------------------------------------------------------------------"
    for pkg in $(printf '%s\n' "${packages[@]}" | sort); do
        info="$("$PACMAN" -Qi "$pkg")"
        version="$(echo "$info" | sed -n 's/^Version *: //p')"
        url="$(echo "$info" | sed -n 's/^URL *: //p')"
        licenses="$(echo "$info" | sed -n 's/^Licenses *: //p')"
        base="$(sed -n '/^%BASE%$/{n;p;}' "$DB/$pkg-$version/desc")"
        short="${pkg#mingw-w64-ucrt-x86_64-}"
        files="${files_of[$pkg]}"
        echo
        echo "$short $version"
        echo "  File:      $files"
        echo "  Licenza:   ${licenses#spdx:}"
        echo "  Sito:      $url"
        echo "  Sorgenti:  https://repo.msys2.org/mingw/sources/$base-$version.src.tar.zst"
        echo "             (ricetta di compilazione: https://github.com/msys2/MINGW-packages/tree/master/${base#mingw-w64-})"
        # Avvisi di licenza e copyright del pacchetto (share/licenses/<nome>, LICENSE di ICU...).
        while IFS= read -r lf; do
            [ -f "$MSYS$lf" ] || continue
            mkdir -p "$DEST/licenze/$short"
            cp "$MSYS$lf" "$DEST/licenze/$short/"
        done < <("$PACMAN" -Qlq "$pkg" | grep -i -E '/share/licenses/.+[^/]$|/(licen[cs]e|copying|notice)[^/]*$' || true)
        spdx_texts "$licenses"
        if [ -n "$SOURCES" ]; then
            mkdir -p "$SOURCES"
            tarball="$base-$version.src.tar.zst"
            [ -f "$SOURCES/$tarball" ] || curl -fsSL -o "$SOURCES/$tarball" "https://repo.msys2.org/mingw/sources/$tarball"
        fi
    done

    if [ -d "$DEST/libredwg" ]; then
        echo
        echo "LibreDWG 0.14 (GNU)"
        echo "  File:      libredwg/dwg2dxf.exe libredwg/dxf2dwg.exe libredwg/libredwg-0.dll"
        echo "             (programmi separati, usati per leggere e scrivere i file DWG)"
        echo "  Licenza:   GPL-3.0-or-later"
        echo "  Sito:      https://www.gnu.org/software/libredwg/"
        echo "  Sorgenti:  https://ftp.gnu.org/gnu/libredwg/libredwg-0.14.tar.xz"
        echo "             (binari: https://github.com/LibreDWG/libredwg/releases/tag/0.14)"
        cp -n "$TEXTS/GPL-3.0-only.txt" "$DEST/licenze/testi/" || true
        if [ -n "$SOURCES" ]; then
            [ -f "$SOURCES/libredwg-0.14.tar.xz" ] || curl -fsSL -o "$SOURCES/libredwg-0.14.tar.xz" "https://ftp.gnu.org/gnu/libredwg/libredwg-0.14.tar.xz"
        fi
    fi

    if [ -d "$DEST/plugins/CanaloCAD" ]; then
        echo
        echo "CanaloCAD (plugin, plugins/CanaloCAD/Canalo.html)"
        echo "  Licenza:   GPL-3.0-or-later"
        echo "  Sorgenti:  https://github.com/SamuDiPi/Canalo"
        echo "  Contiene il CSS generato con Tailwind CSS (MIT, https://tailwindcss.com). Alla prima importazione"
        echo "  di un DWG o di un PDF la pagina scarica dalla rete LibreDWG-web (GPL-3.0-or-later,"
        echo "  https://github.com/mlightcad/libredwg-web) e pdf.js (Apache-2.0, https://mozilla.github.io/pdf.js/):"
        echo "  non sono inclusi in questa distribuzione."
        cp -n "$TEXTS/MIT.txt" "$TEXTS/Apache-2.0.txt" "$DEST/licenze/testi/" 2>/dev/null || true
    fi

    echo
    echo "------------------------------------------------------------------------------------------------"
    echo "Marchi: i nomi di prodotti e costruttori citati (per esempio nel catalogo di CanaloCAD) appartengono"
    echo "ai rispettivi proprietari e sono usati solo per indicare i prodotti; CartesioCAD non è collegato a"
    echo "loro. DWG è usato solo per indicare il formato dei file."
} > "$OUT"

echo "Licenze di terze parti: ${#packages[@]} pacchetti, elenco in $OUT"
