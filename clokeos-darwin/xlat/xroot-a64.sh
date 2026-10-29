#!/bin/bash
# Holt die arm64-Systembibliotheken aus PureDarwins arm64-Image (dyld, libSystem, libobjc, libc++abi ...) nach
# $WORK/a64root: das ist A64_ROOT für a64run (Gast-Dateisystem für /usr/lib/...). Nichts davon kommt ins Repository.
#   xlat/xroot-a64.sh [Ordner] [libs]     mit "libs" nur die Bibliotheken, sonst auch ein paar arm64-Programme des Images
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
export CLOKEOS_ARCH=arm64 CLOKEOS_FLAVOR=desktop
source "$here/config.sh"
root="${1:-$WORK/a64root}"
read -r root_start _ < <(gpt_part "$PD_IMG" "$GUID_LINUX_FS")
fs="$PD_IMG?offset=$((root_start * 512))"
mkdir -p "$root/usr/lib/system"
for f in /usr/lib/dyld /usr/lib/libSystem.B.dylib /usr/lib/system/libdyld.dylib /usr/lib/libobjc.A.dylib /usr/lib/libc++abi.dylib /usr/lib/libc++.1.dylib; do
  "$DEBUGFS" -R "dump $f $root$f" "$fs" >/dev/null 2>&1
  [ -s "$root$f" ] || { echo "Nicht aus dem Image geholt: $f" >&2; exit 1; }
done
[ "${2:-}" = libs ] && { chmod +x "$root/usr/lib/dyld"; echo "$root"; exit 0; }
mkdir -p "$root/bin"
for f in echo cat uname wc true toybox; do "$DEBUGFS" -R "dump /bin/$f $root/bin/$f" "$fs" >/dev/null 2>&1; done
chmod +x "$root"/bin/* "$root"/usr/lib/dyld 2>/dev/null || true
echo "$root"
