#!/bin/bash
# Baut e2fsck, resize2fs, mke2fs und tune2fs (e2fsprogs, GPL-2) statisch für Darwin 20 (macOS 11), damit die
# Wiederherstellung das System-Dateisystem nach dem Schreiben auf die ganze Platte vergrößern kann
# (clokeos-installer, resize2fs) und Dateisysteme prüfen kann.
#
#   recovery/e2fsprogs.sh <x86_64|arm64>      Ergebnis: ~/ClokeOS-work/out/e2fsprogs-<arch>/{e2fsck,resize2fs,mke2fs,tune2fs}
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
arch="${1:?x86_64 oder arm64}"
source "$here/config.sh"
tgz="$WORK/download/e2fsprogs/e2fsprogs-1.47.4.tar.xz"
sha=fd5bf388cbdbe006a3d3b318d983b2948382440acc85a87f1e7d108653e8db0b
[ -f "$tgz" ] || { echo "Fehlt: $tgz (https://mirrors.edge.kernel.org/pub/linux/kernel/people/tytso/e2fsprogs/v1.47.4/)" >&2; exit 1; }
[ "$(shasum -a 256 "$tgz" | cut -d' ' -f1)" = "$sha" ] || { echo "Prüfsumme von $tgz stimmt nicht" >&2; exit 1; }
out="$WORK/out/e2fsprogs-$arch"
b="$WORK/tmp/e2fsprogs-$arch"
rm -rf "$b" "$out"
mkdir -p "$b" "$out"
tar -xJf "$tgz" -C "$b"
cd "$b/e2fsprogs-1.47.4"
# PureDarwins libSystem hat kein sbrk; e2fsprogs fragt es nur für die Speicherstatistik (-t) ab
sed -i '' 's/sbrk(0)/((void *)0)/g' e2fsck/iscan.c e2fsck/util.c resize/resource_track.c
# Rohgeräte von Darwin lehnen die 2-Byte-Schreibvorgänge ab, mit denen libext2fs den Superblock aktualisiert
# ("alignment error"); ohne write_byte schreibt es den ganzen Block
sed -i '' 's/^\t.write_byte\t= unix_write_byte,/\t.write_byte\t= NULL,/' lib/ext2fs/unix_io.c
grep -q 'write_byte\t= NULL' lib/ext2fs/unix_io.c
export CC="clang -arch $arch -mmacosx-version-min=11.0 -O2 -fno-stack-check"
host=x86_64-apple-darwin
[ "$arch" = arm64 ] && host=aarch64-apple-darwin
ac_cv_func_sbrk=no ac_cv_func_chflags=no ./configure --host="$host" --build="$(uname -m)-apple-darwin" --disable-nls --disable-fuse2fs --disable-uuidd \
  --disable-defrag --disable-debugfs --disable-imager --disable-e2initrd-helper \
  --disable-testio-debug >"$b/configure.log" 2>&1 || { tail -20 "$b/configure.log" >&2; exit 1; }
make -j8 libs >"$b/make.log" 2>&1 && make -j8 -C e2fsck e2fsck >>"$b/make.log" 2>&1 &&
  make -j8 -C resize resize2fs >>"$b/make.log" 2>&1 && make -j8 -C misc mke2fs tune2fs >>"$b/make.log" 2>&1 ||
  { tail -30 "$b/make.log" >&2; exit 1; }
cp e2fsck/e2fsck resize/resize2fs misc/mke2fs misc/tune2fs "$out/"
strip "$out"/* 2>/dev/null || true
file "$out"/resize2fs
