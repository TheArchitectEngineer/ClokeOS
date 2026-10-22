#!/bin/bash
# Startet ClokeOS (oder ein anderes PureDarwin-Image) in QEMU.
#
#   ./run.sh                   ClokeOS-Image: Fenster mit dem Bildschirm, serielle Konsole in diesem Terminal
#   ./run.sh --headless        ohne Fenster; Konsole über ./vmctl.py, Mitschnitt in ~/ClokeOS-work/vm/<arch>/serial.log
#   ./run.sh --image <datei>   anderes Image starten (z. B. das unveränderte PureDarwin)
#   ./run.sh --snapshot        Änderungen am Image beim Beenden verwerfen
#   ./run.sh --tcg             nur arm64: emulieren statt Apples Hypervisor (HVF) zu nutzen
#
# Architektur wählen mit CLOKEOS_ARCH=arm64 (Standard: x86_64). Auf einem Mac mit Apple-Chip läuft
# arm64 mit HVF fast ohne Verlust (Eingabeaufforderung nach etwa 6 Sekunden), x86_64 wird emuliert
# (QEMU TCG, etwa 75 bis 90 Sekunden). PureDarwin nutzt auf beiden nur einen Prozessorkern, deshalb
# bekommt die VM einen; CLOKEOS_VM_SMP ändert das.
# Beenden: poweroff in der VM, im Terminal Strg+A und dann X, oder ./vmctl.py quit
set -euo pipefail
source "$(dirname "$0")/config.sh"
need "$QEMU"

image="$CLOKE_IMG"
headless=0
snapshot=""
# HVF gibt es nur für arm64-Gäste auf einem Mac mit Apple-Chip.
accel=tcg
[ "$CLOKE_ARCH" = arm64 ] && [ "$(uname -m)" = arm64 ] && [ "$(sysctl -n kern.hv_support 2>/dev/null)" = 1 ] && accel=hvf
while [ $# -gt 0 ]; do
  case "$1" in
    --headless) headless=1 ;;
    --image) image="$2"; shift ;;
    --snapshot) snapshot=",snapshot=on" ;;
    --tcg) accel=tcg ;;
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
  # Bild: ramfb genügt für Loader und Kernel-Konsole. Xorg braucht dagegen einen Grafiktreiber,
  # und auf QEMUs virt-Maschine gibt es bei PureDarwin nur IOVirtIOGPU: IOGOPFramebuffer hängt sich
  # auf arm64 nur an einen "framebuffer"-Knoten, den PureDarwin bloß für den Raspberry Pi anlegt.
  # Der Desktop bekommt deshalb virtio-gpu (1280 x 800 wie bei x86_64); die Firmware zeigt darüber
  # auch Loader und Kernel-Konsole.
  gpu=ramfb
  [ "$CLOKE_FLAVOR" = desktop ] && gpu=virtio-gpu-pci,xres=1280,yres=800
  args=(
    -M virt,gic-version=3
    -accel "$accel"
    -cpu "$cpu"
    -smp "${CLOKEOS_VM_SMP:-1}"
    "${common[@]}"
    -drive if=none,id=system,format=raw,file="$image$snapshot"
    -device virtio-blk-pci,drive=system,bootindex=1
    -device "$gpu"
    -device usb-mouse,bus=xhci.0
    -nic user,model=virtio-net-pci
  )
else
  # Wie PureDarwins Runner (nix run .#vm), nur ohne KVM. Platte und Netz sind virtio: Das kostet
  # unter Emulation weniger als eine AHCI-Platte und eine Intel-Karte, die QEMU Register für
  # Register nachbildet (Start 66 statt 71 Sekunden), und PureDarwin hat für beides Treiber.
  # CLOKEOS_VM_DEVICES=classic nimmt stattdessen AHCI und die Intel-Karte 82545EM (8086:100f),
  # die einzige, die PureDarwins e1000-Treiber erkennt.
  # Maus: PureDarwins HID-Treiber kennt nur Boot-Protokoll-Mäuse, also usb-mouse statt usb-tablet
  # (im QEMU-Fenster fängt ein Klick die Maus ein, Strg+Alt+G gibt sie wieder frei).
  args=(
    -M q35
    -accel tcg
    -cpu IvyBridge,vendor=GenuineIntel
    -smp "${CLOKEOS_VM_SMP:-1}"
    -vga std
    -fw_cfg name=opt/ovmf/X-PciMmio64Mb,string=2048
    "${common[@]}"
    -device usb-mouse,bus=xhci.0
  )
  if [ "${CLOKEOS_VM_DEVICES:-virtio}" = classic ]; then
    args+=(-drive id=root,format=raw,file="$image$snapshot" -nic user,model=e1000-82545em)
  else
    args+=(
      -drive if=none,id=system,format=raw,file="$image$snapshot"
      -device virtio-blk-pci,drive=system,bootindex=1
      -nic user,model=virtio-net-pci
    )
  fi
fi

: > "$VM_STATE/serial.log"
if [ "$headless" = 1 ]; then
  args+=(
    -display none
    -chardev socket,id=con,path="$VM_STATE/serial.sock",server=on,wait=off,logfile="$VM_STATE/serial.log"
    -serial chardev:con
  )
else
  # wie -serial mon:stdio (Strg+A X beendet, Strg+C geht an die VM), aber mit Mitschnitt
  args+=(
    -chardev stdio,id=con,mux=on,signal=off,logfile="$VM_STATE/serial.log"
    -serial chardev:con
    -mon chardev=con,mode=readline
  )
fi

if [ "$CLOKE_ARCH" = arm64 ]; then
  # PureDarwins arm64-Kernel kann die VM nicht ausschalten: Nach reboot(RB_HALT) hängt er schon
  # beim Abmelden der Treiber, noch bevor er die Platten synchronisiert, bis sein Wachhund nach
  # 30 Sekunden mit "Halt/Restart Timed Out" in Panik geht. poweroff in der ClokeOS-zsh ruft vorher
  # sync auf. run.sh schaltet QEMU deshalb 5 Sekunden nach reboot(RB_HALT) aus (oder bei der Panik).
  (
    halt=0
    while kill -0 $$ 2>/dev/null; do
      if grep -a -q 'Halt/Restart Timed Out' "$VM_STATE/serial.log" 2>/dev/null; then halt=5; fi
      if [ "$halt" -ge 5 ]; then
        CLOKEOS_ARCH="$CLOKE_ARCH" python3 -I "$HERE/vmctl.py" quit >/dev/null 2>&1
        break
      fi
      grep -a -q 'About to call: reboot(RB_HALT)' "$VM_STATE/serial.log" 2>/dev/null && halt=$((halt + 1))
      sleep 1
    done
  ) &
fi

"$QEMU" "${args[@]}" "$@"
