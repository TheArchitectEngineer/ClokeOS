#!/bin/bash
# Startet ClokeOS (oder ein anderes PureDarwin-Image) in QEMU.
#
#   ./run.sh                   ClokeOS-Image: Fenster mit dem Bildschirm, serielle Konsole in diesem Terminal
#   ./run.sh --headless        ohne Fenster; Konsole über ./vmctl.py, Mitschnitt in ~/ClokeOS-work/vm/<arch>/serial.log
#   ./run.sh --image <datei>   anderes Image starten (z. B. das unveränderte PureDarwin)
#   ./run.sh --snapshot        Änderungen am Image beim Beenden verwerfen
#   ./run.sh --hvf             nur arm64: Apples Hypervisor statt Emulation (noch nicht ausprobiert)
#
# Architektur wählen mit CLOKEOS_ARCH=arm64 (Standard: x86_64). Auf einem Mac mit Apple-Chip wird
# x86_64 emuliert (QEMU TCG); bis zur Eingabeaufforderung dauert es etwa 75 bis 90 Sekunden.
# Beenden: poweroff in der VM, im Terminal Strg+A und dann X, oder ./vmctl.py quit
set -euo pipefail
source "$(dirname "$0")/config.sh"
need "$QEMU"

image="$CLOKE_IMG"
headless=0
snapshot=""
accel=tcg
while [ $# -gt 0 ]; do
  case "$1" in
    --headless) headless=1 ;;
    --image) image="$2"; shift ;;
    --snapshot) snapshot=",snapshot=on" ;;
    --hvf) accel=hvf ;;
    --) shift; break ;;
    *) break ;;
  esac
  shift
done

if [ ! -f "$image" ]; then
  echo "Kein Image: $image – zuerst ./brand.sh ausführen." >&2
  exit 1
fi
[ -w "$image" ] || snapshot=",snapshot=on"

mkdir -p "$VM_STATE"
if [ ! -f "$VM_STATE/efi-vars.fd" ]; then
  cp "$FW_VARS" "$VM_STATE/efi-vars.fd"
  chmod u+w "$VM_STATE/efi-vars.fd"
fi
rm -f "$VM_STATE/qmp.sock" "$VM_STATE/serial.sock"

common=(
  -m "${CLOKEOS_VM_MEMORY:-4096}"
  -drive if=pflash,format=raw,unit=0,readonly=on,file="$FW_CODE"
  -drive if=pflash,format=raw,unit=1,file="$VM_STATE/efi-vars.fd"
  -device qemu-xhci,id=xhci
  -device usb-kbd,bus=xhci.0
  -name "$CLOKE_NAME"
  -qmp unix:"$VM_STATE/qmp.sock",server=on,wait=off
  -no-reboot
)

if [ "$CLOKE_ARCH" = arm64 ]; then
  # Wie PureDarwins Runner für arm64 (nix run .#vm-arm64-virt).
  cpu=neoverse-n2
  [ "$accel" = hvf ] && cpu=host
  args=(
    -M virt,gic-version=3
    -accel "$accel"
    -cpu "$cpu"
    -smp "${CLOKEOS_VM_SMP:-4}"
    "${common[@]}"
    -drive if=none,id=system,format=raw,file="$image$snapshot"
    -device virtio-blk-pci,drive=system,bootindex=1
    -device ramfb
    -device usb-mouse,bus=xhci.0
    -nic user,model=virtio-net-pci
  )
else
  # Wie PureDarwins Runner (nix run .#vm), nur ohne KVM. Netzwerkkarte: PureDarwins e1000-Treiber
  # (PDE1000) erkennt nur den 82545EM (8086:100f), nicht QEMUs Standardmodelle e1000/e1000e.
  # Maus: PureDarwins HID-Treiber kennt nur Boot-Protokoll-Mäuse, also usb-mouse statt usb-tablet
  # (im QEMU-Fenster fängt ein Klick die Maus ein, Strg+Alt+G gibt sie wieder frei).
  args=(
    -M q35
    -accel tcg
    -cpu IvyBridge,vendor=GenuineIntel
    -smp "${CLOKEOS_VM_SMP:-2}"
    -vga std
    -fw_cfg name=opt/ovmf/X-PciMmio64Mb,string=2048
    "${common[@]}"
    -drive id=root,format=raw,file="$image$snapshot"
    -device usb-mouse,bus=xhci.0
    -nic user,model=e1000-82545em
  )
fi

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
