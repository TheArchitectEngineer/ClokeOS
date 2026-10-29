#!/bin/bash
# Baut die MIME-Datenbank des Desktops: PureDarwins glib liest nur noch mime.cache (keine Textdateien globs2/magic mehr),
# und das Image hat keine shared-mime-info. Hier: Standarddatenbank (freedesktop.org.xml) + mime/clokeos.xml mit
# update-mime-database (Homebrew: shared-mime-info, GPL-2.0) zu $stage/share/mime.   mime/build.sh <stage>
# Fehlt das Werkzeug, bleibt es bei den handgeschriebenen Dateien (Dateitypen werden dann nicht erkannt).
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
stage="$1"
umd="$(command -v update-mime-database || true)"
base=""
for p in "$(dirname "$umd")/../share/mime/packages/freedesktop.org.xml" /opt/homebrew/opt/shared-mime-info/share/mime/packages/freedesktop.org.xml /usr/local/opt/shared-mime-info/share/mime/packages/freedesktop.org.xml /usr/share/mime/packages/freedesktop.org.xml; do
  [ -f "$p" ] && { base="$p"; break; }
done
if [ -z "$umd" ] || [ -z "$base" ]; then
  echo "mime: übersprungen (update-mime-database/shared-mime-info fehlt: brew install shared-mime-info)" >&2
  exit 0
fi
t="$(mktemp -d)"
trap 'rm -rf "$t"' EXIT
mkdir -p "$t/packages"
cp "$base" "$here/mime/clokeos.xml" "$t/packages/"
"$umd" "$t" >/dev/null
rm -rf "$stage/share/mime"
mkdir -p "$stage/share/mime"
# die Textdateien braucht der Leser nicht; Cache, Typenliste und Symbole für Gnome-Symbolthemen reichen
cp "$t/mime.cache" "$t/version" "$stage/share/mime/"
cp "$t/globs2" "$t/magic" "$t/subclasses" "$t/aliases" "$t/types" "$stage/share/mime/" 2>/dev/null || true
echo "mime: $(du -sk "$stage/share/mime" | cut -f1) KB" >&2

# libclokeos-mime.dylib: die glib des Images sniffed den Inhalt nicht mehr (siehe mime/sniff.c); der Haken liest ihn nach.
# Header und Bibliotheken wie bei menubar/build.sh aus dem Sysroot des Desktop-Images (cloakserver/build.sh legt es an).
source "$here/config.sh"
sysroot="$WORK/sysroot/$CLOKE_ARCH-${PD_SHA256:0:12}"
if [ -f "$sysroot/.fertig" ] && [ -s "$sysroot/libs/libgio-2.0.dylib" ]; then
  target=x86_64-apple-macos11.0
  [ "$CLOKE_ARCH" = arm64 ] && target=arm64-apple-macos11.0
  mkdir -p "$stage/usr/lib"
  clang -target "$target" -isysroot "$sysroot" -DPLATFORM_MacOSX=1 -fno-stack-check -std=gnu11 -O2 -Wall -nostdlib -dynamiclib \
    -I"$sysroot/include/glib-2.0" -I"$sysroot/lib/glib-2.0/include" -I"$sysroot/include/gio-unix-2.0" -L"$sysroot/libs" \
    -Wl,-platform_version,macos,11.0,11.5 -Wl,-no_fixup_chains -Wl,-not_for_dyld_shared_cache -install_name /usr/lib/libclokeos-mime.dylib \
    -o "$stage/usr/lib/libclokeos-mime.dylib" "$here/mime/sniff.c" -lgio-2.0 -lgobject-2.0 -lglib-2.0 -lSystem
  # in die Sitzung laden (der Haken muss beim Programmstart da sein; /etc/profile wird auch von cloakserver-start gelesen)
  cat >> "$stage/etc/profile" <<'EOF2'
if [ -f /usr/lib/libclokeos-mime.dylib ]; then
  case ":$DYLD_INSERT_LIBRARIES:" in *:/usr/lib/libclokeos-mime.dylib:*) ;; *) export DYLD_INSERT_LIBRARIES="${DYLD_INSERT_LIBRARIES:+$DYLD_INSERT_LIBRARIES:}/usr/lib/libclokeos-mime.dylib" ;; esac
fi
EOF2
else
  echo "mime: Inhaltserkennung übersprungen (Sysroot $sysroot fehlt, zuerst cloakserver/build.sh)" >&2
fi
