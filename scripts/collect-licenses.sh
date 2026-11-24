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

# Pacchetti delle DLL e degli eseguibili (tranne CartesioCAD.exe, cartesio-mcp.exe e LibreDWG, trattati a parte).
declare -A seen=()
declare -A files_of=()
packages=()
while IFS= read -r -d '' f; do
    rel="${f#"$DEST"/}"
    case "$rel" in
        CartesioCAD.exe|cartesio-mcp.exe|libredwg/*) continue ;;
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
        if [[ "$version" == *-*.* ]]; then
            # Pacchetto ricompilato da noi (revisione con il punto, es. 4.1.0-1.1): ricetta MSYS2 con correzioni di
            # sicurezza ufficiali del progetto, in attesa della versione ufficiale. I sorgenti sono nell'archivio.
            echo "  Sorgenti:  $base-$version.src.tar.zst nell'archivio dei sorgenti di terze parti di questa versione"
            echo "             (ricetta MSYS2 ricompilata con le correzioni di sicurezza pubblicate dal progetto)"
        else
            echo "  Sorgenti:  https://repo.msys2.org/mingw/sources/$base-$version.src.tar.zst"
            echo "             (ricetta di compilazione: https://github.com/msys2/MINGW-packages/tree/master/${base#mingw-w64-})"
        fi
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

    echo
    echo "Font Hershey (caratteri del font a tratti incorporato in CartesioCAD.exe)"
    echo "  Licenza:   uso libero per qualsiasi scopo, con queste attribuzioni:"
    echo "             The Hershey Fonts were originally created by Dr. A. V. Hershey while working at the"
    echo "             U. S. National Bureau of Standards. The format of the Font data in this distribution was"
    echo "             originally created by James Hurt, Cognition, Inc., 900 Technology Park Drive, Billerica,"
    echo "             MA 01821 (mit-eddie!ci-dandelion!hurt)."
    echo "             I dati non si possono convertire nel formato distribuito dal NTIS."
    echo "  Sorgenti:  src/app/BuiltinStrokeFont.inc"
    if [ -d "$DEST/libredwg" ]; then
        echo
        echo "LibreDWG 0.14.8413 (GNU; istantanea successiva alla 0.14 stabile, con le correzioni di sicurezza)"
        echo "  File:      $(cd "$DEST" && ls libredwg/*.exe libredwg/*.dll 2>/dev/null | tr '\n' ' ')"
        echo "             (programmi separati, usati per leggere e scrivere i file DWG)"
        echo "  Licenza:   GPL-3.0-or-later (testo in licenze/libredwg/COPYING)"
        echo "  Sito:      https://www.gnu.org/software/libredwg/"
        echo "  Sorgenti:  https://github.com/LibreDWG/libredwg/releases/download/0.14.8413/libredwg-0.14.8413.tar.xz"
        echo "             con le correzioni in patches/libredwg/ del repository di CartesioCAD (CVE-2026-111167,"
        echo "             non ancora corretta nelle versioni di LibreDWG); compilati con scripts/build-libredwg.sh"
        cp -n "$TEXTS/GPL-3.0-only.txt" "$DEST/licenze/testi/" || true
        if [ -f "$DEST/libredwg/COPYING" ]; then
            mkdir -p "$DEST/licenze/libredwg"
            mv "$DEST/libredwg/COPYING" "$DEST/licenze/libredwg/COPYING"
        fi
        if [ -n "$SOURCES" ]; then
            cp -n "$ROOT"/patches/libredwg/*.patch "$SOURCES/" 2>/dev/null || true
        fi
        if [ -n "$SOURCES" ]; then
            [ -f "$SOURCES/libredwg-0.14.8413.tar.xz" ] || curl -fsSL -o "$SOURCES/libredwg-0.14.8413.tar.xz" "https://github.com/LibreDWG/libredwg/releases/download/0.14.8413/libredwg-0.14.8413.tar.xz"
        fi
    fi

    if [ -d "$DEST/plugins/CanaloCAD" ]; then
        echo
        echo "CanaloCAD (plugin, plugins/CanaloCAD/Canalo.html)"
        echo "  Licenza:   GPL-3.0-or-later"
        echo "  Sorgenti:  https://github.com/SamuDiPi/Canalo"
        echo "  Contiene i font Inter e JetBrains Mono (SIL OFL 1.1, testi in plugins/CanaloCAD), il CSS"
        echo "  generato con Tailwind CSS (MIT, https://tailwindcss.com) e pdf.js 6.5.438 (Mozilla Foundation,"
        echo "  Apache-2.0, https://mozilla.github.io/pdf.js/; testo in licenze/testi/Apache-2.0.txt)."
        echo "  Solo all'importazione di un DWG la pagina scarica da cdn.jsdelivr.net LibreDWG-web"
        echo "  (@mlightcad/libredwg-web 0.9.3, GPL-3.0, contiene GNU LibreDWG GPL-3.0-or-later):"
        echo "  non è incluso né distribuito con CartesioCAD."
        cp -n "$TEXTS/MIT.txt" "$TEXTS/Apache-2.0.txt" "$DEST/licenze/testi/" 2>/dev/null || true
    fi

    echo
    echo "Inno Setup (solo nell'installatore CartesioCAD-X.Y.Z-installazione.exe, non nel programma)"
    echo "  Copyright (C) 1997-2026 Jordan Russell. Portions Copyright (C) 2000-2026 Martijn Laan."
    echo "  Licenza:   licenza di Inno Setup (testo in licenze/inno-setup/license.txt)"
    echo "  Sito:      https://jrsoftware.org/isinfo.php"
    mkdir -p "$DEST/licenze/inno-setup"
    cp "$ROOT/installer/inno-setup-license.txt" "$DEST/licenze/inno-setup/license.txt"

    echo
    echo "------------------------------------------------------------------------------------------------"
    echo "Marchi: CartesioCAD e CanaloCAD non citano marche o prodotti di altri. DWG e DXF sono usati solo per"
    echo "indicare il formato dei file; i marchi appartengono ai rispettivi titolari."
} > "$OUT"

echo "Licenze di terze parti: ${#packages[@]} pacchetti, elenco in $OUT"
