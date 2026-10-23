#!/bin/bash
# Online-Release für Installer und Wiederherstellung, wie macOS es aus dem Internet lädt.
#
#   recovery/release.sh [--upload] [x86_64] [arm64]     ohne Architektur: beide
#
# Baut je Architektur die Desktop-Variante (brand.sh) und daraus Wiederherstellung und
# Partitionsabbilder (recovery/build.sh) frisch, packt sie mit xz und legt alles unter festen Namen
# nach ~/ClokeOS-work/dist/online-<version>/. Die Wiederherstellung lädt sie über
# https://github.com/<CLOKE_RELEASES_REPO>/releases/latest/download/<name>:
#   clokeos-<arch>.txt                Beschreibung: Version, je Abbild Name, Größe, gepackte Größe, SHA-256
#   clokeos-<arch>-esp.img.xz         EFI-Partition
#   clokeos-<arch>-system.ext4.xz     System
#   clokeos-<arch>-recovery.ext4.xz   Wiederherstellung
#   clokeos-<arch>-installer.img.xz   startfähiges Installationsmedium für VMs (QEMU)
#   clokeos-x86_64-usb-offline.img.xz USB-Stick für echte PCs, bringt das System mit (recovery/usb.sh)
#   clokeos-x86_64-usb-online.img.xz  kleiner USB-Stick, lädt das System aus dem Internet
#   clokeos-x86_64-disk.img.xz        fertig installierte SSD zum Draufschreiben (recovery/disk.sh)
#   clokeos-<version>-source.tar.gz   Quellcode dieses Commits und der mitgebauten Fremdquellen (GPL)
#   SHA256SUMS, RELEASE-NOTES.md
#
# --upload legt das Release v<version> im öffentlichen Repo an und veröffentlicht es: Nur ein
# veröffentlichtes Release ist "latest", nur dann findet die Wiederherstellung es. Wie release.sh
# nur mit sauberem, gepushtem Stand; parallel arbeitende Sitzungen lassen offene Änderungen liegen,
# dann aus einem eigenen git worktree mit eigenem CLOKEOS_WORK bauen (siehe README).
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
upload=0
arches=()
for a in "$@"; do
  case "$a" in
    --upload) upload=1 ;;
    x86_64|arm64) arches+=("$a") ;;
    *) sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 2 ;;
  esac
done
[ ${#arches[@]} -gt 0 ] || arches=(x86_64 arm64)

export CLOKEOS_FLAVOR=desktop
source "$here/config.sh"
need xz shasum git
repo_dir="$(cd "$here/.." && pwd)"
commit="$(git -C "$repo_dir" rev-parse HEAD)"
tag="v$CLOKE_VERSION"
if [ "$upload" = 1 ]; then
  need gh
  [ -z "$(git -C "$repo_dir" status --porcelain)" ] || { echo "Nicht eingecheckte Änderungen im Repo." >&2; exit 1; }
  git -C "$repo_dir" fetch -q origin
  git -C "$repo_dir" branch -r --contains "$commit" | grep -q . || { echo "Commit $commit ist nicht gepusht." >&2; exit 1; }
  if gh release view "$tag" -R "$CLOKE_RELEASES_REPO" >/dev/null 2>&1; then
    echo "$tag gibt es in $CLOKE_RELEASES_REPO schon. CLOKE_VERSION in config.sh erhöhen." >&2
    exit 1
  fi
fi

dist="$WORK/dist/online-$CLOKE_VERSION"
rm -rf "$dist"
mkdir -p "$dist"
rows=""
for arch in "${arches[@]}"; do
  CLOKEOS_ARCH="$arch" "$here/brand.sh" | tail -1
  CLOKEOS_ARCH="$arch" "$here/recovery/build.sh" | tail -1
  src="$WORK/out/recovery-$arch"
  man="$dist/clokeos-$arch.txt"
  {
    echo "version=$CLOKE_VERSION"
    echo "build=$CLOKE_BUILD"
    echo "codename=$CLOKE_CODENAME"
    echo "arch=$arch"
  } > "$man"
  for part in esp:esp.img system:system.ext4 recovery:recovery.ext4 installer:installer.img; do
    key="${part%%:*}" file="${part#*:}"
    name="clokeos-$arch-$key.${file#*.}.xz"
    echo "Packe $name"
    xz -T0 -6 -c "$src/$file" > "$dist/$name"
    raw=$(stat -f %z "$src/$file")
    packed=$(stat -f %z "$dist/$name")
    sum=$(shasum -a 256 "$dist/$name" | cut -d' ' -f1)
    [ "$key" = installer ] || echo "$key=$name $raw $packed $sum" >> "$man"
    rows+="| \`$name\` | $arch | $(( packed / 1048576 )) MB | $(( raw / 1048576 )) MB |"$'\n'
  done
  if [ "$arch" = x86_64 ]; then # USB-Stick (mit den eben gepackten Abbildern) und SSD-Abbild für echte PCs
    CLOKEOS_ARCH="$arch" "$here/recovery/usb.sh" offline "$dist" | tail -1
    CLOKEOS_ARCH="$arch" "$here/recovery/usb.sh" online | tail -1
    CLOKEOS_ARCH="$arch" "$here/recovery/disk.sh" | tail -1
    for k in usb-offline usb-online disk; do
      name="clokeos-$arch-$k.img.xz"
      echo "Packe $name"
      xz -T0 -6 -c "$src/$k.img" > "$dist/$name"
      rows+="| \`$name\` | $arch | $(( $(stat -f %z "$dist/$name") / 1048576 )) MB | $(( $(stat -f %z "$src/$k.img") / 1048576 )) MB |"$'\n'
      [ "$k" = disk ] && rm -f "$src/$k.img" # gut 5 GB, gepackt reicht es
    done
  fi
  # CLOKEOS_RELEASE_PRUNE=1: große Zwischendateien nach dem Packen löschen (wenig Platz auf der Platte);
  # das Desktop-Image von arm64 bleibt für Bildschirmfotos
  if [ "${CLOKEOS_RELEASE_PRUNE:-0}" = 1 ] && [ "$arch" = x86_64 ]; then
    rm -f "$src/system.ext4" "$src/installer.img" "$src/usb-offline.img" "$src/usb-online.img" "$WORK"/out/clokeos-*-x86_64-desktop.img
  fi
done

# Quellcode: dieser Commit und die Fremdquellen, aus denen mitgebaut wurde
srcdir="$WORK/tmp/source-$CLOKE_VERSION"
rm -rf "$srcdir"
mkdir -p "$srcdir/third_party"
git -C "$repo_dir" archive --format=tar --prefix="ClokeOS-$CLOKE_VERSION/" "$commit" | tar -x -C "$srcdir"
for f in "$XNU_LOADER_TGZ" "$GNU_EFI_TGZ" "$APPMENU_TGZ"; do [ -f "$f" ] && cp "$f" "$srcdir/third_party/"; done
# Darlings Frameworks nur, wenn ihre Quellen geladen sind; nur dann baut brand.sh sie ein
# (macapps/install.sh), und nur dann gehören sie ins Quellarchiv
darling=0
for r in "$WORK"/download/darling/*/; do
  [ -d "$r/.git" ] || continue
  n=$(basename "$r")
  git -C "$r" archive --format=tar.gz --prefix="$n/" HEAD > "$srcdir/third_party/$n-$(git -C "$r" rev-parse --short=12 HEAD).tar.gz"
  darling=1
done
licenses="xnu-loader (BSD-3-Clause), gnu-efi und vala-panel-appmenu (LGPL-3)"
macnote="Darlings Mac-Frameworks (AppKit für Mac-Programme) sind in diesem Release nicht enthalten."
if [ "$darling" = 1 ]; then
  licenses="xnu-loader (BSD-3-Clause), gnu-efi, vala-panel-appmenu (LGPL-3) und Darlings Frameworks (GPL-3)"
  macnote="Mac-Programme laufen über Darlings Frameworks (AppKit aus darling-cocotron), noch mit vielen Lücken."
fi
cat > "$srcdir/third_party/README" <<EOF
Fremdquellen, aus denen ClokeOS $CLOKE_VERSION mitgebaut wurde, so wie sie geladen wurden. Die
ClokeOS-Änderungen daran stehen im Quellbaum (loader/xnu-loader.patch, menubar/appmenu-clokeos.patch,
macapps/patches/). PureDarwin selbst, dessen Images die Grundlage sind, steht unter
https://github.com/PureDarwin/PureDarwin/tree/$PD_VERSION.
EOF
tar -czf "$dist/clokeos-$CLOKE_VERSION-source.tar.gz" -C "$srcdir" .
rm -rf "$srcdir"
(cd "$dist" && shasum -a 256 -- *.xz *.tar.gz > SHA256SUMS)

cat > "$dist/RELEASE-NOTES.md" <<EOF
ClokeOS $CLOKE_VERSION „$CLOKE_CODENAME“ (Build $CLOKE_BUILD) mit dem Desktop CloakServer, zum Installieren wie bei macOS: vom USB-Stick auf einen PC oder aus dem Internet in einer VM.

## Auf einem PC installieren (x86_64, USB-Stick)

Zwei Sticks zur Wahl:

- \`clokeos-x86_64-usb-offline.img.xz\` bringt das System mit und installiert ohne Internet. Für die meisten PCs die richtige Wahl.
- \`clokeos-x86_64-usb-online.img.xz\` ist klein und lädt das System beim Installieren aus dem Internet; dafür braucht der PC ein LAN-Kabel und einen unterstützten Netzwerkchip (siehe unten).

1. Abbild laden, entpacken (\`xz -dk\`) und auf einen USB-Stick mit mindestens 2 GB schreiben, zum Beispiel mit balenaEtcher oder auf dem Mac: \`diskutil list\`, \`diskutil unmountDisk /dev/diskN\`, \`sudo dd if=clokeos-x86_64-usb-offline.img of=/dev/rdiskN bs=4m\` (N ist der Stick; alles darauf wird gelöscht).
2. **USB-Tastatur und USB-Maus anstecken**, dann am PC im Boot-Menü der Firmware den Stick wählen (UEFI, ohne CSM/Legacy, Secure Boot aus, SATA im AHCI-Modus, Intel VMD/RST aus). Das Laden dauert je nach Stick eine bis zwei Minuten; mit gedrückter Taste V startet ClokeOS ausführlich mit Meldungen auf dem Bildschirm.
3. In der Wiederherstellung „Install ClokeOS“ wählen und die SATA- oder NVMe-SSD angeben. Die SSD wird dabei gelöscht. Passt die Wiederherstellung samt System nicht in den Speicher unterhalb von 1 GB (je nach Firmware sind dort nur rund 400 MB am Stück frei), startet der Offline-Stick eine schlanke Variante und liest das System während der Installation vom Stick.
4. Neu starten, den Stick abziehen. Beim ersten Start richtet der Einrichtungsassistent Sprache, Region, deinen Account und das Erscheinungsbild ein.

Voraussetzungen: UEFI, SATA- oder NVMe-SSD. Tastatur und Maus über USB, beim Einschalten schon angesteckt; eingebaute Notebook-Tastaturen (PS/2) und Touchpads (I²C) gehen noch nicht. Findet ClokeOS keine USB-Tastatur oder -Maus, zeigt es nach 45 Sekunden ein Fenster mit einer Diagnose zum Abfotografieren. Netzwerk nur über LAN mit Realtek RTL8111/8168/8169 oder Intel 82545EM; WLAN und neuere Intel-Netzwerkchips (I219, I225) werden nicht erkannt. Bild über die Grafik der Firmware (GOP).

## Ohne USB-Stick: fertiges SSD-Abbild (x86_64)

\`clokeos-x86_64-disk.img.xz\` ist eine fertig installierte SSD mit System und Wiederherstellung, ganz ohne Internet: die SSD des PCs per USB-SATA- oder USB-NVMe-Adapter an den Mac hängen, entpacken und draufschreiben (\`sudo dd if=clokeos-x86_64-disk.img of=/dev/rdiskN bs=4m\`, oder balenaEtcher), zurück in den PC und davon starten. Beim ersten Start läuft der Einrichtungsassistent. ClokeOS belegt gut 5 GB, der Rest der SSD bleibt frei.

## In einer VM installieren (QEMU)

\`clokeos-<arch>-installer.img.xz\` (x86_64 oder arm64 für Macs mit Apple-Chip) entpacken und mit einer leeren zweiten Platte starten, zum Beispiel mit \`clokeos-darwin/run.sh\` aus dem Quellcode: \`CLOKEOS_ARCH=arm64 CLOKEOS_FLAVOR=desktop ./run.sh --image clokeos-arm64-installer.img --disk platte.img\`. Die Wiederherstellung lädt das System dann aus dem Internet von hier.

Später startet „Restart in Recovery…“ im ClokeOS-Menü die Wiederherstellung auf der Platte, auf x86_64 auch die Taste R beim Start. Von dort lässt sich ClokeOS neu installieren, die Wiederherstellung bleibt erhalten.

## Neu in $CLOKE_VERSION

- Englisch als Grundsprache wie bei macOS, Deutsch unter System Settings > Language & Region oder im Einrichtungsassistenten
- Einrichtungsassistent beim ersten Start: Sprache, Region mit Zeitzone, eigener Account mit Passwort, Erscheinungsbild
- Entfernte Anmeldung per SSH (System Settings > Sharing > Remote Login, voreingestellt an): \`ssh <account>@<adresse>\`, die Adresse steht auf derselben Seite
- USB-Sticks für echte PCs: offline (bringt das System mit) und online (klein, lädt aus dem Internet); sie starten als RAM-Disk und installieren auf SATA- oder NVMe-SSDs, auch auf PCs, deren Firmware unter 1 GB nur wenig Speicher frei lässt
- fertiges SSD-Abbild zum Draufschreiben, als Weg ohne Stick
- Downloads setzen nach Verbindungsabbrüchen an derselben Stelle fort
- Diagnosefenster, wenn keine USB-Tastatur oder -Maus da ist
- GTK-Thema ClokeOS in den Farben des Desktops
- schlankere Wiederherstellung (knapp 600 MB statt 1,4 GB)

## Was drin ist

- CloakServer, der Fensterserver von ClokeOS: Menüleiste, Dock, Launchpad, Anmeldung, Hell- und Dunkelmodus, Fenster mit Schatten und Glas
- Globale Menüleiste wie bei macOS: Die Menüs der Programme stehen oben in der Leiste statt im Fenster, bei GTK-Programmen wie Fundus und Terminal ebenso wie bei Mac-Programmen
- Wiederherstellung mit Installer, Online-Hilfe, Festplattendienstprogramm und Terminal
- $macnote

Grenzen: ein Prozessorkern, Grafik in Software über die Firmware, nur wenige Treiber (siehe oben). Getestet in QEMU (x86_64 emuliert, auch mit USB-Stick und NVMe-SSD; arm64 mit Apples Hypervisor); auf echter Hardware ist es ein erster Versuch.

## Dateien

| Datei | Architektur | gepackt | entpackt |
|---|---|---|---|
$rows
Die Wiederherstellung liest \`clokeos-<arch>.txt\` und prüft jedes Abbild beim Laden gegen die SHA-256 darin. \`SHA256SUMS\` enthält die Prüfsummen aller Dateien.

## Quellen und Lizenzen

\`clokeos-$CLOKE_VERSION-source.tar.gz\` enthält den Quellcode dieses Releases (Commit \`$commit\`) und die mitgebauten Fremdquellen: $licenses. Grundlage sind PureDarwins Images $PD_VERSION (Darwin und XNU unter der APSL 2.0, dazu GPL-Teile wie corecrypto); deren Quellcode: https://github.com/PureDarwin/PureDarwin/tree/$PD_VERSION.
EOF
ls -l "$dist"

if [ "$upload" = 1 ]; then
  gh release create "$tag" -R "$CLOKE_RELEASES_REPO" --title "ClokeOS $CLOKE_VERSION „$CLOKE_CODENAME“" \
    --notes-file "$dist/RELEASE-NOTES.md" --latest "$dist"/*.xz "$dist"/*.txt "$dist"/*.tar.gz "$dist/SHA256SUMS"
  echo "Veröffentlicht: $(gh release view "$tag" -R "$CLOKE_RELEASES_REPO" --json url -q .url)"
fi
