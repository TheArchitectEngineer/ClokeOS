#!/bin/bash
# Fertiges Festplattenabbild (x86_64): ClokeOS so, wie es der Installer auf eine SSD schreibt, mit
# EFI-Partition, System und Wiederherstellung. Der Weg ohne USB-Installer und ohne Internet, etwa für
# Rechner, auf denen der USB-Stick nicht startet: die SSD per USB-Adapter an einen Mac oder PC hängen
# und das Abbild darauf schreiben. Beim ersten Start läuft der Einrichtungsassistent.
#
#   recovery/disk.sh
#
# Ergebnis: ~/ClokeOS-work/out/recovery-<arch>/disk.img (vorher recovery/build.sh). Auf die SSD:
#   sudo dd if=disk.img of=/dev/rdiskN bs=4m    (macOS; oder balenaEtcher)
# Die Partitionen sind so groß wie ihre Abbilder, der Rest der SSD bleibt frei; die Sicherungs-GPT
# liegt am Ende des Abbilds, nicht am Ende der SSD (das stört weder UEFI noch PureDarwin).
set -euo pipefail
source "$(dirname "$0")/../config.sh"
need python3 mcopy mtype uuidgen
export MTOOLS_SKIP_CHECK=1

out="$WORK/out/recovery-$CLOKE_ARCH"
tmp="$WORK/tmp/disk-$CLOKE_ARCH"
for f in esp.img system.ext4 recovery.ext4; do
  [ -f "$out/$f" ] || { echo "Fehlt: $out/$f – zuerst recovery/build.sh" >&2; exit 1; }
done
"$DEBUGFS" -R "stat /var/db/.RunClokeOSSetup" "$out/system.ext4" 2>/dev/null | grep -q '^Inode' ||
  { echo "system.ext4 ohne /var/db/.RunClokeOSSetup – mit recovery/build.sh neu bauen" >&2; exit 1; }
rm -rf "$tmp"
mkdir -p "$tmp"

# Boot-Argumente wie patch_esp im Installer: boot-uuid= nennt die Kennung der Partition
sys_guid=$(uuidgen | tr a-f A-F)
rec_guid=$(uuidgen | tr a-f A-F)
cp "$out/esp.img" "$tmp/esp.img"
base=$(mtype -i "$tmp/esp.img" ::/EFI/BOOT/boot-args-system.txt | tr -d '\r\n' | sed -E 's/ *boot-uuid=[^ ]*//g; s/ *rd=[^ ]*//g')
printf '%s boot-uuid=%s\n' "$base" "$sys_guid" > "$tmp/boot-args.txt"
printf '%s boot-uuid=%s\n' "$base" "$rec_guid" > "$tmp/boot-args-recovery.txt"
for f in boot-args.txt boot-args-system.txt; do mcopy -o -i "$tmp/esp.img" "$tmp/boot-args.txt" "::/EFI/BOOT/$f"; done
mcopy -o -i "$tmp/esp.img" "$tmp/boot-args-recovery.txt" ::/EFI/BOOT/boot-args-recovery.txt
mtype -i "$tmp/esp.img" ::/EFI/BOOT/boot-args.txt | grep -q "boot-uuid=$sys_guid" ||
  { echo "boot-args.txt nicht gesetzt" >&2; exit 1; }

rm -f "$out/disk.img"
python3 -I "$HERE/recovery/imagetool.py" disk "$out/disk.img" "$tmp/esp.img" "$out/system.ext4" \
  "$out/recovery.ext4" "$sys_guid" "$rec_guid"
rm -rf "$tmp"
echo "Festplattenabbild: $out/disk.img"
