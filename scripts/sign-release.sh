#!/bin/sh
# Firma l'archivio di una versione per gli aggiornamenti dal programma.
#
#   scripts/sign-release.sh <CartesioCAD-X.Y.Z-win64.zip> <chiave privata .pem> [--major]
#
# Scrive accanto all'archivio CartesioCAD-X.Y.Z-aggiornamento.json (versione, nome, dimensione e SHA-256
# dell'archivio, major) e la sua firma Ed25519 CartesioCAD-X.Y.Z-aggiornamento.json.sig, poi controlla la firma con
# la chiave pubblica. I tre file vanno allegati alla versione su GitHub. La chiave privata resta fuori dal
# repository: senza di lei non si possono pubblicare aggiornamenti installabili dal programma.
set -eu
zip="${1:?uso: sign-release.sh <archivio.zip> <chiave privata .pem> [--major]}"
key="${2:?manca la chiave privata}"
major=false
[ "${3:-}" = "--major" ] && major=true
OPENSSL="${OPENSSL:-openssl}"
name="$(basename "$zip")"
version="$(echo "$name" | sed -n 's/^CartesioCAD-\([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\)-win64\.zip$/\1/p')"
[ -n "$version" ] || { echo "nome dell'archivio non valido: $name (atteso CartesioCAD-X.Y.Z-win64.zip)" >&2; exit 1; }
dir="$(cd "$(dirname "$zip")" && pwd)"
size="$(wc -c < "$zip" | tr -d ' ')"
sha="$(sha256sum "$zip" | cut -c1-64)"
manifest="$dir/CartesioCAD-$version-aggiornamento.json"
printf '{\n  "prodotto": "CartesioCAD",\n  "versione": "%s",\n  "major": %s,\n  "archivio": "%s",\n  "dimensione": %s,\n  "sha256": "%s",\n  "data": "%s"\n}\n' \
    "$version" "$major" "$name" "$size" "$sha" "$(date +%Y-%m-%d)" > "$manifest"
"$OPENSSL" pkeyutl -sign -inkey "$key" -rawin -in "$manifest" -out "$manifest.sig"
pub="$(mktemp)"
"$OPENSSL" pkey -in "$key" -pubout -out "$pub"
"$OPENSSL" pkeyutl -verify -pubin -inkey "$pub" -rawin -in "$manifest" -sigfile "$manifest.sig" >/dev/null
rm -f "$pub"
echo "Firmato: $manifest (+ .sig), versione $version, major $major, $size byte"
