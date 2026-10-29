#!/bin/bash
# Baut den Machbarkeitsbeweis der Übersetzungsschicht (docs/translation-study.md).
#
#   xlat/build.sh [Ausgabeordner]     jitprobe + x64run für arm64 (Darwin 20), hello-x64 für x86_64
#
# Gelinkt wird gegen die libSystem aus dem PureDarwin-Image wie bei tux/build.sh, damit jedes Symbol im Gast da ist.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
export CLOKEOS_ARCH=arm64 CLOKEOS_FLAVOR=desktop
source "$here/config.sh"
out="${1:-$WORK/out/xlat}"
mkdir -p "$out"
libs="$WORK/sysroot/tools-$CLOKE_ARCH-${PD_SHA256:0:12}"
[ -f "$libs/libSystem.B.dylib" ] || { echo "Keine libSystem aus dem arm64-Image: zuerst tux/build.sh arm64 laufen lassen." >&2; exit 1; }
cd "$here/xlat"
for t in jitprobe x64run; do
  clang -target arm64-apple-macos11.0 -isysroot "$(xcrun --show-sdk-path)" -O1 -Wall -fno-stack-check -nostdlib \
    -Wl,-platform_version,macos,11.0,11.5 -Wl,-no_fixup_chains -o "$out/$t" $t.c "$libs/libSystem.B.dylib"
done
clang -target x86_64-apple-macos11.0 -nostdlib -static -Wl,-e,_start -o "$out/hello-x64" hello-x64.s
file "$out"/*
