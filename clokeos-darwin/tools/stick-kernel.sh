#!/bin/bash
# Tauscht in einem fertigen USB-Abbild (offline) den Kernel aus: auf dem Stick selbst und im Abbild der
# EFI-Partition, das der Installer auf die SSD schreibt (ClokeOS/Install/clokeos-<arch>-esp.img.xz, samt
# Prüfsumme in clokeos-<arch>.txt). Für Tests mit einem neuen Kernel, ohne den ganzen Release neu zu bauen.
# Die große ramdisk.img (alte Kopie der Abbilder) fliegt raus; der Stick startet dann die schlanke.
#
#   tools/stick-kernel.sh <stick.img> [kernel]      Standard: ~/ClokeOS-work/out/kernel (und kernel-up daneben)
set -euo pipefail
source "$(dirname "$0")/../config.sh"
need xz shasum mcopy mdel mtype mdir
export MTOOLS_SKIP_CHECK=1
img="${1:?Stick-Abbild angeben}"
kernel="${2:-$WORK/out/kernel}"
up="$(dirname "$kernel")/kernel-up"
[ -f "$img" ] && [ -f "$kernel" ] || { echo "Abbild oder Kernel fehlt" >&2; exit 1; }
I="$img@@1048576"
tmp="$WORK/tmp/stick-kernel"
rm -rf "$tmp"
mkdir -p "$tmp"
# erst löschen, dann schreiben: in der EFI-Partition ist wenig frei
mdel -i "$I" ::/EFI/BOOT/kernel-smp 2>/dev/null || true
mdel -i "$I" ::/ramdisk.img 2>/dev/null || true
mcopy -o -i "$I" "$kernel" ::/EFI/BOOT/kernel
[ -f "$up" ] && mcopy -o -i "$I" "$up" ::/EFI/BOOT/kernel-up
arch=x86_64
mcopy -n -i "$I" "::/ClokeOS/Install/clokeos-$arch-esp.img.xz" "$tmp/esp.img.xz"
xz -dc "$tmp/esp.img.xz" > "$tmp/esp.img"
mdel -i "$tmp/esp.img@@0" ::/EFI/BOOT/kernel-smp 2>/dev/null || true
mcopy -o -i "$tmp/esp.img@@0" "$kernel" ::/EFI/BOOT/kernel
[ -f "$up" ] && mcopy -o -i "$tmp/esp.img@@0" "$up" ::/EFI/BOOT/kernel-up
size=$(stat -f %z "$tmp/esp.img")
xz -T0 -6 -c "$tmp/esp.img" > "$tmp/new.xz"
packed=$(stat -f %z "$tmp/new.xz")
sum=$(shasum -a 256 "$tmp/new.xz" | cut -d' ' -f1)
mcopy -o -i "$I" "$tmp/new.xz" "::/ClokeOS/Install/clokeos-$arch-esp.img.xz"
mtype -i "$I" "::/ClokeOS/Install/clokeos-$arch.txt" |
  sed "s#^esp=.*#esp=clokeos-$arch-esp.img.xz $size $packed $sum#" > "$tmp/man.txt"
mcopy -o -i "$I" "$tmp/man.txt" "::/ClokeOS/Install/clokeos-$arch.txt"
echo "OK: $img trägt $(basename "$kernel") auf dem Stick und im Installationsabbild ($packed Bytes gepackt, $sum)"
