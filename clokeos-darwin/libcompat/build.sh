#!/bin/bash
# Baut libclokeos_compat (Symbole, die Mac-Programme an libSystem binden und PureDarwins libSystem
# 20.5 nicht hat: compat.c, chkstk.S, system.c; dazu libffi.dylib aus PureDarwins libffi.a) und legt sie samt einer libSystem.B.dylib, die sie
# zusätzlich re-exportiert, in <stage>. Zweistufig gebundene Programme (Homebrews Ruby, Bottles)
# finden die Symbole so über libSystem.
#
#   libcompat/build.sh <stage>     Architektur wie überall mit CLOKEOS_ARCH
set -euo pipefail
source "$(dirname "$0")/../config.sh"
need "$DEBUGFS" clang nm codesign

stage="$1"
read -r root_start _ < <(gpt_part "$PD_IMG" "$GUID_LINUX_FS")
fs="$PD_IMG?offset=$((root_start * 512))"
out="$WORK/tmp/libcompat-$CLOKE_ARCH"
rm -rf "$out"
mkdir -p "$out" "$stage/usr/lib/system"

"$DEBUGFS" -R "dump /usr/lib/libSystem.B.dylib $out/libSystem.B.dylib" "$fs" >/dev/null 2>&1
[ -s "$out/libSystem.B.dylib" ] || { echo "libSystem.B.dylib nicht aus dem Image geholt" >&2; exit 1; }

target=x86_64-apple-macos11.0
[ "$CLOKE_ARCH" = arm64 ] && target=arm64-apple-macos11.0
inst=/usr/lib/system/libclokeos_compat.dylib
# -nostdlib, gegen die PureDarwin-libSystem: system.c ruft chmod, lgamma, setreuid usw. auf
clang -target "$target" -dynamiclib -nostdlib -O2 -fno-stack-check -D_FORTIFY_SOURCE=0 -std=gnu11 -Wall \
  -Wno-unused-function -Wl,-no_fixup_chains -Wl,-platform_version,macos,11.0,11.5 \
  -install_name /usr/lib/libclokeos_compat.dylib -o "$out/libclokeos_compat.dylib" \
  "$HERE/libcompat/compat.c" "$HERE/libcompat/chkstk.S" "$HERE/libcompat/system.c" "$out/libSystem.B.dylib"
# Der Linker des Macs (x86_64) sucht bei einem Install-Namen unter /usr/lib/system die Teilbibliotheken von
# libSystem ("library 'system_pthread' not found"); deshalb erst ein anderer Name, dann umsetzen
install_name_tool -id "$inst" "$out/libclokeos_compat.dylib"
python3 -I "$HERE/libcompat/reexport.py" "$out/libSystem.B.dylib" "$inst"
codesign --force -s - "$out/libSystem.B.dylib" "$out/libclokeos_compat.dylib" 2>/dev/null || true

# libffi: PureDarwin hat nur libffi.a; Rubys fiddle (Homebrew) lädt /usr/lib/libffi.dylib
"$DEBUGFS" -R "dump /lib/libffi.a $out/libffi.a" "$fs" >/dev/null 2>&1
[ -s "$out/libffi.a" ] || { echo "libffi.a nicht aus dem Image geholt" >&2; exit 1; }
clang -target "$target" -dynamiclib -nostdlib -Wl,-no_fixup_chains -Wl,-platform_version,macos,11.0,11.5 \
  -compatibility_version 27.0.0 -current_version 27.0.0 -install_name /usr/lib/libffi.dylib \
  -Wl,-force_load,"$out/libffi.a" -o "$out/libffi.dylib" "$out/libSystem.B.dylib"
codesign --force -s - "$out/libffi.dylib" 2>/dev/null || true
cp "$out/libffi.dylib" "$stage/usr/lib/libffi.dylib"
cp "$out/libclokeos_compat.dylib" "$stage$inst"
cp "$out/libSystem.B.dylib" "$stage/usr/lib/libSystem.B.dylib"
chmod 755 "$stage$inst" "$stage/usr/lib/libSystem.B.dylib"
echo "Fertig: libclokeos_compat für $CLOKE_ARCH"
