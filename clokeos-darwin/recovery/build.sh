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
#   system.ext4      Wurzel des vollen Systems, auf belegt + SYSTEM_FREE_MB gebracht (resize2fs)
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
sys_uuid=$(fsuuid "$out/system.ext4")
echo "System: $sys_mb MB, UUID $sys_uuid"

# --- Wiederherstellung: das System ohne Ballast ---------------------------------------------
cp -c "$out/system.ext4" "$out/recovery.ext4" 2>/dev/null || cp "$out/system.ext4" "$out/recovery.ext4"
mkdir -p "$tmp/root" # rdump legt den Baum in einen vorhandenen Ordner
"$DEBUGFS" -R "rdump / $tmp/root" "$out/recovery.ext4" >/dev/null 2>&1 || true
[ -x "$tmp/root/sbin/launchd" ] || { echo "Wurzel nicht ausgepackt" >&2; exit 1; }
python3 -I "$HERE/recovery/imagetool.py" prune "$tmp/root" "$tmp/prune.list"
# Dateien und Symlinks mit rm, Ordner danach mit rmdir (die Liste steht tiefste zuerst)
while read -r p; do
  if [ -d "$tmp/root$p" ] && [ ! -L "$tmp/root$p" ]; then echo "rmdir $p"; else echo "rm $p"; fi
done < "$tmp/prune.list" > "$tmp/prune.cmds"
"$DEBUGFS" -w -f "$tmp/prune.cmds" "$out/recovery.ext4" > "$tmp/prune.log" 2>&1

# Eigene Dateien der Wiederherstellung
stage="$tmp/stage"
mkdir -p "$stage/etc"
cp -R "$HERE/recovery/overlay/." "$stage/"
cat > "$stage/etc/clokeos-recovery" <<EOF
# ClokeOS-Wiederherstellung: CloakServer zeigt statt Anmeldung und Schreibtisch die Wiederherstellung.
RELEASES="$CLOKE_RELEASES_URL"
VERSION="$CLOKE_VERSION"
BUILD="$CLOKE_BUILD"
EOF
find "$stage" -name .DS_Store -delete
{
  (cd "$stage" && find . -mindepth 1 -type d | sed 's|^\.||' | sort) | while read -r d; do echo "mkdir $d"; done
  (cd "$stage" && find . -type f | sed 's|^\.||' | sort) | while read -r f; do
    echo "rm $f"
    echo "write $stage$f $f"
    echo "set_inode_field $f mode 0100$(stat -f '%Lp' "$stage$f")"
    echo "set_inode_field $f uid 0"
    echo "set_inode_field $f gid 0"
  done
} > "$tmp/stage.cmds"
"$DEBUGFS" -w -f "$tmp/stage.cmds" "$out/recovery.ext4" > "$tmp/stage.log" 2>&1
"$E2FSCK" -fy "$out/recovery.ext4" >/dev/null 2>&1 || true
"$E2FSCK" -fn "$out/recovery.ext4" > "$tmp/e2fsck.log" 2>&1 || { cat "$tmp/e2fsck.log" >&2; exit 1; }
# Auf das Minimum, das resize2fs zulässt (Inode-Tabellen und Journal des großen Systems bleiben,
# es liegt deshalb über dem belegten Platz), dann RECOVERY_FREE_MB dazu
"$RESIZE2FS" -M "$out/recovery.ext4" >/dev/null 2>&1
min_mb=$("$DUMPE2FS" -h "$out/recovery.ext4" 2>/dev/null | awk -F': *' '/^Block count/ {c=$2} /^Block size/ {b=$2}
  END {printf "%d", c * b / 1048576}')
rec_mb=$(( (min_mb + RECOVERY_FREE_MB + 63) / 64 * 64 ))
fit "$out/recovery.ext4" "$rec_mb"
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
