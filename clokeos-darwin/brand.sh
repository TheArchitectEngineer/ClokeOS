#!/bin/bash
# Baut aus dem PureDarwin-Image ein ClokeOS-Image (~/ClokeOS-work/out/clokeos-<version>-x86_64.img).
#
# Das Basisimage bleibt unverändert. Das Wurzeldateisystem (ext4) wird mit debugfs bearbeitet,
# dafür braucht es weder Linux noch Root-Rechte. Was geändert wird:
#   - overlay/ wird ins Wurzeldateisystem kopiert (sw_vers, clokefetch, Rechnername, Anmeldetext)
#   - SystemVersion.plist und /etc/clokeos-release werden aus config.sh erzeugt
#   - /etc/zshrc bekommt die ClokeOS-Eingabeaufforderung und zeigt beim Start /etc/motd
#   - PureDarwins Rechnername-Dienst wird entfernt, das Volume heißt "Cloke HD"
#   - die EFI-Partition bekommt den Loader mit Bootlogo (loader/build.sh) und die Boot-Argumente
#     aus config.sh, beides mit mtools
set -euo pipefail
source "$(dirname "$0")/config.sh"
need "$DEBUGFS" "$E2FSCK" "$TUNE2FS" python3 mcopy mtype

"$HERE/fetch.sh"
"$HERE/loader/build.sh" "$LOADER_EFI"

tmp="$WORK/tmp/brand"
rm -rf "$tmp"
mkdir -p "$tmp/stage" "$(dirname "$CLOKE_IMG")"
cp "$PD_IMG" "$tmp/image.img"

read -r root_start root_count < <(gpt_part "$tmp/image.img" "$GUID_LINUX_FS")
root="$tmp/root.ext4"
dd if="$tmp/image.img" of="$root" bs=512 skip="$root_start" count="$root_count" status=none

# --- Dateien zusammenstellen ---------------------------------------------------------------
cp -R "$HERE/overlay/." "$tmp/stage/"
find "$tmp/stage" -name .DS_Store -delete

mkdir -p "$tmp/stage/System/Library/CoreServices"
cat > "$tmp/stage/System/Library/CoreServices/SystemVersion.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>ProductBuildVersion</key>
	<string>$CLOKE_BUILD</string>
	<key>ProductCopyright</key>
	<string>$(date +%Y) ClokeOS. Darwin: Apple Inc. (APSL 2.0), PureDarwin $PD_VERSION.</string>
	<key>ProductName</key>
	<string>$CLOKE_NAME</string>
	<key>ProductUserVisibleVersion</key>
	<string>$CLOKE_VERSION</string>
	<key>ProductVersion</key>
	<string>$CLOKE_VERSION</string>
</dict>
</plist>
EOF

cat > "$tmp/stage/etc/clokeos-release" <<EOF
NAME="$CLOKE_NAME"
VERSION="$CLOKE_VERSION"
CODENAME="$CLOKE_CODENAME"
BUILD="$CLOKE_BUILD"
HOSTNAME="$CLOKE_HOST"
BASE="PureDarwin $PD_VERSION"
EOF

"$DEBUGFS" -R 'cat /etc/zshrc' "$root" 2>/dev/null > "$tmp/stage/etc/zshrc"
grep -q 'interactive' "$tmp/stage/etc/zshrc" || { echo "/etc/zshrc im Basisimage nicht gefunden" >&2; exit 1; }
cat >> "$tmp/stage/etc/zshrc" <<'EOF'

# ClokeOS
export PS1='%n@%m %1~ %# '
# Die Konsole startet oft, bevor com.clokeos.hostname den Namen gesetzt hat.
_clokeos_host() { [[ $HOST == localhost* ]] && HOST=$(/bin/hostname) }
precmd_functions+=(_clokeos_host)
if [[ -z $CLOKEOS_GREETED && -r /etc/motd ]]; then
  export CLOKEOS_GREETED=1
  cat /etc/motd
fi
EOF

# --- in das ext4-Dateisystem schreiben -----------------------------------------------------
cmds="$tmp/debugfs.cmds"
{
  echo "rm /System/Library/LaunchDaemons/org.puredarwin.hostname.plist"
  echo "rm /usr/libexec/pd-set-hostname"
  (cd "$tmp/stage" && find . -mindepth 1 -type d | sed 's|^\.||' | sort) | while read -r d; do
    echo "mkdir $d"
  done
  (cd "$tmp/stage" && find . -type f | sed 's|^\.||' | sort) | while read -r f; do
    mode=$(stat -f '%Lp' "$tmp/stage$f")
    echo "rm $f"
    echo "write $tmp/stage$f $f"
    echo "set_inode_field $f mode 0100$mode"
    echo "set_inode_field $f uid 0"
    echo "set_inode_field $f gid 0"
  done
} > "$cmds"
"$DEBUGFS" -w -f "$cmds" "$root" > "$tmp/debugfs.log" 2>&1
"$TUNE2FS" -L "Cloke HD" "$root" >/dev/null
"$E2FSCK" -fn "$root" > "$tmp/e2fsck.log" 2>&1 || { cat "$tmp/e2fsck.log" >&2; exit 1; }

# Stichprobe: stehen die neuen Dateien wirklich im Image?
for f in $(cd "$tmp/stage" && find . -type f | sed 's|^\.||'); do
  if ! "$DEBUGFS" -R "cat $f" "$root" 2>/dev/null | cmp -s - "$tmp/stage$f"; then
    echo "Nicht geschrieben: $f (siehe $tmp/debugfs.log)" >&2
    exit 1
  fi
done

dd if="$root" of="$tmp/image.img" bs=512 seek="$root_start" conv=notrunc status=none

# --- EFI-Partition: Loader mit Bootlogo und Boot-Argumente --------------------------------
read -r esp_start _ < <(gpt_part "$tmp/image.img" "$GUID_ESP")
esp="$tmp/image.img@@$((esp_start * 512))"
printf '%s\n' "$CLOKE_BOOT_ARGS" > "$tmp/boot-args.txt"
mcopy -o -i "$esp" "$LOADER_EFI" ::/EFI/BOOT/BOOTX64.EFI
mcopy -o -i "$esp" "$tmp/boot-args.txt" ::/EFI/BOOT/boot-args.txt
mtype -i "$esp" ::/EFI/BOOT/BOOTX64.EFI | cmp -s - "$LOADER_EFI" ||
  { echo "Loader nicht in der EFI-Partition angekommen" >&2; exit 1; }
mtype -i "$esp" ::/EFI/BOOT/boot-args.txt | cmp -s - "$tmp/boot-args.txt" ||
  { echo "boot-args.txt nicht in der EFI-Partition angekommen" >&2; exit 1; }

mv "$tmp/image.img" "$CLOKE_IMG"
rm -rf "$tmp"
echo "Fertig: $CLOKE_IMG"
echo "Starten mit: $HERE/run.sh"
