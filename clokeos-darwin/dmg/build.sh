#!/bin/bash
# Baut clokeos-dmg (dmg/dmg.c) auf dem Mac für PureDarwin und legt es als /usr/libexec/clokeos-dmg in <stage>.
# Das Programm braucht nur libSystem (Inflate, bunzip2 und LZVN sind eingebaut).
#
#   dmg/build.sh <stage>     Architektur wie überall mit CLOKEOS_ARCH
#
# Header aus dem Desktop-Image wie bei CloakServer (Sysroot von cloakserver/build.sh).
set -euo pipefail
source "$(dirname "$0")/../config.sh"
need clang

stage="$1"
sysroot="$WORK/sysroot/$CLOKE_ARCH-${PD_SHA256:0:12}"
[ -f "$sysroot/.fertig" ] || { echo "Fehlt: $sysroot – zuerst cloakserver/build.sh" >&2; exit 1; }
target=x86_64-apple-macos11.0
[ "$CLOKE_ARCH" = arm64 ] && target=arm64-apple-macos11.0
mkdir -p "$stage/usr/libexec"
clang -target "$target" -isysroot "$sysroot" -DPLATFORM_MacOSX=1 -fno-stack-check -std=gnu11 -O2 -Wall -Wextra \
  -Wno-nullability-completeness -I"$sysroot/include" -nostdlib -L"$sysroot/libs" \
  -Wl,-platform_version,macos,11.0,11.5 -Wl,-no_fixup_chains -Wl,-dead_strip \
  -o "$stage/usr/libexec/clokeos-dmg" "$HERE/dmg/dmg.c" -lSystem
echo "Fertig: clokeos-dmg für $CLOKE_ARCH"
