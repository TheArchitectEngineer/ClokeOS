#!/bin/bash
# Schreibt einen Ordner (z. B. dlopen-Testprogramm und dünn geschnittene Mach-O-Dateien) in eine
# Testkopie des Desktop-Images, nach /Library/ClokeOS/<ZIEL>.
#
#   macapps/dlprobe.sh <image-klon> <ordner> [ZIEL]     (ZIEL: Standard Dlprobe;
#   "/" legt den Ordner wie eine Stage über die Wurzel, vorhandene Dateien werden ersetzt)
#
# Das Image muss ein eigener Klon sein (cp -c); die Dateien gehören root:wheel, Ausführungsrechte
# bleiben erhalten, Symlinks werden übernommen.
set -euo pipefail
source "$(dirname "$0")/../config.sh"
need "$DEBUGFS"
img="$1"; src="$(cd "$2" && pwd)"; dst="/Library/ClokeOS/${3:-Dlprobe}"; [ "${3:-}" = / ] && dst=""
read -r root_start _ < <(gpt_part "$img" "$GUID_LINUX_FS")
fs="$img?offset=$((root_start * 512))"
cmds="$(mktemp)"
{
  [ -n "$dst" ] && { echo "mkdir /Library"; echo "mkdir /Library/ClokeOS"; echo "mkdir $dst"; }
  (cd "$src" && find . -mindepth 1 -type d | sort) | while read -r d; do echo "mkdir $dst/${d#./}"; done
  (cd "$src" && find . -type f | sort) | while read -r f; do
    g="$dst/${f#./}"
    echo "rm $g"; echo "write $src/${f#./} $g"
    mode=0100644; [ -x "$src/${f#./}" ] && mode=0100755
    echo "sif $g mode $mode"; echo "sif $g uid 0"; echo "sif $g gid 0"
  done
  (cd "$src" && find . -type l | sort) | while read -r l; do echo "rm $dst/${l#./}"; echo "symlink $dst/${l#./} $(readlink "$src/${l#./}")"; done
} > "$cmds"
"$DEBUGFS" -w -f "$cmds" "$fs" 2>&1 | grep -viE "^debugfs [0-9]|^debugfs: |Allocated inode|File exists|File not found" || true
rm -f "$cmds"
echo "Fertig: $dst in $img"
