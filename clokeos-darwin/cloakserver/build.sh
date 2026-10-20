#!/bin/bash
# Baut CloakServer, den Fensterserver von ClokeOS, auf dem Mac für PureDarwin (x86_64).
#
#   cloakserver/build.sh [stage]   ohne stage: nur bauen nach ~/ClokeOS-work/out/CloakServer/
#
# Übersetzt wird mit Apples clang gegen Header und Bibliotheken aus PureDarwins Desktop-Image
# (Xlib, cairo, pango, librsvg). Die holt das Skript mit debugfs direkt aus dem Image, ohne es
# zu booten, nach ~/ClokeOS-work/sysroot/. Das Ergebnis ist ein Mach-O-Programm wie PureDarwins
# eigene (macOS 11, dyld-Info ohne chained fixups). Logo und Icons erzeugt resources.py aus
# clokeos.html. In <stage> landet es als /System/Library/CoreServices/CloakServer/.
set -euo pipefail
source "$(dirname "$0")/../config.sh"
need "$DEBUGFS" clang python3 osascript

src="$HERE/cloakserver"
out="$WORK/out/CloakServer"
sysroot="$WORK/sysroot/x86_64-${PD_SHA256:0:12}"

if [ ! -f "$PD_IMG" ]; then
  echo "Fehlt: $PD_IMG – CloakServer braucht das Desktop-Image (CLOKEOS_FLAVOR=desktop ./fetch.sh)." >&2
  exit 1
fi

# --- Header und Bibliotheken aus dem Image ----------------------------------------------------
read -r root_start _ < <(gpt_part "$PD_IMG" "$GUID_LINUX_FS")
fs="$PD_IMG?offset=$((root_start * 512))"
dfs() { "$DEBUGFS" -R "$1" "$fs" >/dev/null 2>&1 || true; }
if [ ! -f "$sysroot/.fertig" ]; then
  echo "Hole Header aus $(basename "$PD_IMG")"
  rm -rf "$sysroot"
  mkdir -p "$sysroot/usr/lib/system" "$sysroot/lib/glib-2.0" "$sysroot/libs"
  dfs "rdump /usr/include $sysroot/usr"
  dfs "rdump /include $sysroot"
  dfs "rdump /lib/glib-2.0/include $sysroot/lib/glib-2.0"
  dfs "dump /usr/lib/system/libdyld.dylib $sysroot/usr/lib/system/libdyld.dylib"
  for f in "$sysroot/usr/lib/system/libdyld.dylib" "$sysroot/include/cairo/cairo.h" "$sysroot/usr/include/stdio.h" \
           "$sysroot/lib/glib-2.0/include/glibconfig.h"; do
    [ -s "$f" ] || { echo "Nicht aus dem Image geholt: $f" >&2; exit 1; }
  done
  touch "$sysroot/.fertig"
fi
# Zum Linken genügen die Bibliotheken, die CloakServer direkt benutzt, unter ihrem -l-Namen.
# Ihre Installationsnamen (otool -D) landen im Programm. Xcomposite und Xdamage gibt es im Image
# nur als statische Archive.
for spec in System:/usr/lib/libSystem.B.dylib X11:/lib/libX11.6.dylib Xext:/lib/libXext.6.dylib \
            Xcursor:/lib/libXcursor.1.dylib Xfixes:/lib/libXfixes.3.dylib cairo:/lib/libcairo.2.dylib \
            pango-1.0:/lib/libpango-1.0.0.dylib pangocairo-1.0:/lib/libpangocairo-1.0.0.dylib \
            gobject-2.0:/lib/libgobject-2.0.0.dylib glib-2.0:/lib/libglib-2.0.0.dylib rsvg-2:/lib/librsvg-2.2.dylib \
            Xcomposite.a:/lib/libXcomposite.a Xdamage.a:/lib/libXdamage.a; do
  name="${spec%%:*}"
  case "$name" in *.a) f="$sysroot/libs/lib$name" ;; *) f="$sysroot/libs/lib$name.dylib" ;; esac
  [ -s "$f" ] || dfs "dump ${spec#*:} $f"
  [ -s "$f" ] || { echo "Nicht aus dem Image geholt: ${spec#*:}" >&2; exit 1; }
done
[ -s "$sysroot/usr/lib/libSystem.B.dylib" ] || cp "$sysroot/libs/libSystem.dylib" "$sysroot/usr/lib/libSystem.B.dylib"

# --- übersetzen ------------------------------------------------------------------------------
rm -rf "$out"
mkdir -p "$out/Resources"
python3 -I "$src/resources.py" "$HERE/../clokeos.html" "$out/Resources"

inc=(-I"$sysroot/include" -I"$sysroot/include/cairo" -I"$sysroot/include/pango-1.0" -I"$sysroot/include/glib-2.0"
     -I"$sysroot/lib/glib-2.0/include" -I"$sysroot/include/harfbuzz" -I"$sysroot/include/librsvg-2.0"
     -I"$sysroot/include/gdk-pixbuf-2.0" -I"$sysroot/include/freetype2")
# PLATFORM_MacOSX: PureDarwins sys/cdefs.h ist die unbearbeitete Fassung aus Apples Libc und
# wählt Symbolnamen (fopen statt fopen$UNIX2003) erst mit diesem Makro wie für macOS.
clang -target x86_64-apple-macos11.0 -isysroot "$sysroot" -DPLATFORM_MacOSX=1 -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter \
  -Wno-nullability-completeness -Wno-missing-field-initializers "${inc[@]}" \
  -nostdlib -L"$sysroot/libs" -Wl,-platform_version,macos,11.0,11.5 -Wl,-dead_strip \
  -o "$out/CloakServer" "$src/cloakserver.c" "$src/comp.c" "$src/wall.c" \
  -lSystem -lX11 -lXext -lXcursor -lXfixes -lXcomposite -lXdamage -lcairo -lpango-1.0 -lpangocairo-1.0 \
  -lgobject-2.0 -lglib-2.0 -lrsvg-2
echo "Fertig: $out/CloakServer"

if [ $# -ge 1 ]; then
  dest="$1/System/Library/CoreServices/CloakServer"
  mkdir -p "$dest"
  cp -R "$out/." "$dest/"
fi
