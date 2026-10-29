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
export CLOKEOS_ARCH="$arch" CLOKEOS_FLAVOR=desktop
source "$here/config.sh"
need "$DEBUGFS" clang xcrun
out="$WORK/out/tux-$arch"
mkdir -p "$out"

# Gelinkt wird gegen die libSystem aus dem PureDarwin-Image (wie tools/build.sh), damit jedes Symbol, das
# tux braucht, im Gast auch da ist; ohne -no_fixup_chains fände dyld im Gast die Bindungen nicht.
libs="$WORK/sysroot/tools-$CLOKE_ARCH-${PD_SHA256:0:12}"
if [ ! -f "$libs/.fertig" ]; then
  read -r root_start _ < <(gpt_part "$PD_IMG" "$GUID_LINUX_FS")
  fs="$PD_IMG?offset=$((root_start * 512))"
  rm -rf "$libs"
  mkdir -p "$libs"
  for f in /usr/lib/libSystem.B.dylib /usr/lib/libIOKitCF.dylib /usr/lib/libCoreFoundation.dylib; do
    "$DEBUGFS" -R "dump $f $libs/$(basename "$f")" "$fs" >/dev/null 2>&1 || true
    [ -s "$libs/$(basename "$f")" ] || { echo "Nicht aus dem Image geholt: $f" >&2; exit 1; }
  done
  touch "$libs/.fertig"
fi
target=x86_64-apple-macos11.0
[ "$arch" = arm64 ] && target=arm64-apple-macos11.0
# x86_64: wenig Platz ab Adresse 0 (kleine Seite 0), damit Linux-Programme mit Adressen unter 4 GiB laden können
extra=()
[ "$arch" = x86_64 ] && extra=(-Wl,-pagezero_size,0x1000 -Wl,-no_pie)
cd "$here/tux"
clang -target "$target" -isysroot "$(xcrun --show-sdk-path)" -std=gnu11 -O2 -fno-stack-check -Wall -Wno-unused-function \
  -Wno-deprecated-declarations -nostdlib -Wl,-platform_version,macos,11.0,11.5 -Wl,-no_fixup_chains ${extra[@]+"${extra[@]}"} \
  -o "$out/tux" tux.c sys.c sig.c thr.c fdx.c sock.c "$libs/libSystem.B.dylib"
strip -x "$out/tux" 2>/dev/null || true
nm -u "$out/tux" | sort > "$out/tux.undefined"
file "$out/tux"
echo "Undefinierte Symbole: $(wc -l < "$out/tux.undefined") ($out/tux.undefined)"
