#!/bin/bash
# Holt für den WebKitGTK-Cross-Build alle Bibliotheken, Header und pkg-config-Dateien (ohne LLVM) aus dem
# arm64-Desktop-Image nach $WORK/sysroot/webkit-arm64/ (debugfs, ohne das Image zu booten).
set -euo pipefail
source "$(dirname "$0")/../config.sh"
CLOKE_ARCH=arm64
need "$DEBUGFS"
sr="$WORK/sysroot/webkit-arm64"
read -r root_start _ < <(gpt_part "$PD_IMG" "$GUID_LINUX_FS")
fs="$PD_IMG?offset=$((root_start * 512))"
mkdir -p "$sr/lib" "$sr/usr"
"$DEBUGFS" -R "rdump /usr/include $sr/usr" "$fs" >/dev/null 2>&1 || true
"$DEBUGFS" -R "rdump /include $sr" "$fs" >/dev/null 2>&1 || true
for n in $("$DEBUGFS" -R "ls /lib" "$fs" 2>/dev/null | tr -s ' ' '\n' | grep -v '^[0-9(]' | grep -v -e '^\.\.\?$' -e LLVM -e clang -e '^python' -e '^cmake' -e wlroots); do
  [ -e "$sr/lib/$n" ] || "$DEBUGFS" -R "rdump /lib/$n $sr/lib" "$fs" >/dev/null 2>&1 || true
done
for n in $("$DEBUGFS" -R "ls /usr/lib" "$fs" 2>/dev/null | tr -s ' ' '\n' | grep -v '^[0-9(]' | grep -v -e '^\.\.\?$' -e LLVM -e clang -e '^python'); do
  [ -e "$sr/usr/lib/$n" ] || "$DEBUGFS" -R "rdump /usr/lib/$n $sr/usr/lib" "$fs" >/dev/null 2>&1 || true
done
cp -f "$sr/usr/lib/libSystem.B.dylib" "$sr/usr/lib/libSystem.dylib" 2>/dev/null || true
mkdir -p "$sr/usr/lib/system"; "$DEBUGFS" -R "dump /usr/lib/system/libdyld.dylib $sr/usr/lib/system/libdyld.dylib" "$fs" >/dev/null 2>&1 || true
"$DEBUGFS" -R "dump /usr/lib/libSystem.B.dylib $sr/usr/lib/libSystem.B.dylib" "$fs" >/dev/null 2>&1 || true
touch "$sr/.fertig"; du -sh "$sr"
