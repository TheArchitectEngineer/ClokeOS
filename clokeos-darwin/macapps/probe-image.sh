#!/bin/bash
# Legt eine Testkopie des ClokeOS-Desktop-Images an und schreibt die Foundation-Probe hinein.
#
#   macapps/probe-image.sh            -> ~/ClokeOS-work/tmp/fprobe-desktop.img
#   ./run.sh --headless --image ~/ClokeOS-work/tmp/fprobe-desktop.img
#   ./vmctl.py send '/Library/ClokeOS/Probe/probe.sh' 300
#
# Die Kopie ist ein APFS-Klon (cp -c) und kostet kaum Platz; das eigentliche Image bleibt
# unverändert. Im Gast landen
#   /Library/ClokeOS/MacFrameworks/{CoreFoundation,Foundation}.framework   (aus foundation.sh)
#   /Library/ClokeOS/Probe/fprobe     das Prüfprogramm (x86_64, Apples SDK)
#   /Library/ClokeOS/Probe/probe.sh   startet es mit DYLD_FRAMEWORK_PATH
set -euo pipefail
source "$(dirname "$0")/../config.sh"
CLOKEOS_FLAVOR=desktop
source "$(dirname "$0")/../config.sh"
need "$DEBUGFS"

out="$WORK/out/macapps"
src="$out/root/Library/ClokeOS"
img="$WORK/tmp/fprobe-desktop.img"
[ -f "$CLOKE_IMG" ] || { echo "Fehlt: $CLOKE_IMG (CLOKEOS_FLAVOR=desktop ./brand.sh)" >&2; exit 1; }
[ -f "$src/Probe/fprobe" ] || { echo "Fehlt: $src/Probe/fprobe (macapps/foundation.sh und Prüfprogramm bauen)" >&2; exit 1; }

cat > "$src/Probe/probe.sh" <<'EOF'
#!/bin/zsh
# Foundation-Probe mit Darlings Frameworks statt der fehlenden Apple-Frameworks
export DYLD_FRAMEWORK_PATH=/Library/ClokeOS/MacFrameworks
cd /tmp
/Library/ClokeOS/Probe/fprobe "$@"
echo "fprobe-Ende: $?"
EOF
chmod +x "$src/Probe/probe.sh"

mkdir -p "$(dirname "$img")"
rm -f "$img"
cp -c "$CLOKE_IMG" "$img"
read -r root_start _ < <(gpt_part "$img" "$GUID_LINUX_FS")
fs="$img?offset=$((root_start * 512))"

# debugfs-Befehle: Ordner, Dateien (root:wheel), Symlinks der Framework-Bundles
cmds="$WORK/tmp/fprobe.cmds"
{
  for d in /Library /Library/ClokeOS /Library/ClokeOS/Probe /Library/ClokeOS/MacFrameworks; do echo "mkdir $d"; done
  (cd "$src" && find . -mindepth 1 -type d | sort) | while read -r d; do echo "mkdir /Library/ClokeOS/${d#./}"; done
  (cd "$src" && find . -type f | sort) | while read -r f; do
    g="/Library/ClokeOS/${f#./}"
    echo "write $src/${f#./} $g"
    mode=0100644; [ -x "$src/${f#./}" ] && mode=0100755
    echo "sif $g mode $mode"; echo "sif $g uid 0"; echo "sif $g gid 0"
  done
  (cd "$src" && find . -type l | sort) | while read -r l; do
    echo "symlink /Library/ClokeOS/${l#./} $(readlink "$src/${l#./}")"
  done
} > "$cmds"
"$DEBUGFS" -w -f "$cmds" "$fs" 2>&1 | grep -viE "^debugfs [0-9]|^debugfs: (mkdir|write|sif|symlink) |Allocated inode" | grep -v "File exists" || true
"$DEBUGFS" -R "ls -l /Library/ClokeOS/MacFrameworks/Foundation.framework" "$fs" 2>/dev/null
echo "Fertig: $img"
