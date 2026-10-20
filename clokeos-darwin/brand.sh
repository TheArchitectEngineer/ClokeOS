#!/bin/bash
# Baut aus dem PureDarwin-Image ein ClokeOS-Image (~/ClokeOS-work/out/clokeos-<version>-<arch>.img).
# Architektur wie bei run.sh mit CLOKEOS_ARCH=arm64 (Standard: x86_64), die Desktop-Variante mit
# CLOKEOS_FLAVOR=desktop (Ergebnis: clokeos-<version>-x86_64-desktop.img).
#
# Das Basisimage bleibt unverändert. Das Wurzeldateisystem (ext4) wird mit debugfs bearbeitet,
# dafür braucht es weder Linux noch Root-Rechte. Was geändert wird:
#   - overlay/ wird ins Wurzeldateisystem kopiert (sw_vers, clokefetch, Rechnername, Anmeldetext)
#   - SystemVersion.plist und /etc/clokeos-release werden aus config.sh erzeugt
#   - /etc/zshrc bekommt die ClokeOS-Eingabeaufforderung und zeigt beim Start /etc/motd
#   - PureDarwins Rechnername-Dienst wird entfernt, das Volume heißt "Cloke HD"
#   - liegt eine RAM-Disk in der EFI-Partition (ramdisk.img, arm64-Minimal), bekommt sie dasselbe
#   - x86_64: die EFI-Partition bekommt den Loader mit Bootlogo (loader/build.sh) und die
#     Boot-Argumente aus config.sh, beides mit mtools
#   - desktop: dazu overlay-desktop/ und CloakServer (cloakserver/build.sh), der beim Start
#     Xorg mit dem ClokeOS-Desktop öffnet, die Programme als .app-Bundles wie bei macOS
#     (cloakserver/bundles.py) und das Benutzerkonto aus config.sh (CLOKE_USER); sind die
#     Darling-Quellen geladen, auch die Mac-Frameworks samt Hallo.app (macapps/install.sh)
set -euo pipefail
source "$(dirname "$0")/config.sh"
need "$DEBUGFS" "$E2FSCK" "$TUNE2FS" python3 mcopy mtype mdir
export MTOOLS_SKIP_CHECK=1

"$HERE/fetch.sh"
[ -n "$LOADER_EFI" ] && "$HERE/loader/build.sh" "$LOADER_EFI"

tmp="$WORK/tmp/brand"
rm -rf "$tmp"
mkdir -p "$tmp/stage" "$(dirname "$CLOKE_IMG")"
# Auf APFS ist -c ein Klon: kostet bei den 6 GiB des Desktop-Images weder Zeit noch Platz.
cp -c "$PD_IMG" "$tmp/image.img" 2>/dev/null || cp "$PD_IMG" "$tmp/image.img"

read -r root_start root_count < <(gpt_part "$tmp/image.img" "$GUID_LINUX_FS")
root="$tmp/root.ext4"
dd if="$tmp/image.img" of="$root" bs=512 skip="$root_start" count="$root_count" status=none

# --- Dateien zusammenstellen ---------------------------------------------------------------
cp -R "$HERE/overlay/." "$tmp/stage/"
if [ "$CLOKE_FLAVOR" = desktop ]; then
  cp -R "$HERE/overlay-desktop/." "$tmp/stage/"
  "$HERE/cloakserver/build.sh" "$tmp/stage"
  python3 -I "$HERE/cloakserver/bundles.py" "$tmp/stage" "$WORK/out/CloakServer/Resources/icons" \
    "$CLOKE_VERSION" "$CLOKE_BUILD"
  "$HERE/macapps/install.sh" "$tmp/stage"
fi
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
TIMEZONE="$CLOKE_TZ"
LOGIN="$CLOKE_LOGIN"
BASE="PureDarwin $PD_VERSION"
EOF

"$DEBUGFS" -R 'cat /etc/zshrc' "$root" 2>/dev/null > "$tmp/stage/etc/zshrc"
grep -q 'interactive' "$tmp/stage/etc/zshrc" || { echo "/etc/zshrc im Basisimage nicht gefunden" >&2; exit 1; }
cat >> "$tmp/stage/etc/zshrc" <<'EOF'

# ClokeOS
export PS1='%n@%m %1~ %# '
[[ -r /etc/clokeos-release ]] && export TZ=$(. /etc/clokeos-release; print -r -- $TIMEZONE)
# Die Konsole startet oft, bevor com.clokeos.hostname den Namen gesetzt hat.
_clokeos_host() { [[ $HOST == localhost* ]] && HOST=$(/bin/hostname) }
precmd_functions+=(_clokeos_host)
if [[ -z $CLOKEOS_GREETED && -r /etc/motd ]]; then
  export CLOKEOS_GREETED=1
  cat /etc/motd
fi
EOF

# --- Desktop: Benutzerkonto wie im Prototyp ------------------------------------------------
# Ohne Passwort (leeres Feld, die Eingabetaste meldet an), Gruppe staff, Ordner wie in clokeos.html.
# PureDarwin hat kein su und kein sudo; für Verwaltung gibt es auf der Anmeldung root.
: > "$tmp/extra.cmds"
if [ "$CLOKE_FLAVOR" = desktop ]; then
  # /Applications wie bei macOS für Administratoren beschreibbar; hier die Gruppe staff, damit
  # der Benutzer Programme durch Hineinziehen installieren kann
  printf 'set_inode_field /Applications mode 040775\nset_inode_field /Applications gid 20\n' >> "$tmp/extra.cmds"
fi
if [ "$CLOKE_FLAVOR" = desktop ] && [ -n "$CLOKE_USER" ]; then
  home="/Users/$CLOKE_USER"
  for f in master.passwd:root passwd:root group:staff; do
    "$DEBUGFS" -R "cat /etc/${f%:*}" "$root" 2>/dev/null > "$tmp/stage/etc/${f%:*}"
    grep -q "^${f#*:}:" "$tmp/stage/etc/${f%:*}" || { echo "/etc/${f%:*} im Basisimage nicht gefunden" >&2; exit 1; }
  done
  printf '%s::%s:20::0:0:%s:%s:/bin/zsh\n' "$CLOKE_USER" "$CLOKE_USER_UID" "$CLOKE_USER_NAME" "$home" \
    >> "$tmp/stage/etc/master.passwd"
  printf '%s:*:%s:20:%s:%s:/bin/zsh\n' "$CLOKE_USER" "$CLOKE_USER_UID" "$CLOKE_USER_NAME" "$home" \
    >> "$tmp/stage/etc/passwd"
  sed -i '' "s/^staff:\(.*\)$/staff:\1,$CLOKE_USER/" "$tmp/stage/etc/group"
  # wie bei macOS nur für root lesbar; die übrigen Programme lesen /etc/passwd
  chmod 600 "$tmp/stage/etc/master.passwd"

  h="$tmp/stage$home"
  mkdir -p "$h"/{Desktop,Dokumente,Downloads,Bilder,Musik,.Trash,.config/gtk-3.0}
  cp "$HERE/overlay-desktop/var/root/.config/gtk-3.0/gtk.css" "$h/.config/gtk-3.0/"
  for f in profile zprofile zshrc; do printf 'test -r /etc/%s && . /etc/%s\n' $f $f > "$h/.$f"; done
  printf "alias ll='ls -la'\n" >> "$h/.zshrc"
  cat > "$h/Desktop/Willkommen.txt" <<EOF
Willkommen bei ClokeOS $CLOKE_VERSION „$CLOKE_CODENAME“

ClokeOS ist eine macOS-ähnliche Oberfläche auf Darwin-Basis: der Kernel XNU,
darunter PureDarwin $PD_VERSION, darüber CloakServer als Fensterserver.
Ein paar Dinge zum Ausprobieren:

• Im Terminal: clokefetch, uname -a, sw_vers
• Fenster ziehen, an der Ecke vergrößern, gelb = ins Dock
• Erscheinungsbild in der Menüleiste: automatisch, hell oder dunkel
• Abmelden im ClokeOS-Menü

Dieses Konto hat kein Passwort. Für Verwaltungsaufgaben meldest du dich als
System Administrator an; su und sudo gibt es in ClokeOS noch nicht.
EOF
  # Besitzer und Rechte im Image: alles unter $home gehört dem Konto
  {
    echo "set_inode_field /Users mode 040755"
    (cd "$tmp/stage" && find ".$home" | sed 's|^\.||' | sort) | while read -r f; do
      [ -d "$tmp/stage$f" ] && echo "set_inode_field $f mode 040755"
      echo "set_inode_field $f uid $CLOKE_USER_UID"
      echo "set_inode_field $f gid 20"
    done
    echo "set_inode_field $home/.Trash mode 040700"
  } >> "$tmp/extra.cmds"
fi

# --- in ein ext4-Dateisystem schreiben ----------------------------------------------------
# brand_ext4 <ext4-datei> <volume-name|"">: Overlay hinein, prüfen, ob alles angekommen ist.
brand_ext4() {
  local fs="$1" label="$2" cmds="$tmp/debugfs.cmds" f
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
    # Symlinks (Framework-Bundles: Versions/Current, Resources, die Programmdatei)
    (cd "$tmp/stage" && find . -type l | sed 's|^\.||' | sort) | while read -r f; do
      echo "rm $f"
      echo "symlink $f $(readlink "$tmp/stage$f")"
    done
    cat "$tmp/extra.cmds"
  } > "$cmds"
  "$DEBUGFS" -w -f "$cmds" "$fs" > "$tmp/debugfs.log" 2>&1
  [ -n "$label" ] && "$TUNE2FS" -L "$label" "$fs" >/dev/null
  "$E2FSCK" -fn "$fs" > "$tmp/e2fsck.log" 2>&1 || { cat "$tmp/e2fsck.log" >&2; exit 1; }

  for f in $(cd "$tmp/stage" && find . -type f | sed 's|^\.||'); do
    if ! "$DEBUGFS" -R "cat $f" "$fs" 2>/dev/null | cmp -s - "$tmp/stage$f"; then
      echo "Nicht geschrieben: $f in $fs (siehe $tmp/debugfs.log)" >&2
      exit 1
    fi
  done
  for f in $(cd "$tmp/stage" && find . -type l | sed 's|^\.||'); do
    if ! "$DEBUGFS" -R "stat $f" "$fs" 2>/dev/null | grep -qF "Fast link dest: \"$(readlink "$tmp/stage$f")\""; then
      echo "Symlink nicht geschrieben: $f in $fs (siehe $tmp/debugfs.log)" >&2
      exit 1
    fi
  done
}

brand_ext4 "$root" "Cloke HD"
dd if="$root" of="$tmp/image.img" bs=512 seek="$root_start" conv=notrunc status=none

# --- EFI-Partition ------------------------------------------------------------------------
read -r esp_start _ < <(gpt_part "$tmp/image.img" "$GUID_ESP")
esp="$tmp/image.img@@$((esp_start * 512))"

# Manche PureDarwin-Images (arm64-Minimal) booten eine RAM-Disk statt der Wurzelpartition.
if mdir -i "$esp" ::/ramdisk.img >/dev/null 2>&1; then
  mcopy -n -i "$esp" ::/ramdisk.img "$tmp/ramdisk.ext4"
  brand_ext4 "$tmp/ramdisk.ext4" ""
  mcopy -o -i "$esp" "$tmp/ramdisk.ext4" ::/ramdisk.img
  mtype -i "$esp" ::/ramdisk.img | cmp -s - "$tmp/ramdisk.ext4" ||
    { echo "RAM-Disk nicht in der EFI-Partition angekommen" >&2; exit 1; }
fi

if [ -n "$LOADER_EFI" ]; then
  mcopy -o -i "$esp" "$LOADER_EFI" "::/EFI/BOOT/$EFI_BINARY"
  mtype -i "$esp" "::/EFI/BOOT/$EFI_BINARY" | cmp -s - "$LOADER_EFI" ||
    { echo "Loader nicht in der EFI-Partition angekommen" >&2; exit 1; }
fi
if [ "$CLOKE_ARCH" = x86_64 ]; then
  # PureDarwins Grafiktreiber legt /dev/fb0 und Xorgs Bild ein Byte neben den Framebuffer
  # (falsche Farben). kcfix.py korrigiert das im Kernel Collection.
  mcopy -n -i "$esp" ::/EFI/BOOT/kernel "$tmp/kernel"
  python3 -I "$HERE/kcfix.py" "$tmp/kernel"
  mcopy -o -i "$esp" "$tmp/kernel" ::/EFI/BOOT/kernel
  mtype -i "$esp" ::/EFI/BOOT/kernel | cmp -s - "$tmp/kernel" ||
    { echo "Kernel nicht in der EFI-Partition angekommen" >&2; exit 1; }
fi
if [ -n "$CLOKE_BOOT_ARGS" ]; then
  printf '%s\n' "$CLOKE_BOOT_ARGS" > "$tmp/boot-args.txt"
  mcopy -o -i "$esp" "$tmp/boot-args.txt" ::/EFI/BOOT/boot-args.txt
  mtype -i "$esp" ::/EFI/BOOT/boot-args.txt | cmp -s - "$tmp/boot-args.txt" ||
    { echo "boot-args.txt nicht in der EFI-Partition angekommen" >&2; exit 1; }
fi

mv "$tmp/image.img" "$CLOKE_IMG"
rm -rf "$tmp"
echo "Fertig: $CLOKE_IMG"
env=""
[ "$CLOKE_ARCH" = x86_64 ] || env+="CLOKEOS_ARCH=$CLOKE_ARCH "
[ "$CLOKE_FLAVOR" = minimal ] || env+="CLOKEOS_FLAVOR=$CLOKE_FLAVOR "
echo "Starten mit: $env$HERE/run.sh"
