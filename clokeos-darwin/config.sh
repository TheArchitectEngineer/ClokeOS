# Gemeinsame Einstellungen für die ClokeOS-Skripte (wird per "source" geladen).

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Große Dateien (Downloads, gebaute Images, VM-Zustand) liegen außerhalb des Projektordners.
WORK="${CLOKEOS_WORK:-$HOME/ClokeOS-work}"

# Basis: PureDarwin 20.5, minimales x86_64-Image (UEFI, xnu-loader, ext4-Wurzel).
PD_VERSION="20.5"
PD_URL="https://github.com/PureDarwin/PureDarwin/releases/download/$PD_VERSION/puredarwin-minimal.img"
PD_SHA256="ef65a816395680ef9210f401cebe3783c131badca1a942cb80e56bec97e717ac"
PD_IMG="$WORK/download/pd-$PD_VERSION/puredarwin-minimal.img"

# Bootloader: PureDarwins xnu-loader (der Stand aus PureDarwin 20.5) mit ClokeOS-Bootlogo,
# gebaut mit mingw-w64 gegen gnu-efi (die Version aus PureDarwins Nix-Build). Siehe loader/.
XNU_LOADER_REV="8295cd89c184785896d007d0554b398d22d8e32f"
XNU_LOADER_URL="https://codeload.github.com/PureDarwin/xnu-loader/tar.gz/$XNU_LOADER_REV"
XNU_LOADER_SHA256="625c13d7302de58519b8c9e25ae10e46cca816ca3a42abe00f47f4ab1668a92a"
XNU_LOADER_TGZ="$WORK/download/xnu-loader/xnu-loader-$XNU_LOADER_REV.tar.gz"
GNU_EFI_VERSION="4.0.2"
GNU_EFI_URL="https://codeload.github.com/ncroxon/gnu-efi/tar.gz/refs/tags/$GNU_EFI_VERSION"
GNU_EFI_SHA256="f31488c3fc7d64257ec8fcad470ec75a936a4db77350fabaf043c52a7acbae72"
GNU_EFI_TGZ="$WORK/download/gnu-efi/gnu-efi-$GNU_EFI_VERSION.tar.gz"
LOADER_EFI="$WORK/out/BOOTX64.EFI"

# Kernel-Argumente (EFI/BOOT/boot-args.txt). Ohne -v zeigt der Loader das Bootlogo und gibt dem
# Kernel keinen Framebuffer; Kernel-Log und Shell laufen dann nur über die serielle Konsole
# (serial=3). Mit -v vorn dran sieht der Bildschirm wieder aus wie bei PureDarwin.
CLOKE_BOOT_ARGS="debug=0x218 -nogzalloc_mode keepsyms=1 serial=3 gopconsole=1 gen9_debug=1"

# ClokeOS-Version, passend zur Oberfläche in clokeos.html (OS-Objekt).
CLOKE_NAME="ClokeOS"
CLOKE_VERSION="2.0"
CLOKE_CODENAME="Presidio"
CLOKE_BUILD="2A17"
CLOKE_HOST="clokeos"
CLOKE_IMG="$WORK/out/clokeos-$CLOKE_VERSION-x86_64.img"

# Laufzeitdateien der VM (UEFI-Variablen, Sockets, Logs).
VM_STATE="$WORK/vm"

# Homebrew-Werkzeuge
BREW="$(brew --prefix 2>/dev/null || echo /opt/homebrew)"
DEBUGFS="$BREW/opt/e2fsprogs/sbin/debugfs"
E2FSCK="$BREW/opt/e2fsprogs/sbin/e2fsck"
TUNE2FS="$BREW/opt/e2fsprogs/sbin/tune2fs"
QEMU="$BREW/bin/qemu-system-x86_64"
OVMF_CODE="$BREW/share/qemu/edk2-x86_64-code.fd"
OVMF_VARS="$BREW/share/qemu/edk2-i386-vars.fd"
MINGW_CC="$BREW/bin/x86_64-w64-mingw32-gcc"

need() {
  for tool in "$@"; do
    if [ ! -x "$tool" ] && ! command -v "$tool" >/dev/null 2>&1; then
      echo "Fehlt: $tool – installieren mit: brew install qemu e2fsprogs mtools mingw-w64" >&2
      exit 1
    fi
  done
}

# Lädt eine Datei, falls sie fehlt oder nicht stimmt, und prüft die SHA-256-Prüfsumme.
fetch_file() {
  local url="$1" sha="$2" dest="$3"
  if [ -f "$dest" ] && [ "$(shasum -a 256 "$dest" | cut -d' ' -f1)" = "$sha" ]; then
    echo "Schon da: $dest"
    return 0
  fi
  mkdir -p "$(dirname "$dest")"
  echo "Lade $url"
  curl -fL --retry 5 -o "$dest.part" "$url"
  local sum
  sum="$(shasum -a 256 "$dest.part" | cut -d' ' -f1)"
  if [ "$sum" != "$sha" ]; then
    echo "Prüfsumme stimmt nicht für $dest: $sum (erwartet $sha)" >&2
    rm -f "$dest.part"
    exit 1
  fi
  mv "$dest.part" "$dest"
  echo "OK: $dest"
}

# Startsektor und Länge (in 512-Byte-Sektoren) einer GPT-Partition nach Typ-GUID.
# Liest die GPT direkt, damit kein sgdisk/sfdisk nötig ist.
gpt_part() {
  python3 -I - "$1" "$2" <<'PY'
import struct, sys, uuid
img, want = sys.argv[1], uuid.UUID(sys.argv[2])
with open(img, 'rb') as f:
    f.seek(512)
    hdr = f.read(92)
    if hdr[:8] != b'EFI PART':
        sys.exit('keine GPT in ' + img)
    lba, count, size = struct.unpack_from('<QII', hdr, 72)
    f.seek(lba * 512)
    for _ in range(count):
        e = f.read(size)
        if uuid.UUID(bytes_le=e[:16]) == want:
            first, last = struct.unpack_from('<QQ', e, 32)
            print(first, last - first + 1)
            sys.exit(0)
sys.exit('Partition %s nicht gefunden' % want)
PY
}
GUID_ESP="C12A7328-F81F-11D2-BA4B-00A0C93EC93B"
GUID_LINUX_FS="0FC63DAF-8483-4772-8E79-3D69D8477DE4"
