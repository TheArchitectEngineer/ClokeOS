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
clang -target arm64-apple-macos11.0 -isysroot "$(xcrun --show-sdk-path)" -O1 -Wall -fno-stack-check -nostdlib -dynamiclib \
  -Wl,-platform_version,macos,11.0,11.5 -Wl,-no_fixup_chains -o "$out/libxlat-exec.dylib" exechook.c "$libs/libSystem.B.dylib"
for t in jitprobe x64run; do
  clang -target arm64-apple-macos11.0 -isysroot "$(xcrun --show-sdk-path)" -O1 -Wall -fno-stack-check -nostdlib \
    -Wl,-platform_version,macos,11.0,11.5 -Wl,-no_fixup_chains -o "$out/$t" $t.c "$libs/libSystem.B.dylib"
done
clang -target x86_64-apple-macos11.0 -nostdlib -static -Wl,-e,_start -o "$out/hello-x64" hello-x64.s
clang -target x86_64-apple-macos11.0 -O2 -ffreestanding -fno-builtin -fno-stack-protector -nostdlib -static -Wl,-e,_start -o "$out/t-sys" t-sys.c
# dynamisches Testprogramm gegen die x86_64-libSystem aus dem Image (xlat/xroot.sh holt sie)
xr="$WORK/x86root/usr/lib"
if [ -f "$xr/libSystem.B.dylib" ]; then
  clang -target x86_64-apple-macos11.0 -isysroot "$(xcrun --show-sdk-path)" -O1 -nostdlib -Wl,-platform_version,macos,11.0,11.5 -Wl,-e,_main \
    -o "$out/hello-dyn" hello-dyn.c "$xr/libSystem.B.dylib" "$xr/libc++abi.dylib"
  clang -target x86_64-apple-macos11.0 -isysroot "$(xcrun --show-sdk-path)" -O1 -nostdlib -Wl,-platform_version,macos,11.0,11.5 -Wl,-e,_main \
    -o "$out/hello-thr" hello-thr.c "$xr/libSystem.B.dylib" "$xr/libc++abi.dylib"
  clang -target x86_64-apple-macos11.0 -isysroot "$(xcrun --show-sdk-path)" -O1 -nostdlib -Wl,-platform_version,macos,11.0,11.5 -Wl,-e,_main \
    -o "$out/hello-sig" hello-sig.c "$xr/libSystem.B.dylib" "$xr/libc++abi.dylib"
  clang -target x86_64-apple-macos11.0 -isysroot "$(xcrun --show-sdk-path)" -O1 -nostdlib -Wl,-platform_version,macos,11.0,11.5 -Wl,-e,_main \
    -o "$out/hello-cv" hello-cv.c "$xr/libSystem.B.dylib" "$xr/libc++abi.dylib"
  clang -target x86_64-apple-macos11.0 -isysroot "$(xcrun --show-sdk-path)" -O1 -nostdlib -Wl,-platform_version,macos,11.0,11.5 -Wl,-e,_main \
    -o "$out/hello-mtx" hello-mtx.c "$xr/libSystem.B.dylib" "$xr/libc++abi.dylib"
  clang -target x86_64-apple-macos11.0 -isysroot "$(xcrun --show-sdk-path)" -O1 -nostdlib -Wl,-platform_version,macos,11.0,11.5 -Wl,-e,_main \
    -o "$out/hello-spawn" hello-spawn.c "$xr/libSystem.B.dylib" "$xr/libc++abi.dylib"
  clang -target x86_64-apple-macos11.0 -isysroot "$(xcrun --show-sdk-path)" -O1 -nostdlib -Wl,-platform_version,macos,11.0,11.5 -Wl,-e,_main \
    -o "$out/hello-rw" hello-rw.c "$xr/libSystem.B.dylib" "$xr/libc++abi.dylib"
fi
file "$out"/*

# ucrun: x64run mit Unicorn (GPL-2.0, QEMU-TCG als Bibliothek) als CPU-Kern. Quelle: git clone --branch 2.1.4 unicorn-engine/unicorn
# nach $WORK/download/unicorn/src; gebaut wird nur, wenn sie da ist (Zwischenstand build-arm64 danach löschen).
uni="$WORK/download/unicorn"
if [ -d "$uni/src" ]; then
  if [ ! -f "$uni/build-arm64/libunicorn-static.a" ]; then
    mkdir -p "$uni/build-arm64"
    (cd "$uni/build-arm64" && cmake ../src -DCMAKE_BUILD_TYPE=Release -DUNICORN_ARCH="x86;aarch64" -DBUILD_SHARED_LIBS=OFF \
      -DUNICORN_BUILD_TESTS=OFF -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
      -DCMAKE_OSX_SYSROOT="$(xcrun --show-sdk-path)" -DCMAKE_C_FLAGS=-fno-stack-check >/dev/null && make -j6 unicorn >/dev/null)
  fi
  b="$uni/build-arm64"
  clang -target arm64-apple-macos11.0 -isysroot "$(xcrun --show-sdk-path)" -O1 -fno-stack-check -nostdlib \
    -Wl,-platform_version,macos,11.0,11.5 -Wl,-no_fixup_chains -I"$uni/src/include" -o "$out/ucrun" ucrun.c jit/jit.c \
    "$b/libunicorn-static.a" "$b/libx86_64-softmmu.a" "$b/libaarch64-softmmu.a" "$b/libunicorn-common.a" "$libs/libSystem.B.dylib"
fi
