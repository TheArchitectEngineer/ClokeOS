#!/bin/bash
# Baut die kleinen ClokeOS-Hilfsprogramme aus tools/ auf dem Mac für PureDarwin.
#
#   tools/build.sh <ausgabeordner>     Architektur wie überall mit CLOKEOS_ARCH (Standard: x86_64)
#
# Header kommen aus Apples SDK auf dem Mac, gelinkt wird gegen libSystem, IOKit und CoreFoundation
# aus dem PureDarwin-Image (mit debugfs geholt, ohne es zu booten). Das Ergebnis ist ein Mach-O-
# Programm wie PureDarwins eigene: macOS 11, dyld-Info ohne chained fixups.
set -euo pipefail
source "$(dirname "$0")/../config.sh"
need "$DEBUGFS" clang xcrun

out="$1"
libs="$WORK/sysroot/tools-$CLOKE_ARCH-${PD_SHA256:0:12}"
read -r root_start _ < <(gpt_part "$PD_IMG" "$GUID_LINUX_FS")
fs="$PD_IMG?offset=$((root_start * 512))"

if [ ! -f "$libs/.fertig" ]; then
  rm -rf "$libs"
  mkdir -p "$libs"
  # IOKit.framework und CoreFoundation.framework sind im Image nur Verweise auf diese Dateien.
  for f in /usr/lib/libSystem.B.dylib /usr/lib/libIOKitCF.dylib /usr/lib/libCoreFoundation.dylib; do
    "$DEBUGFS" -R "dump $f $libs/$(basename "$f")" "$fs" >/dev/null 2>&1 || true
    [ -s "$libs/$(basename "$f")" ] || { echo "Nicht aus dem Image geholt: $f" >&2; exit 1; }
  done
  touch "$libs/.fertig"
fi

target=x86_64-apple-macos11.0
[ "$CLOKE_ARCH" = arm64 ] && target=arm64-apple-macos11.0
mkdir -p "$out"
clang -target "$target" -isysroot "$(xcrun --show-sdk-path)" -std=gnu11 -O2 -Wall -Wextra \
  -nostdlib -Wl,-platform_version,macos,11.0,11.5 -Wl,-no_fixup_chains \
  -o "$out/clokeos-netup" "$HERE/tools/netup.c" \
  "$libs/libSystem.B.dylib" "$libs/libIOKitCF.dylib" "$libs/libCoreFoundation.dylib"
