#!/bin/bash
# Legt den Übersetzer (ucrun), den exec-Haken (libxlat-exec.dylib), die x86_64-Systembibliotheken und die Dateizuordnung für
# x86_64-Mach-O-Dateien in das Stage-Verzeichnis von brand.sh (nur arm64, Desktop).   xlat/image.sh <stage> <extra.cmds>
# Fehlt der Unicorn-Quellbaum ($WORK/download/unicorn/src) oder das x86_64-PureDarwin-Image, wird nichts eingebaut.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
source "$here/config.sh"
stage="$1"; cmds="$2"
if [ "$CLOKE_ARCH" = x86_64 ]; then  # Gegenrichtung: arm64-Programme auf dem x86_64-Desktop (a64run)
  a64img="$(dirname "$PD_IMG")/puredarwin-arm64-virt.img"
  if [ ! -d "$WORK/download/unicorn/src" ] || [ ! -f "$a64img" ]; then
    echo "xlat: arm64-Gegenrichtung übersprungen (Unicorn-Quellen oder arm64-PureDarwin-Image fehlen, siehe docs/translation-study.md)" >&2
    exit 0
  fi
  out="$WORK/out/xlat-a64"
  "$here/xlat/build-a64.sh" "$out" >&2
  "$here/xlat/xroot-a64.sh" "$stage/Library/ClokeOS/arm64" libs >&2
  mkdir -p "$stage/usr/bin" "$stage/usr/lib" "$stage/share/applications" "$stage/share/mime"
  cp "$out/a64run" "$here/xlat/a64-open" "$stage/usr/bin/"
  cp "$out/libxlat-exec.dylib" "$stage/usr/lib/"
  chmod 755 "$stage/usr/bin/a64run" "$stage/usr/bin/a64-open" "$stage/usr/lib/libxlat-exec.dylib"
  cp "$here/xlat/a64.desktop" "$stage/share/applications/"
  cat >> "$stage/etc/profile" <<'EOF2'
if [ -x /usr/bin/a64run ] && [ -f /usr/lib/libxlat-exec.dylib ]; then
  export A64_ROOT=/Library/ClokeOS/arm64
  case ":$DYLD_INSERT_LIBRARIES:" in *:/usr/lib/libxlat-exec.dylib:*) ;; *) export DYLD_INSERT_LIBRARIES="${DYLD_INSERT_LIBRARIES:+$DYLD_INSERT_LIBRARIES:}/usr/lib/libxlat-exec.dylib" ;; esac
fi
EOF2
  cat > "$stage/etc/clokeos-xlat.zsh" <<'EOF3'
if [[ -o interactive && -z $XLAT_HOOKED && -x /usr/bin/a64run && -f /usr/lib/libxlat-exec.dylib ]]; then
  export XLAT_HOOKED=1 A64_ROOT=/Library/ClokeOS/arm64
  case ":$DYLD_INSERT_LIBRARIES:" in *:/usr/lib/libxlat-exec.dylib:*) ;; *) export DYLD_INSERT_LIBRARIES="${DYLD_INSERT_LIBRARIES:+$DYLD_INSERT_LIBRARIES:}/usr/lib/libxlat-exec.dylib" ;; esac
  exec zsh
fi
EOF3
  # arm64-Mach-O (cf fa ed fe, CPU-Typ 0c 00 00 01): eigener Typ, Fundus startet sie nicht selbst
  m="$stage/share/mime/magic"
  [ -f "$m" ] || printf 'MIME-Magic\0\n' > "$m"
  printf '[50:application/x-arm64-macho]\n>0=\0\010\317\372\355\376\014\000\000\001\n' >> "$m"
  for f in "$stage/share/applications/mimeinfo.cache:[MIME Cache]" "$stage/share/applications/mimeapps.list:[Default Applications]"; do
    [ -f "${f%%:*}" ] || printf '%s\n' "${f#*:}" > "${f%%:*}"
    echo 'application/x-arm64-macho=a64.desktop;' >> "${f%%:*}"
  done
  exit 0
fi
[ "$CLOKE_ARCH" = arm64 ] || exit 0
x86img="$(dirname "$PD_IMG")/puredarwin-minimal.img"
if [ ! -d "$WORK/download/unicorn/src" ] || [ ! -f "$x86img" ]; then
  echo "xlat: übersprungen (Unicorn-Quellen oder x86_64-Image fehlen, siehe docs/translation-study.md)" >&2
  exit 0
fi
out="$WORK/out/xlat"
"$here/xlat/build.sh" "$out" >&2
"$here/xlat/xroot.sh" "$stage/Library/ClokeOS/x86" libs >&2
mkdir -p "$stage/usr/bin" "$stage/usr/lib" "$stage/share/applications" "$stage/share/mime"
cp "$out/ucrun" "$here/xlat/xlat-open" "$stage/usr/bin/"
cp "$out/libxlat-exec.dylib" "$stage/usr/lib/"
chmod 755 "$stage/usr/bin/ucrun" "$stage/usr/bin/xlat-open" "$stage/usr/lib/libxlat-exec.dylib"
cp "$here/xlat/xlat.desktop" "$stage/share/applications/"
# Haken: execve/posix_spawn der Shell und der Desktop-Programme starten x86_64-Programme durch ucrun
cat >> "$stage/etc/profile" <<'EOF2'
if [ -x /usr/bin/ucrun ] && [ -f /usr/lib/libxlat-exec.dylib ]; then
  export XLAT_ROOT=/Library/ClokeOS/x86
  case ":$DYLD_INSERT_LIBRARIES:" in *:/usr/lib/libxlat-exec.dylib:*) ;; *) export DYLD_INSERT_LIBRARIES="${DYLD_INSERT_LIBRARIES:+$DYLD_INSERT_LIBRARIES:}/usr/lib/libxlat-exec.dylib" ;; esac
fi
EOF2
# Eine schon laufende Shell kann den Haken nicht nachträglich laden (dyld wertet DYLD_INSERT_LIBRARIES nur beim Start aus):
# interaktive zsh (siehe /etc/zshrc) startet sich einmal neu, danach erben alle Kindprozesse den Haken.
cat > "$stage/etc/clokeos-xlat.zsh" <<'EOF3'
if [[ -o interactive && -z $XLAT_HOOKED && -x /usr/bin/ucrun && -f /usr/lib/libxlat-exec.dylib ]]; then
  export XLAT_HOOKED=1 XLAT_ROOT=/Library/ClokeOS/x86
  case ":$DYLD_INSERT_LIBRARIES:" in *:/usr/lib/libxlat-exec.dylib:*) ;; *) export DYLD_INSERT_LIBRARIES="${DYLD_INSERT_LIBRARIES:+$DYLD_INSERT_LIBRARIES:}/usr/lib/libxlat-exec.dylib" ;; esac
  exec zsh
fi
EOF3
# x86_64-Mach-O erkennt man am Anfang (cf fa ed fe, CPU-Typ 07 00 00 01); eigener Typ, damit Fundus sie nicht selbst starten will
m="$stage/share/mime/magic"
[ -f "$m" ] || printf 'MIME-Magic\0\n' > "$m"
printf '[50:application/x-x86-macho]\n>0=\0\010\317\372\355\376\007\000\000\001\n' >> "$m"
append() { # Datei, Kopfzeile
  [ -f "$1" ] || printf '%s\n' "$2" > "$1"
  echo 'application/x-x86-macho=xlat.desktop;' >> "$1"
}
append "$stage/share/applications/mimeinfo.cache" '[MIME Cache]'
append "$stage/share/applications/mimeapps.list" '[Default Applications]'
