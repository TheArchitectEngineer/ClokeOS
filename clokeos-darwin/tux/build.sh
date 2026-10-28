#!/bin/bash
# Baut tux (Linux-Programme auf ClokeOS) für Darwin 20 (macOS 11) auf x86_64 oder arm64.
#
#   tux/build.sh <x86_64|arm64>      Ergebnis: $WORK/out/tux-<arch>/tux
#
# -fno-stack-check: PureDarwins libSystem hat kein ___chkstk_darwin. Am Ende werden die Symbole mit
# `nm -u` aufgelistet, damit man sie mit den Exporten der libSystem im Gast vergleichen kann.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
arch="${1:?x86_64 oder arm64}"
source "$here/config.sh"
out="$WORK/out/tux-$arch"
mkdir -p "$out"
cd "$here/tux"
clang -arch "$arch" -mmacosx-version-min=11.0 -O2 -fno-stack-check -Wall -Wno-unused-function \
  -o "$out/tux" tux.c sys.c sig.c
strip -x "$out/tux" 2>/dev/null || true
nm -u "$out/tux" | sort > "$out/tux.undefined"
file "$out/tux"
echo "Undefinierte Symbole: $(wc -l < "$out/tux.undefined") ($out/tux.undefined)"
