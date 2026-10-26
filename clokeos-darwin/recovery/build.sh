#!/bin/bash
# Baut die Wiederherstellung (wie macOS Recovery) und die Teile eines Online-Releases aus der
# Desktop-Variante. Architektur wie überall mit CLOKEOS_ARCH (Standard: x86_64).
#
#   recovery/build.sh             nutzt clokeos-<version>-<arch>-desktop.img (baut es, falls es fehlt)
#
# Ergebnis in ~/ClokeOS-work/out/recovery-<arch>/:
#   esp.img          EFI-Partition: Loader, Kernel und Boot-Argumente boot-args.txt, boot-args-system.txt
#                    und boot-args-recovery.txt. Ihr boot-uuid= ist ein Platzhalter: Der Loader startet
#                    damit die GPT-Partition dieser Kennung, und clokeos-installer setzt beim
#                    Installieren die Kennungen der neuen Partitionen ein (bootnext tauscht die Dateien)
#   system.ext4      Wurzel des vollen Systems, auf belegt + SYSTEM_FREE_MB gebracht (resize2fs), mit
#                    /var/db/.RunClokeOSSetup: beim ersten Start läuft der Einrichtungsassistent
#   recovery.ext4    Wurzel der Wiederherstellung: das System ohne Wine, Compiler und Entwickler-
#                    dateien (imagetool.py prune), dazu recovery/overlay und /etc/clokeos-recovery
#   installer.img    startfähiges Installationsmedium: EFI-Partition + Wiederherstellung unter dem
#                    GPT-Namen "ClokeOS Installer" mit eigener Partitionskennung in boot-args.txt
#   info             Version, Build und die UUIDs der Dateisysteme
set -euo pipefail
export CLOKEOS_FLAVOR=desktop
source "$(dirname "$0")/../config.sh"
need "$DEBUGFS" "$E2FSCK" "$TUNE2FS" "$BREW/opt/e2fsprogs/sbin/resize2fs" "$BREW/opt/e2fsprogs/sbin/dumpe2fs" \
  python3 mcopy mtype
RESIZE2FS="$BREW/opt/e2fsprogs/sbin/resize2fs"
DUMPE2FS="$BREW/opt/e2fsprogs/sbin/dumpe2fs"
export MTOOLS_SKIP_CHECK=1

SYSTEM_FREE_MB=${CLOKEOS_SYSTEM_FREE_MB:-2048}     # freier Platz im installierten System
RECOVERY_FREE_MB=${CLOKEOS_RECOVERY_FREE_MB:-192}  # in der Wiederherstellung (Protokolle, /tmp)

[ -f "$CLOKE_IMG" ] || "$HERE/brand.sh"
out="$WORK/out/recovery-$CLOKE_ARCH"
tmp="$WORK/tmp/recovery-$CLOKE_ARCH"
rm -rf "$out" "$tmp"
mkdir -p "$out" "$tmp"

read -r esp_start esp_count < <(gpt_part "$CLOKE_IMG" "$GUID_ESP")
read -r root_start root_count < <(gpt_part "$CLOKE_IMG" "$GUID_LINUX_FS")
dd if="$CLOKE_IMG" of="$out/esp.img" bs=512 skip="$esp_start" count="$esp_count" status=none
dd if="$CLOKE_IMG" of="$out/system.ext4" bs=512 skip="$root_start" count="$root_count" status=none

fsuuid() { "$DUMPE2FS" -h "$1" 2>/dev/null | awk -F': *' '/^Filesystem UUID/ {print toupper($2)}'; }
used_mb() {
  "$DUMPE2FS" -h "$1" 2>/dev/null | awk -F': *' '/^Block count/ {c=$2} /^Free blocks/ {f=$2} /^Block size/ {b=$2}
    END {printf "%d", (c - f) * b / 1048576}'
}
# Größe auf ein Vielfaches von 64 MiB
fit() { "$E2FSCK" -fy "$1" >/dev/null 2>&1 || true; "$E2FSCK" -fn "$1" >/dev/null; "$RESIZE2FS" "$1" "$2M" >/dev/null 2>&1; }

# --- System ---------------------------------------------------------------------------------
sys_mb=$(( ($(used_mb "$out/system.ext4") + SYSTEM_FREE_MB + 63) / 64 * 64 ))
fit "$out/system.ext4" "$sys_mb"
"$TUNE2FS" -U random "$out/system.ext4" >/dev/null
# Beim ersten Start nach der Installation läuft der Einrichtungsassistent wie bei macOS
# (cloakserver/setup.c): Sprache, Region, eigenes Konto, Erscheinungsbild
: > "$tmp/run-setup"
"$DEBUGFS" -w -R "write $tmp/run-setup /var/db/.RunClokeOSSetup" "$out/system.ext4" >/dev/null 2>&1
"$DEBUGFS" -R 'stat /var/db/.RunClokeOSSetup' "$out/system.ext4" 2>/dev/null | grep -q '^Inode' ||
  { echo "Markierung für den Einrichtungsassistenten fehlt" >&2; exit 1; }
sys_uuid=$(fsuuid "$out/system.ext4")
echo "System: $sys_mb MB, UUID $sys_uuid"

# --- Wiederherstellung: das System ohne Ballast, kompakt neu angelegt -----------------------
# Ausgepackt, ausgedünnt (imagetool.py prune --usb: ohne Wine, Compiler, Mesa/LLVM, Übersetzungen …)
# und mit mke2fs -d neu angelegt: Ein verkleinertes Dateisystem des großen Systems behielte dessen
# Inode-Tabellen und bliebe über 1,4 GB. So passt die Wiederherstellung samt Abbildern als RAM-Disk
# auf den USB-Stick (recovery/usb.sh, unter die Grenze von XNU), und die Partition wird kleiner.
mkdir -p "$tmp/root" # rdump legt den Baum in einen vorhandenen Ordner
"$DEBUGFS" -R "rdump / $tmp/root" "$out/system.ext4" >/dev/null 2>&1 || true
[ -x "$tmp/root/sbin/launchd" ] || { echo "Wurzel nicht ausgepackt" >&2; exit 1; }

# --- Update-Paket: Programme und Oberfläche des Systems, als Datei in der EFI-Partition (clokeos-update,
# clokeos-apply-update). Das System selbst lässt sich in der Wiederherstellung nicht einbinden (ext4 würde den
# Kernel abstürzen lassen), deshalb legt das nächste System-Start die Dateien hin. Das Paket entsteht vor dem
# Ausdünnen; Benutzer, /etc und /var bleiben außen vor.
upd_paths=(System/Library/CoreServices System/Applications Library/Fonts Library/Preferences/com.clokeos.dock.plist
           share/themes share/icons share/clokeos share/glib-2.0/schemas lib/gtk-3.0/modules etc/xdg
           usr/libexec/clokeos-aktivitaet usr/libexec/clokeos-diag usr/libexec/clokeos-kalender
           usr/libexec/clokeos-apply-update usr/libexec/cloakserver-session usr/libexec/cloakserver-start
           usr/libexec/cloakserver-xorg usr/lib/libclokeos-xpoll.dylib lib/libXcursor.1.dylib usr/bin/clokefetch)
upd_have=()
for q in "${upd_paths[@]}"; do [ -e "$tmp/root/$q" ] && upd_have+=("$q"); done
COPYFILE_DISABLE=1 tar --uid 0 --gid 0 --numeric-owner --no-xattrs -C "$tmp/root" -cf "$tmp/update.tar" "${upd_have[@]}"
xz -T0 -9 -c "$tmp/update.tar" > "$tmp/update.xz"
upd_id="$CLOKE_VERSION-$CLOKE_BUILD-$(shasum -a 256 "$tmp/update.xz" | cut -c1-12)"
printf '%s\n' "$upd_id" > "$tmp/update.id"
echo "Update-Paket: $(stat -f %z "$tmp/update.xz") Bytes ($upd_id)"
[ "$(stat -f %z "$tmp/update.xz")" -lt 14000000 ] || { echo "Update-Paket passt nicht mehr in die EFI-Partition" >&2; exit 1; }
# Das frisch installierte System kennt seine Kennung schon (sonst legte der erste Start dieselben Dateien noch einmal hin)
"$DEBUGFS" -w -R "write $tmp/update.id /var/db/clokeos-update-id" "$out/system.ext4" >/dev/null 2>&1
"$DEBUGFS" -R 'stat /var/db/clokeos-update-id' "$out/system.ext4" 2>/dev/null | grep -q '^Inode' ||
  { echo "Update-Kennung nicht im System" >&2; exit 1; }
python3 -I "$HERE/recovery/imagetool.py" prune "$tmp/root" "$tmp/prune.list" --usb
# die Liste steht tiefste zuerst: Dateien, Symlinks und leere Ordner
while read -r p; do
  if [ -d "$tmp/root$p" ] && [ ! -L "$tmp/root$p" ]; then rmdir "$tmp/root$p" 2>/dev/null || rm -rf "$tmp/root$p"
  else rm -f "$tmp/root$p"; fi
done < "$tmp/prune.list"
# Die Wiederherstellung braucht nur das Standard-Hintergrundbild und keine Schrift für CJK: sonst passt die RAM-Disk
# des Offline-Sticks nicht unter die Grenze des Loaders (880 MB)
rm -f "$tmp/root/Library/ClokeOS/Wallpapers/"art*.png "$tmp/root/Library/ClokeOS/Wallpapers/"*-t.png
rm -rf "$tmp/root/Library/Fonts/NotoSansCJK"
rm -f "$tmp/root/var/db/.RunClokeOSSetup" # der Einrichtungsassistent gehört zum installierten System

# Eigene Dateien der Wiederherstellung
cp -R "$HERE/recovery/overlay/." "$tmp/root/"
cat > "$tmp/root/etc/clokeos-recovery" <<EOF
# ClokeOS-Wiederherstellung: CloakServer zeigt statt Anmeldung und Schreibtisch die Wiederherstellung.
RELEASES="$CLOKE_RELEASES_URL"
VERSION="$CLOKE_VERSION"
BUILD="$CLOKE_BUILD"
EOF
find "$tmp/root" -name .DS_Store -delete
xattr -rc "$tmp/root" 2>/dev/null || true # macOS hängt com.apple.provenance an; nicht ins ext4
DEBUGFS="$DEBUGFS" python3 -I "$HERE/recovery/imagetool.py" owners "$out/system.ext4" "$tmp/root" "$tmp/owners.cmds"
used_root=$(du -sm "$tmp/root" | cut -f1)
nfiles=$(find "$tmp/root" | wc -l | tr -d ' ')
rec_mb=$(( ((used_root + RECOVERY_FREE_MB) * 11 / 10 + 63) / 64 * 64 ))
# dieselben Merkmale wie PureDarwins ext4 (ohne das neue orphan_file von mke2fs 1.47)
"$MKE2FS" -q -t ext4 -O ^orphan_file,metadata_csum_seed -N $(( nfiles * 3 / 2 + 4096 )) -m 0 -J size=16 \
  -L Recovery -d "$tmp/root" "$out/recovery.ext4" "${rec_mb}M"
"$DEBUGFS" -w -f "$tmp/owners.cmds" "$out/recovery.ext4" > "$tmp/owners.log" 2>&1
"$E2FSCK" -fy "$out/recovery.ext4" >/dev/null 2>&1 || true
"$E2FSCK" -fn "$out/recovery.ext4" > "$tmp/e2fsck.log" 2>&1 || { cat "$tmp/e2fsck.log" >&2; exit 1; }
"$DEBUGFS" -R 'stat /usr/libexec/clokeos-installer' "$out/recovery.ext4" 2>/dev/null | grep -q 'User: *0 ' ||
  { echo "Besitzer in der Wiederherstellung nicht gesetzt" >&2; exit 1; }
rm -rf "$tmp/root"
cp -c "$out/recovery.ext4" "$tmp/installer.ext4" 2>/dev/null || cp "$out/recovery.ext4" "$tmp/installer.ext4"
"$TUNE2FS" -U random -L "Recovery" "$out/recovery.ext4" >/dev/null
"$TUNE2FS" -U random -L "Installer" "$tmp/installer.ext4" >/dev/null
rec_uuid=$(fsuuid "$out/recovery.ext4")
inst_uuid=$(fsuuid "$tmp/installer.ext4")
echo "Wiederherstellung: $rec_mb MB, UUID $rec_uuid"

# --- EFI-Partitionen ------------------------------------------------------------------------
# Grundlage sind die Boot-Argumente des Desktop-Images. boot-uuid= nennt die GPT-Partition, die
# der Loader als Wurzel nimmt (nicht die UUID des Dateisystems); auf der installierten Platte setzt
# clokeos-installer die echten Kennungen ein, hier stehen Platzhalter gleicher Länge.
esp="$out/esp.img@@0"
base=$(mtype -i "$esp" ::/EFI/BOOT/boot-args.txt | tr -d '\r\n' | sed -E 's/ *boot-uuid=[^ ]*//g')
inst_part=$(uuidgen | tr a-f A-F)
printf '%s boot-uuid=%s\n' "$base" 00000000-0000-0000-0000-000000000002 > "$tmp/boot-args-system.txt"
printf '%s boot-uuid=%s\n' "$base" 00000000-0000-0000-0000-000000000003 > "$tmp/boot-args-recovery.txt"
printf '%s boot-uuid=%s\n' "$base" "$inst_part" > "$tmp/boot-args-installer.txt"
cp "$out/esp.img" "$tmp/installer-esp.img"
mcopy -o -i "$esp" "$tmp/boot-args-system.txt" ::/EFI/BOOT/boot-args.txt
mcopy -o -i "$esp" "$tmp/boot-args-system.txt" ::/EFI/BOOT/boot-args-system.txt
mcopy -o -i "$esp" "$tmp/boot-args-recovery.txt" ::/EFI/BOOT/boot-args-recovery.txt
mcopy -o -i "$tmp/installer-esp.img@@0" "$tmp/boot-args-installer.txt" ::/EFI/BOOT/boot-args.txt
mcopy -o -i "$esp" "$tmp/update.xz" ::/EFI/BOOT/update.xz
mcopy -o -i "$esp" "$tmp/update.id" ::/EFI/BOOT/update.id
for f in boot-args.txt boot-args-system.txt boot-args-recovery.txt; do
  mtype -i "$esp" "::/EFI/BOOT/$f" | grep -q "boot-uuid=" || { echo "$f nicht in esp.img" >&2; exit 1; }
done

# --- Installationsmedium ---------------------------------------------------------------------
python3 -I "$HERE/recovery/imagetool.py" gpt "$out/installer.img" "$tmp/installer-esp.img" "$tmp/installer.ext4" \
  "ClokeOS Installer" "$inst_part"

cat > "$out/info" <<EOF
version=$CLOKE_VERSION
build=$CLOKE_BUILD
codename=$CLOKE_CODENAME
arch=$CLOKE_ARCH
system_uuid=$sys_uuid
recovery_uuid=$rec_uuid
installer_uuid=$inst_uuid
installer_partition=$inst_part
EOF
rm -rf "$tmp"
ls -l "$out"
echo "Installationsmedium testen: CLOKEOS_ARCH=$CLOKE_ARCH CLOKEOS_FLAVOR=desktop $HERE/run.sh --image $out/installer.img --disk <leere-platte.img>"
