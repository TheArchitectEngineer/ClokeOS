#!/bin/bash
# Wine auf ClokeOS (x86_64): PureDarwins Wine hat mountmgr.so und winebus.so, die gegen
# DiskArbitration, SystemConfiguration, CoreServices, Security und IOKit gebunden sind. Diese
# Frameworks haben in PureDarwin nicht alle Symbole (IOHIDDevice*, SecKeychain*, MDQuery* ...),
# dlopen scheitert mit "image not loaded", Wine meldet c0000135 und startet weder MountMgr noch winebus.
# Hier: eine kleine Bibliothek mit genau diesen Symbolen als Leerfunktionen (keine Laufwerks-
# Benachrichtigungen, kein Schlüsselbund, keine Gamepads) und Kopien der beiden .so, deren Ladebefehle
# auf sie zeigen statt auf die Frameworks. Die Bibliothek re-exportiert CoreFoundation (zwei Symbole fehlen auch dort).
#
#   wine/build.sh <stage>
set -euo pipefail
source "$(dirname "$0")/../config.sh"
[ "$CLOKE_ARCH" = x86_64 ] || exit 0
need "$DEBUGFS" clang nm codesign

stage="$1"
read -r root_start _ < <(gpt_part "$PD_IMG" "$GUID_LINUX_FS")
fs="$PD_IMG?offset=$((root_start * 512))"
out="$WORK/tmp/wine-$CLOKE_ARCH"
rm -rf "$out"
mkdir -p "$out" "$stage/usr/lib/wine/x86_64-unix"
unix=/usr/lib/wine/x86_64-unix
inst=/usr/lib/libwinemacstubs.dylib

for n in mountmgr winebus; do
  "$DEBUGFS" -R "dump $unix/$n.so $out/$n.so" "$fs" >/dev/null 2>&1
  [ -s "$out/$n.so" ] || { echo "$n.so nicht aus dem Image geholt (Wine fehlt?)" >&2; exit 1; }
done

# Alle Symbole, die die beiden Bibliotheken aus den Frameworks (außer CoreFoundation) holen
for n in mountmgr winebus; do
  nm -u -m "$out/$n.so" | sed -nE 's/.* external (_[A-Za-z0-9_]*) \(from (DiskArbitration|SystemConfiguration|CoreServices|Security|IOKit)\)/\1/p'
done | sort -u > "$out/symbols"
# Zwei Symbole fehlen auch in PureDarwins CoreFoundation; die Ersatzbibliothek re-exportiert CoreFoundation
printf '%s\n' _CFURLCopyResourcePropertyForKey _kCFURLVolumeIsBrowsableKey >> "$out/symbols"
[ -s "$out/symbols" ] || { echo "keine Symbole gefunden" >&2; exit 1; }

{
  echo '/* erzeugt von wine/build.sh */'
  while read -r s; do
    s=${s#_}
    case "$s" in
      k*) echo "const void *$s;" ;;                                  # Konstanten: NULL
      SecKeychain*) echo "int $s(void) { return -25291; }" ;;        # errSecNotAvailable
      *) echo "void *$s(void) { return 0; }" ;;                      # NULL / 0 / kein Ergebnis
    esac
  done < "$out/symbols"
} > "$out/stubs.c"
clang -target x86_64-apple-macos11.0 -dynamiclib -O1 -Wl,-no_fixup_chains -Wl,-headerpad,0x400 \
  -Wl,-platform_version,macos,11.0,11.5 -compatibility_version 1000.0.0 -current_version 1000.0.0 \
  -install_name /tmp/libwinemacstubs.dylib -o "$out/libwinemacstubs.dylib" "$out/stubs.c"
install_name_tool -id "$inst" "$out/libwinemacstubs.dylib"
python3 -I "$HERE/libcompat/reexport.py" "$out/libwinemacstubs.dylib" /System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation

for n in mountmgr winebus; do
  python3 -I "$HERE/wine/repoint.py" "$out/$n.so" "$inst"
done
codesign --force -s - "$out"/*.so "$out/libwinemacstubs.dylib" 2>/dev/null || true
cp "$out/libwinemacstubs.dylib" "$stage$inst"
cp "$out/mountmgr.so" "$out/winebus.so" "$stage$unix/"
chmod 755 "$stage$inst" "$stage$unix/mountmgr.so" "$stage$unix/winebus.so"
echo "Fertig: Wine-Stubs ($(wc -l < "$out/symbols" | tr -d ' ') Symbole)"
