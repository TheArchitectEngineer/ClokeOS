#!/bin/bash
# Baut a64run (arm64-Mach-O auf x86_64, Unicorn-AArch64-Kern) und die arm64-Testprogramme.
#
#   xlat/build-a64.sh [Ausgabeordner]         Ziel: x86_64, Darwin 20 (gelinkt gegen die libSystem des x86_64-Images wie tux/build.sh)
#   A64_HOST=1 xlat/build-a64.sh [Ordner]     dasselbe, aber gegen die libSystem des Mac (nur zum schnellen Ausprobieren unter Rosetta)
#
# Unicorn (GPL-2.0) kommt als Quellbaum nach $WORK/download/unicorn/src (git clone --branch 2.1.4 unicorn-engine/unicorn); es
# wird nur der AArch64-Kern gebaut, nach $WORK/download/unicorn/build-x86_64-a64 (danach löschen). Ohne Quellbaum: Abbruch.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
export CLOKEOS_ARCH=x86_64 CLOKEOS_FLAVOR=desktop
source "$here/config.sh"
out="${1:-$WORK/out/xlat-a64}"
mkdir -p "$out"
uni="$WORK/download/unicorn"
[ -d "$uni/src" ] || { echo "Unicorn-Quellen fehlen: $uni/src" >&2; exit 1; }
sdk="$(xcrun --show-sdk-path)"
b="${A64_UNICORN_BUILD:-$uni/build-x86_64-a64}"
if [ ! -f "$b/libunicorn-static.a" ]; then
  mkdir -p "$b"
  # Unicorn wählt das TCG-Backend nach dem Standardziel des Compilers (-dM -E), nicht nach CMAKE_OSX_ARCHITECTURES: auf einem arm64-Mac
  # gäbe das einen arm64-Code-Generator in einer x86_64-Bibliothek (Absturz im ersten Block). Deshalb ein Compiler-Wrapper mit festem Ziel.
  printf '#!/bin/sh\nexec /usr/bin/cc -target x86_64-apple-macos11.0 "$@"\n' > "$b/cc-x86_64"; chmod +x "$b/cc-x86_64"
  (cd "$b" && cmake "$uni/src" -DCMAKE_C_COMPILER="$b/cc-x86_64" -DCMAKE_BUILD_TYPE=Release -DUNICORN_ARCH="aarch64" -DBUILD_SHARED_LIBS=OFF \
    -DUNICORN_BUILD_TESTS=OFF -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -DCMAKE_OSX_SYSROOT="$sdk" -DCMAKE_C_FLAGS=-fno-stack-check >/dev/null && make -j6 unicorn >/dev/null)
fi
cd "$here/xlat"
if [ -n "${A64_HOST:-}" ]; then
  clang -target x86_64-apple-macos11.0 -isysroot "$sdk" -O1 -Wall -I"$uni/src/include" -o "$out/a64run" a64run.c \
    "$b/libunicorn-static.a" "$b/libaarch64-softmmu.a" "$b/libunicorn-common.a"
else
  libs="$WORK/sysroot/tools-x86_64-${PD_SHA256:0:12}"
  [ -f "$libs/libSystem.B.dylib" ] || { echo "Keine libSystem aus dem x86_64-Image: zuerst tux/build.sh x86_64 laufen lassen." >&2; exit 1; }
  clang -target x86_64-apple-macos11.0 -isysroot "$sdk" -O1 -Wall -fno-stack-check -nostdlib \
    -Wl,-platform_version,macos,11.0,11.5 -Wl,-no_fixup_chains -I"$uni/src/include" -o "$out/a64run" a64run.c \
    "$b/libunicorn-static.a" "$b/libaarch64-softmmu.a" "$b/libunicorn-common.a" "$libs/libSystem.B.dylib"
fi
# exec-Haken für x86_64-Hosts (derselbe Quelltext wie für arm64, Ziel hier /usr/bin/a64run)
if [ -z "${A64_HOST:-}" ]; then
  clang -target x86_64-apple-macos11.0 -isysroot "$sdk" -O1 -Wall -fno-stack-check -nostdlib -dynamiclib \
    -Wl,-platform_version,macos,11.0,11.5 -Wl,-no_fixup_chains -o "$out/libxlat-exec.dylib" exechook.c "$libs/libSystem.B.dylib"
fi
# arm64-Testprogramme: freistehend (statisch) und dynamisch gegen die arm64-libSystem aus dem Image (xlat/xroot-a64.sh holt sie)
clang -target arm64-apple-macos11.0 -O2 -ffreestanding -fno-builtin -fno-stack-protector -nostdlib -static -Wl,-e,_start -o "$out/a64-sys" a64-sys.c
ar_="${A64_ROOT:-$WORK/a64root}/usr/lib"
if [ -f "$ar_/libSystem.B.dylib" ]; then
  for t in hello-dyn; do
    clang -target arm64-apple-macos11.0 -isysroot "$sdk" -O1 -nostdlib -Wl,-platform_version,macos,11.0,11.5 -Wl,-no_fixup_chains -Wl,-e,_main \
      -o "$out/a64-$t" $t.c "$ar_/libSystem.B.dylib" "$ar_/libc++abi.dylib"
  done
fi
file "$out"/*
