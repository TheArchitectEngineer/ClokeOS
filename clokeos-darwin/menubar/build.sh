#!/bin/bash
# Baut die globale Menüleiste der Desktop-Variante auf dem Mac für PureDarwin:
#
#   menubar/build.sh <stage>     Architektur wie überall mit CLOKEOS_ARCH (Standard: x86_64)
#
#   /lib/gtk-3.0/modules/libappmenu-gtk-module.so
#       appmenu-gtk-module aus vala-panel-appmenu (LGPL-3): blendet die Menüleiste in GTK-Fenstern
#       aus und gibt sie über D-Bus heraus. appmenu-clokeos.patch setzt die Fenster-Eigenschaften
#       über GDK statt Xlib (PureDarwins arm64-GTK hat Xlib nur statisch eingebaut) und liest
#       Tastenkürzel auch aus GtkAccelLabel (so zeigen sie Xfce-Programme wie Thunar an).
#   /System/Library/CoreServices/CloakServer/cloakmenu
#       holt die Menüs als der angemeldete Benutzer vom Sitzungsbus und gibt sie an CloakServer
#   /share/glib-2.0/schemas: das Schema des Moduls, mit den Schemas des Images neu übersetzt
#       (ohne Schema bricht g_settings_new im Modul jedes GTK-Programm ab)
#
# Header und Bibliotheken kommen wie bei CloakServer aus dem Desktop-Image (cloakserver/build.sh
# legt sie nach ~/ClokeOS-work/sysroot/).
set -euo pipefail
source "$(dirname "$0")/../config.sh"
need "$DEBUGFS" clang patch glib-compile-schemas

stage="$1"
sysroot="$WORK/sysroot/$CLOKE_ARCH-${PD_SHA256:0:12}"
[ -f "$sysroot/.fertig" ] || { echo "Fehlt: $sysroot – zuerst cloakserver/build.sh" >&2; exit 1; }
[ -f "$APPMENU_TGZ" ] || { echo "Fehlt: $APPMENU_TGZ – zuerst CLOKEOS_FLAVOR=desktop ./fetch.sh" >&2; exit 1; }
read -r root_start _ < <(gpt_part "$PD_IMG" "$GUID_LINUX_FS")
fs="$PD_IMG?offset=$((root_start * 512))"

for spec in gtk-3:/lib/libgtk-3.0.dylib gdk-3:/lib/libgdk-3.0.dylib gio-2.0:/lib/libgio-2.0.0.dylib \
            gobject-2.0:/lib/libgobject-2.0.0.dylib glib-2.0:/lib/libglib-2.0.0.dylib \
            gdk_pixbuf-2.0:/lib/libgdk_pixbuf-2.0.0.dylib; do
  f="$sysroot/libs/lib${spec%%:*}.dylib"
  [ -s "$f" ] || "$DEBUGFS" -R "dump ${spec#*:} $f" "$fs" >/dev/null 2>&1 || true
  [ -s "$f" ] || { echo "Nicht aus dem Image geholt: ${spec#*:}" >&2; exit 1; }
done

b="$WORK/tmp/menubar-$CLOKE_ARCH"
rm -rf "$b"
mkdir -p "$b"
tar -xzf "$APPMENU_TGZ" -C "$b"
m="$b/vala-panel-appmenu-$APPMENU_VERSION/subprojects/appmenu-gtk-module"
patch -s -d "$m" -p1 < "$HERE/menubar/appmenu-clokeos.patch"

target=x86_64-apple-macos11.0
[ "$CLOKE_ARCH" = arm64 ] && target=arm64-apple-macos11.0
inc=(-I"$sysroot/include" -I"$sysroot/include/gtk-3.0" -I"$sysroot/include/glib-2.0" -I"$sysroot/lib/glib-2.0/include"
     -I"$sysroot/include/gio-unix-2.0" -I"$sysroot/include/pango-1.0" -I"$sysroot/include/harfbuzz"
     -I"$sysroot/include/cairo" -I"$sysroot/include/gdk-pixbuf-2.0" -I"$sysroot/include/atk-1.0"
     -I"$sysroot/include/freetype2" -I"$sysroot/include/fribidi")
# wie bei CloakServer: PLATFORM_MacOSX für PureDarwins Libc-Header, kein ___chkstk_darwin, keine
# chained fixups
cc=(clang -target "$target" -isysroot "$sysroot" -DPLATFORM_MacOSX=1 -fno-stack-check -std=gnu11 -O2
    -nostdlib -L"$sysroot/libs" -Wl,-platform_version,macos,11.0,11.5 -Wl,-no_fixup_chains)

mkdir -p "$stage/lib/gtk-3.0/modules" "$stage/System/Library/CoreServices/CloakServer"
# Fremdcode: Warnungen aus, wie sie upstream gebaut wird (warning_level=1); ulong kennt nur glibc
"${cc[@]}" -w -bundle -DG_LOG_DOMAIN=\"appmenu-gtk-module\" -Dulong="unsigned long" "${inc[@]}" -I"$m/lib" -I"$m/src" \
  -o "$stage/lib/gtk-3.0/modules/libappmenu-gtk-module.so" "$m"/lib/*.c "$m"/src/*.c \
  -lgtk-3 -lgdk-3 -lgdk_pixbuf-2.0 -lgio-2.0 -lgobject-2.0 -lglib-2.0 -lSystem
"${cc[@]}" -Wall -Wextra -Wno-unused-parameter "${inc[@]}" \
  -o "$stage/System/Library/CoreServices/CloakServer/cloakmenu" "$HERE/menubar/cloakmenu.c" \
  -lgio-2.0 -lgobject-2.0 -lglib-2.0 -lSystem

# GSettings: alle Schemas des Images plus das des Moduls in eine gschemas.compiled
"$DEBUGFS" -R "rdump /share/glib-2.0/schemas $b" "$fs" >/dev/null 2>&1 || true
[ -s "$b/schemas/gschemas.compiled" ] || { echo "Keine GSettings-Schemas im Image gefunden" >&2; exit 1; }
cp "$m/data/org.appmenu.gtk-module.gschema.xml" "$b/schemas/"
glib-compile-schemas "$b/schemas"
mkdir -p "$stage/share/glib-2.0/schemas"
cp "$b/schemas/org.appmenu.gtk-module.gschema.xml" "$b/schemas/gschemas.compiled" "$stage/share/glib-2.0/schemas/"
chmod 644 "$stage/share/glib-2.0/schemas/"*
echo "Fertig: Menüleiste für $CLOKE_ARCH (appmenu-gtk-module $APPMENU_VERSION, cloakmenu)"
