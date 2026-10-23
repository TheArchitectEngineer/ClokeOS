#!/bin/bash
# USB-Installationsmedium für echte Rechner (x86_64): ClokeOS vom USB-Stick auf eine SATA- oder
# NVMe-SSD installieren, auch ohne Internet.
#
#   recovery/usb.sh [<ordner mit clokeos-<arch>.txt und den .xz-Abbildern>]
#
# Der Stick bekommt eine FAT32-Partition mit Loader, Kernel, Boot-Argumenten und der Wiederherstellung
# als RAM-Disk, die der Loader (loader/xnu-loader.patch, clokeos_load_ramdisk) in den Arbeitsspeicher
# lädt; der Kernel startet mit rd=md0 daraus. Vom Stick selbst kann er danach nicht zuverlässig lesen:
# PureDarwins einziger Treiber für USB-Massenspeicher (RavynXHCIMassStorageDisk im xHCI-Treiber)
# bricht in QEMU nach kurzer Zeit mit E/A-Fehlern ab, und an USB-2-Controllern (EHCI) fehlt er ganz.
#   \ramdisk.img       Wiederherstellung und, mit dem Ordner, unter /Library/ClokeOS/Install die
#                      EFI-Partition und das System des Releases: installiert ohne Internet
#                      (CloakServer, rec_init); die Wiederherstellungspartition lädt der Installer
#                      nach oder lässt sie weg, sie passt nicht mehr mit hinein
#   \ramdisk-lite.img  nur die Wiederherstellung, für Rechner, in deren Speicher unter 1 GB die große
#                      nicht passt (1 GB RAM): lädt das System aus dem Internet
# XNU erreicht eine RAM-Disk nur unterhalb von gut 900 MB (siehe den Loader), deshalb die zwei Größen.
#
# CLOKEOS_USB_MODE=partition (Versuch) legt die Wiederherstellung stattdessen als ext4-Partition auf
# den Stick und startet mit boot-uuid= davon, ohne RAM-Disk. Das braucht kaum Arbeitsspeicher, steht
# und fällt aber mit dem USB-Treiber.
#
# Ergebnis: ~/ClokeOS-work/out/recovery-<arch>/usb.img (vorher recovery/build.sh). Auf den Stick:
#   sudo dd if=usb.img of=/dev/rdiskN bs=4m    (macOS; oder balenaEtcher)
# Braucht einen Rechner mit UEFI (ohne CSM, Secure Boot aus).
set -euo pipefail
export CLOKEOS_FLAVOR=desktop
source "$(dirname "$0")/../config.sh"
RESIZE2FS="$BREW/opt/e2fsprogs/sbin/resize2fs"
DUMPE2FS="$BREW/opt/e2fsprogs/sbin/dumpe2fs"
need "$DEBUGFS" "$E2FSCK" "$TUNE2FS" "$RESIZE2FS" "$DUMPE2FS" python3 mcopy mformat mtype mdir mmd uuidgen
export MTOOLS_SKIP_CHECK=1
payload="${1:-}"
mode="${CLOKEOS_USB_MODE:-ramdisk}"
ramdisk=1
[ "$mode" = partition ] && ramdisk=0

out="$WORK/out/recovery-$CLOKE_ARCH"
tmp="$WORK/tmp/usb-$CLOKE_ARCH"
[ -f "$out/recovery.ext4" ] && [ -f "$out/esp.img" ] || { echo "Fehlt: $out – zuerst recovery/build.sh" >&2; exit 1; }
rm -rf "$tmp"
mkdir -p "$tmp"

# --- Wurzel: die Wiederherstellung, dazu die Abbilder ---------------------------------------
root="$tmp/root.ext4"
cp -c "$out/recovery.ext4" "$root" 2>/dev/null || cp "$out/recovery.ext4" "$root"
extra=16
files=()
if [ -n "$payload" ]; then
  [ -f "$payload/clokeos-$CLOKE_ARCH.txt" ] || { echo "Keine Beschreibung clokeos-$CLOKE_ARCH.txt in $payload" >&2; exit 1; }
  files=("$payload/clokeos-$CLOKE_ARCH.txt")
  parts="esp system recovery"
  # in der RAM-Disk ist kein Platz für die Wiederherstellung: der Installer lädt sie nach
  [ "$ramdisk" = 1 ] && parts="esp system"
  for k in $parts; do
    f=$(awk -F'[= ]' -v k="$k" '$1 == k {print $2}' "$payload/clokeos-$CLOKE_ARCH.txt")
    [ -f "$payload/$f" ] || { echo "Fehlt: $payload/$f" >&2; exit 1; }
    files+=("$payload/$f")
  done
  extra=$(( extra + $(du -cm "${files[@]}" | tail -1 | cut -f1) ))
fi
# erst auf das Minimum, dann um die Abbilder und Platz für Protokolle und /tmp vergrößern
"$E2FSCK" -fy "$root" >/dev/null 2>&1 || true
"$RESIZE2FS" -M "$root" >/dev/null 2>&1
size_mb=$(( $(stat -f %z "$root") / 1048576 ))
"$RESIZE2FS" "$root" "$(( (size_mb + extra + 15) / 16 * 16 ))M" >/dev/null 2>&1
"$TUNE2FS" -U random -L "Installer" "$root" >/dev/null
if [ ${#files[@]} -gt 0 ]; then
  {
    echo "mkdir /Library/ClokeOS"
    echo "mkdir /Library/ClokeOS/Install"
    for f in "${files[@]}"; do echo "write $f /Library/ClokeOS/Install/$(basename "$f")"; done
  } > "$tmp/payload.cmds"
  "$DEBUGFS" -w -f "$tmp/payload.cmds" "$root" > "$tmp/payload.log" 2>&1
  "$DEBUGFS" -R "stat /Library/ClokeOS/Install/clokeos-$CLOKE_ARCH.txt" "$root" 2>/dev/null | grep -q '^Inode' ||
    { cat "$tmp/payload.log" >&2; exit 1; }
fi
"$E2FSCK" -fy "$root" >/dev/null 2>&1 || true
"$E2FSCK" -fn "$root" > "$tmp/e2fsck.log" 2>&1 || { cat "$tmp/e2fsck.log" >&2; exit 1; }
rsize=$(stat -f %z "$root")
# kleine RAM-Disk ohne Abbilder: nur die Wiederherstellung, auf ihr Minimum plus Platz für /tmp
lite="$tmp/lite.ext4"
if [ "$ramdisk" = 1 ]; then
  cp -c "$out/recovery.ext4" "$lite" 2>/dev/null || cp "$out/recovery.ext4" "$lite"
  "$RESIZE2FS" -M "$lite" >/dev/null 2>&1
  lmb=$(( $(stat -f %z "$lite") / 1048576 ))
  "$RESIZE2FS" "$lite" "$(( (lmb + 16 + 15) / 16 * 16 ))M" >/dev/null 2>&1
  "$TUNE2FS" -U random -L "Installer" "$lite" >/dev/null
  "$E2FSCK" -fn "$lite" > "$tmp/e2fsck-lite.log" 2>&1 || { cat "$tmp/e2fsck-lite.log" >&2; exit 1; }
fi

# --- Dateien der EFI-Partition ---------------------------------------------------------------
# einzeln aus EFI/BOOT (mcopy -s benennt einen ausgepackten Ordner EFI in EFI_ um)
mkdir -p "$tmp/boot"
for f in $(mdir -b -i "$out/esp.img@@0" ::/EFI/BOOT | sed 's|^::/EFI/BOOT/||'); do
  case "$f" in boot-args-*|.|..) continue ;; esac
  mcopy -n -i "$out/esp.img@@0" "::/EFI/BOOT/$f" "$tmp/boot/$f"
done
[ -f "$tmp/boot/BOOTX64.EFI" ] && [ -f "$tmp/boot/kernel" ] || { echo "Loader oder Kernel fehlt in esp.img" >&2; exit 1; }
base=$(tr -d '\r\n' < "$tmp/boot/boot-args.txt" | sed -E 's/ *boot-uuid=[^ ]*//g; s/ *rd=[^ ]*//g')
part_guid=$(uuidgen | tr a-f A-F)

if [ "$ramdisk" = 1 ]; then
  rd_mb=$(( rsize / 1048576 ))
  [ "$rd_mb" -le 880 ] || { echo "RAM-Disk zu groß: $rd_mb MB (höchstens 880, siehe loader/xnu-loader.patch)" >&2; exit 1; }
  printf '%s rd=md0\n' "$base" > "$tmp/boot/boot-args.txt"
  esize=$(( (rsize + $(stat -f %z "$lite") + $(du -sk "$tmp/boot" | cut -f1) * 1024 + 96 * 1048576) / 1048576 * 1048576 ))
else
  # Wurzel auf der zweiten Partition des Sticks; boot-uuid= nennt ihre GPT-Kennung
  printf '%s boot-uuid=%s\n' "$base" "$part_guid" > "$tmp/boot/boot-args.txt"
  esize=$(( 64 * 1048576 ))
fi
esp="$tmp/usb-esp.img"
truncate -s "$esize" "$esp"
mformat -i "$esp" -F -v CLOKEOS -T $(( esize / 512 )) -h 64 -s 32 ::
mmd -i "$esp" ::/EFI ::/EFI/BOOT
mcopy -i "$esp" "$tmp/boot/"* ::/EFI/BOOT/
mtype -i "$esp" ::/EFI/BOOT/boot-args.txt | grep -q 'rd=md0\|boot-uuid=' || { echo "boot-args.txt fehlt im USB-Abbild" >&2; exit 1; }

# GPT-Name "ClokeOS Installer": der Installer bietet den Stick nicht als Ziel an
if [ "$ramdisk" = 1 ]; then
  mcopy -i "$esp" "$root" ::/ramdisk.img
  mcopy -i "$esp" "$lite" ::/ramdisk-lite.img
  python3 -I "$HERE/recovery/imagetool.py" gpt "$out/usb.img" "$esp" - "ClokeOS Installer" "$part_guid"
else
  python3 -I "$HERE/recovery/imagetool.py" gpt "$out/usb.img" "$esp" "$root" "ClokeOS Installer" "$part_guid"
fi
if [ "$ramdisk" = 1 ]; then
  echo "USB-Installationsmedium: $out/usb.img (RAM-Disk $(( rsize / 1048576 )) MB$([ -n "$payload" ] && echo " mit Abbildern für die Installation ohne Internet"), schlanke $(( $(stat -f %z "$lite") / 1048576 )) MB)"
else
  echo "USB-Installationsmedium: $out/usb.img (Wiederherstellung als Partition, $(( rsize / 1048576 )) MB)"
fi
rm -rf "$tmp"
