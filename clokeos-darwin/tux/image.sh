#!/bin/bash
# Legt tux, tux-run, den Linux-Baum /TUX (Alpine minirootfs) und die Dateizuordnung für ELF-Dateien in
# das Stage-Verzeichnis von brand.sh.   tux/image.sh <stage> <extra.cmds>
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
source "$here/config.sh"
stage="$1"; cmds="$2"
[ -f "$ALPINE_TGZ" ] || { echo "Fehlt: $ALPINE_TGZ (fetch.sh)" >&2; exit 1; }
"$here/tux/build.sh" "$CLOKE_ARCH" >/dev/null
mkdir -p "$stage/usr/bin" "$stage/TUX" "$stage/share/applications" "$stage/share/mime"
cp "$WORK/out/tux-$CLOKE_ARCH/tux" "$here/tux/tux-run" "$here/tux/tux-open" "$stage/usr/bin/"
chmod 755 "$stage/usr/bin/tux" "$stage/usr/bin/tux-run" "$stage/usr/bin/tux-open"
cp "$here/tux/tux.desktop" "$stage/share/applications/"
tar -xzf "$ALPINE_TGZ" -C "$stage/TUX"
printf 'set_inode_field /TUX/tmp mode 041777\nset_inode_field /TUX/root mode 040700\n' >> "$cmds"
# ELF-Dateien erkennt man am Anfang; eigener Typ, damit Thunar sie nicht selbst starten will
m="$stage/share/mime/magic"
[ -f "$m" ] || printf 'MIME-Magic\0\n' > "$m"
printf '[50:application/x-linux-elf]\n>0=\0\004\177ELF\n' >> "$m"
append() { # Datei, Kopfzeile
  [ -f "$1" ] || printf '%s\n' "$2" > "$1"
  echo 'application/x-linux-elf=tux.desktop;' >> "$1"
}
append "$stage/share/applications/mimeinfo.cache" '[MIME Cache]'
append "$stage/share/applications/mimeapps.list" '[Default Applications]'
