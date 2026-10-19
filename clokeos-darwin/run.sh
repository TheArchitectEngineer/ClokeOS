#!/bin/bash
# Startet ClokeOS (oder ein anderes PureDarwin-Image) in QEMU.
#
#   ./run.sh                   ClokeOS-Image: Fenster mit dem Bildschirm, serielle Konsole in diesem Terminal
#   ./run.sh --headless        ohne Fenster; Konsole über ./vmctl.py, Mitschnitt in ~/ClokeOS-work/vm/serial.log
#   ./run.sh --image <datei>   anderes Image starten (z. B. das unveränderte PureDarwin)
#   ./run.sh --snapshot        Änderungen am Image beim Beenden verwerfen
#
# Auf einem Mac mit Apple-Chip wird x86_64 emuliert (QEMU TCG); der Start dauert deshalb einige Minuten.
# Beenden: im Terminal Strg+A, dann X – oder ./vmctl.py quit
set -euo pipefail
source "$(dirname "$0")/config.sh"
need "$QEMU"

image="$CLOKE_IMG"
headless=0
snapshot=""
while [ $# -gt 0 ]; do
  case "$1" in
    --headless) headless=1 ;;
    --image) image="$2"; shift ;;
    --snapshot) snapshot=",snapshot=on" ;;
    --) shift; break ;;
    *) break ;;
  esac
  shift
done

if [ ! -f "$image" ]; then
  echo "Kein Image: $image – zuerst ./fetch.sh und ./brand.sh ausführen." >&2
  exit 1
fi
[ -w "$image" ] || snapshot=",snapshot=on"

mkdir -p "$VM_STATE"
if [ ! -f "$VM_STATE/OVMF_VARS.fd" ]; then
  cp "$OVMF_VARS" "$VM_STATE/OVMF_VARS.fd"
  chmod u+w "$VM_STATE/OVMF_VARS.fd"
fi
rm -f "$VM_STATE/qmp.sock" "$VM_STATE/serial.sock"

# Dieselbe Maschine wie PureDarwins eigener Runner (nix run .#vm), nur ohne KVM.
args=(
  -M q35
  -accel tcg
  -m "${CLOKEOS_VM_MEMORY:-4096}"
  -smp "${CLOKEOS_VM_SMP:-2}"
  -vga std
  -cpu IvyBridge,vendor=GenuineIntel
  -fw_cfg name=opt/ovmf/X-PciMmio64Mb,string=2048
  -drive if=pflash,format=raw,unit=0,readonly=on,file="$OVMF_CODE"
  -drive if=pflash,format=raw,unit=1,file="$VM_STATE/OVMF_VARS.fd"
  -drive id=root,format=raw,file="$image$snapshot"
  -device qemu-xhci,id=xhci
  -device usb-kbd,bus=xhci.0
  -device usb-tablet,bus=xhci.0
  -name "$CLOKE_NAME"
  -qmp unix:"$VM_STATE/qmp.sock",server=on,wait=off
  -no-reboot
)

if [ "$headless" = 1 ]; then
  : > "$VM_STATE/serial.log"
  args+=(
    -display none
    -chardev socket,id=con,path="$VM_STATE/serial.sock",server=on,wait=off,logfile="$VM_STATE/serial.log"
    -serial chardev:con
  )
else
  args+=(-serial mon:stdio)
fi

exec "$QEMU" "${args[@]}" "$@"
