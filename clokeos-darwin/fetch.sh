#!/bin/bash
# Lädt das PureDarwin-Basisimage und die Quellen für den Bootloader und prüft die Prüfsummen.
set -euo pipefail
source "$(dirname "$0")/config.sh"

fetch_file "$XNU_LOADER_URL" "$XNU_LOADER_SHA256" "$XNU_LOADER_TGZ"
fetch_file "$GNU_EFI_URL" "$GNU_EFI_SHA256" "$GNU_EFI_TGZ"

mkdir -p "$(dirname "$PD_IMG")"
if [ -f "$PD_IMG" ] && [ "$(shasum -a 256 "$PD_IMG" | cut -d' ' -f1)" = "$PD_SHA256" ]; then
  echo "PureDarwin $PD_VERSION ist schon da: $PD_IMG"
  exit 0
fi

echo "Lade PureDarwin $PD_VERSION ($PD_URL)"
curl -fL --retry 5 -C - -o "$PD_IMG" "$PD_URL"

sum="$(shasum -a 256 "$PD_IMG" | cut -d' ' -f1)"
if [ "$sum" != "$PD_SHA256" ]; then
  echo "Prüfsumme stimmt nicht: $sum (erwartet $PD_SHA256)" >&2
  exit 1
fi
echo "OK: $PD_IMG"
