#!/bin/bash
# Baut libclokeos_carbon (macapps/carbon.c) und eine leere Carbon.framework-Hülle, trägt die Bibliothek
# in PureDarwins CoreServices ein und legt alles in <stage>. Logic Pros OMF.framework lädt damit.
#
#   macapps/carbon.sh <stage>     Architektur wie überall mit CLOKEOS_ARCH
set -euo pipefail
source "$(dirname "$0")/../config.sh"
need "$DEBUGFS" clang codesign
stage="$1"
read -r root_start _ < <(gpt_part "$PD_IMG" "$GUID_LINUX_FS")
fs="$PD_IMG?offset=$((root_start * 512))"
out="$WORK/tmp/carbon-$CLOKE_ARCH"
rm -rf "$out"; mkdir -p "$out"
fw=System/Library/Frameworks
"$DEBUGFS" -R "dump /$fw/CoreServices.framework/Versions/A/CoreServices $out/CoreServices" "$fs" >/dev/null 2>&1
[ -s "$out/CoreServices" ] || { echo "CoreServices nicht aus dem Image geholt" >&2; exit 1; }
"$DEBUGFS" -R "dump /usr/lib/libSystem.B.dylib $out/libSystem.B.dylib" "$fs" >/dev/null 2>&1

target=x86_64-apple-macos11.0
[ "$CLOKE_ARCH" = arm64 ] && target=arm64-apple-macos11.0
flags=(-target "$target" -dynamiclib -nostdlib -O2 -Wl,-no_fixup_chains -Wl,-platform_version,macos,11.0,11.5)
clang "${flags[@]}" -install_name /usr/lib/libclokeos_carbon.dylib -o "$out/libclokeos_carbon.dylib" \
  "$HERE/macapps/carbon.c" "$out/libSystem.B.dylib"
# Carbon: leere Hülle mit Apples Versionsnummern (OMF verlangt Kompatibilität 2.0.0), die Carbon-Teile re-exportiert
echo 'static int carbon_stub;' > "$out/stub.c"
clang "${flags[@]}" -Wl,-headerpad,0x200 -compatibility_version 2.0.0 -current_version 170.0.0 \
  -install_name /System/Library/Frameworks/Carbon.framework/Versions/A/Carbon -o "$out/Carbon" "$out/stub.c" "$out/libSystem.B.dylib"
python3 -I "$HERE/libcompat/reexport.py" "$out/CoreServices" /usr/lib/libclokeos_carbon.dylib
python3 -I "$HERE/libcompat/reexport.py" "$out/Carbon" /System/Library/Frameworks/CoreServices.framework/Versions/A/CoreServices
codesign --force -s - "$out/libclokeos_carbon.dylib" "$out/CoreServices" "$out/Carbon" 2>/dev/null || true

mkdir -p "$stage/usr/lib" "$stage/$fw/CoreServices.framework/Versions/A" "$stage/$fw/Carbon.framework/Versions/A"
cp "$out/libclokeos_carbon.dylib" "$stage/usr/lib/"
cp "$out/CoreServices" "$stage/$fw/CoreServices.framework/Versions/A/CoreServices"
cp "$out/Carbon" "$stage/$fw/Carbon.framework/Versions/A/Carbon"
ln -sfn A "$stage/$fw/Carbon.framework/Versions/Current"
ln -sfn Versions/Current/Carbon "$stage/$fw/Carbon.framework/Carbon"
chmod 755 "$stage/usr/lib/libclokeos_carbon.dylib" "$stage/$fw"/*/Versions/A/*
echo "Fertig: libclokeos_carbon und Carbon für $CLOKE_ARCH"
