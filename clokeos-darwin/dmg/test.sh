#!/bin/bash
# Prüft clokeos-dmg auf dem Mac gegen echte Images von hdiutil: UDZO, UDBZ, UDRO, UDRW (ohne UDIF-Anhang), Partitionsschemata
# GPT/APM/keins und HFS+-Kompression (LZVN, zlib). Vergleicht den ausgepackten Baum mit dem Quellordner.
#
#   dmg/test.sh
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
t="$(mktemp -d)"
trap 'chmod -R u+w "$t" 2>/dev/null; rm -rf "$t"' EXIT
cc -Wall -Wextra -O2 -o "$t/dmg" "$here/dmg.c"
mkdir -p "$t/src/sub/deep" "$t/vols"
echo hello > "$t/src/hello.txt"
head -c 3000000 /dev/urandom > "$t/src/random.bin"
(yes "compressible text line" || true) | head -c 5000000 > "$t/src/text.txt" || true
ln -s hello.txt "$t/src/link"
echo x > "$t/src/sub/deep/Datei mit Leerzeichen.txt"
cp /bin/ls "$t/src/ls-exe"
for i in $(seq 1 300); do echo "$i" > "$t/src/sub/f$i.txt"; done
export CLOKE_VOLUMES="$t/vols" CLOKE_TMP="$t"
check() { # <image> <Volumename>
  "$t/dmg" mount "$1" > "$t/log" 2>&1 || { cat "$t/log"; exit 1; }
  diff -r "$t/src" "$t/vols/$2" -x .clokeos-dmg -x .fseventsd -x .Trashes -x .DS_Store -x '.Spotlight*' > "$t/diff" || { cat "$t/diff"; exit 1; }
  "$t/dmg" eject "$t/vols/$2" > /dev/null
  [ -z "$(ls "$t/vols")" ] || { echo "eject left files"; exit 1; }
  echo "ok: $(basename "$1")"
}
for f in UDZO UDBZ UDRO UDRW; do
  hdiutil create -quiet -srcfolder "$t/src" -fs HFS+ -volname "V $f" -format $f -ov "$t/$f.dmg"
  check "$t/$f.dmg" "V $f"
done
for l in GPTSPUD SPUD NONE; do
  hdiutil create -quiet -srcfolder "$t/src" -fs HFS+ -volname "L $l" -layout $l -format UDBZ -ov "$t/L$l.dmg"
  check "$t/L$l.dmg" "L $l"
done
# HFS+-Kompression (ditto legt LZVN an)
hdiutil create -quiet -size 40m -fs HFS+ -volname Comp -layout NONE "$t/comp.dmg"
mkdir "$t/mp"
hdiutil attach -quiet -nobrowse -mountpoint "$t/mp" "$t/comp.dmg"
ditto --hfsCompression "$t/src" "$t/mp/"
hdiutil detach -quiet "$t/mp"
hdiutil convert -quiet "$t/comp.dmg" -format UDZO -o "$t/compz.dmg" -ov
check "$t/compz.dmg" Comp
echo "alles in Ordnung"
