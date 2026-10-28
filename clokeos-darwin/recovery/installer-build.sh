#!/bin/bash
# Baut clokeos-installer (recovery/installer.c) auf dem Mac für PureDarwin und legt ihn als
# /usr/libexec/clokeos-installer in <stage>. Die Desktop-Variante bekommt ihn immer: Die
# Wiederherstellung installiert damit, das System stellt damit "Neustart in Wiederherstellung" ein.
#
#   recovery/installer-build.sh <stage>     Architektur wie überall mit CLOKEOS_ARCH
#
# Header und Bibliotheken aus dem Desktop-Image wie bei CloakServer (Sysroot von cloakserver/build.sh);
# libcurl wird dynamisch gelinkt, liblzma gibt es im Image nur als statisches Archiv.
set -euo pipefail
source "$(dirname "$0")/../config.sh"
need "$DEBUGFS" clang

stage="$1"
sysroot="$WORK/sysroot/$CLOKE_ARCH-${PD_SHA256:0:12}"
[ -f "$sysroot/.fertig" ] || { echo "Fehlt: $sysroot – zuerst cloakserver/build.sh" >&2; exit 1; }
read -r root_start _ < <(gpt_part "$PD_IMG" "$GUID_LINUX_FS")
fs="$PD_IMG?offset=$((root_start * 512))"
for spec in curl:/usr/lib/libcurl.4.dylib lzma.a:/lib/liblzma.a; do
  name="${spec%%:*}"
  case "$name" in *.a) f="$sysroot/libs/lib$name" ;; *) f="$sysroot/libs/lib$name.dylib" ;; esac
  [ -s "$f" ] || "$DEBUGFS" -R "dump ${spec#*:} $f" "$fs" >/dev/null 2>&1 || true
  [ -s "$f" ] || { echo "Nicht aus dem Image geholt: ${spec#*:}" >&2; exit 1; }
done

target=x86_64-apple-macos11.0
[ "$CLOKE_ARCH" = arm64 ] && target=arm64-apple-macos11.0
mkdir -p "$stage/usr/libexec"
# RELEASES_REPO: woher der USB-Stick die Wiederherstellung derselben Version nachlädt
clang -target "$target" -isysroot "$sysroot" -DPLATFORM_MacOSX=1 -DRELEASES_REPO="\"$CLOKE_RELEASES_REPO\"" \
  -fno-stack-check -std=gnu11 -O2 -Wall -Wextra \
  -Wno-nullability-completeness -I"$sysroot/include" -nostdlib -L"$sysroot/libs" \
  -Wl,-platform_version,macos,11.0,11.5 -Wl,-no_fixup_chains -Wl,-dead_strip \
  -o "$stage/usr/libexec/clokeos-installer" "$HERE/recovery/installer.c" -lcurl "$sysroot/libs/liblzma.a" -lSystem
echo "Fertig: clokeos-installer für $CLOKE_ARCH"

# e2fsprogs (e2fsck, resize2fs, mke2fs, tune2fs): der Installer vergrößert damit das Dateisystem des Systems
e2="$WORK/out/e2fsprogs-$CLOKE_ARCH"
[ -x "$e2/resize2fs" ] || "$HERE/recovery/e2fsprogs.sh" "$CLOKE_ARCH" >/dev/null
mkdir -p "$stage/usr/sbin"
cp "$e2/e2fsck" "$e2/resize2fs" "$e2/mke2fs" "$e2/tune2fs" "$stage/usr/sbin/"
