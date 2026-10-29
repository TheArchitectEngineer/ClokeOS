#!/bin/bash
# Holt die x86_64-Systembibliotheken aus PureDarwins x86_64-Image (dyld, libSystem, libobjc, libc++abi) nach
# $WORK/x86root: das ist XLAT_ROOT für ucrun (Gast-Dateisystem für /usr/lib/...). Nichts davon kommt ins Repository.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
export CLOKEOS_ARCH=x86_64 CLOKEOS_FLAVOR=minimal
source "$here/config.sh"
root="${1:-$WORK/x86root}"
read -r root_start _ < <(gpt_part "$PD_IMG" "$GUID_LINUX_FS")
fs="$PD_IMG?offset=$((root_start * 512))"
mkdir -p "$root/usr/lib/system"
for f in /usr/lib/dyld /usr/lib/libSystem.B.dylib /usr/lib/system/libdyld.dylib /usr/lib/libobjc.A.dylib /usr/lib/libc++abi.dylib /usr/lib/libc++.1.dylib; do
  "$DEBUGFS" -R "dump $f $root$f" "$fs" >/dev/null 2>&1
  [ -s "$root$f" ] || { echo "Nicht aus dem Image geholt: $f" >&2; exit 1; }
done
[ "${2:-}" = libs ] && { chmod +x "$root/usr/lib/dyld"; echo "$root"; exit 0; }
# ein paar echte x86_64-Programme des Images zum Ausprobieren von ucrun (toybox ist ein Multi-Call-Programm: ls, find, sed, sort, date ...)
mkdir -p "$root/bin"
for f in echo cat uname wc true toybox; do "$DEBUGFS" -R "dump /bin/$f $root/bin/$f" "$fs" >/dev/null 2>&1; done
chmod +x "$root"/bin/* "$root"/usr/lib/dyld 2>/dev/null || true
echo "$root"
