#!/bin/bash
# Baut ein ClokeOS-Release: je Architektur ein frisch gebautes Image, gepackt mit xz,
# dazu SHA256SUMS und Release-Notizen. Ergebnis: ~/ClokeOS-work/dist/<version>/
#
#   ./release.sh                    nur x86_64
#   ./release.sh x86_64 arm64       mehrere Architekturen (arm64 lädt dafür PureDarwins Vorschau-Image)
#   ./release.sh --upload [...]     zusätzlich als Release-Entwurf v<version> ins GitHub-Repo laden
#
# Hochgeladen wird nur ein Entwurf. Veröffentlichen geht danach von Hand auf GitHub. Ist v<version>
# dort schon veröffentlicht, bricht das Skript ab: dann zuerst CLOKE_VERSION in config.sh erhöhen.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"

upload=0
arches=()
for a in "$@"; do
  case "$a" in
    --upload) upload=1 ;;
    x86_64|arm64) arches+=("$a") ;;
    *) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 2 ;;
  esac
done
[ ${#arches[@]} -gt 0 ] || arches=(x86_64)

source "$here/config.sh"
need xz shasum git
repo_dir="$(cd "$here/.." && pwd)"
tag="v$CLOKE_VERSION"
commit="$(git -C "$repo_dir" rev-parse HEAD)"

if [ "$upload" = 1 ]; then
  need gh
  if [ -n "$(git -C "$repo_dir" status --porcelain)" ]; then
    echo "Das Repo hat nicht eingecheckte Änderungen. Erst committen, dann hochladen." >&2
    exit 1
  fi
  git -C "$repo_dir" fetch -q origin
  if ! git -C "$repo_dir" branch -r --contains "$commit" | grep -q .; then
    echo "Commit $commit ist noch nicht auf GitHub. Erst git push, dann hochladen." >&2
    exit 1
  fi
  repo="$(cd "$repo_dir" && gh repo view --json nameWithOwner -q .nameWithOwner)"
  state="$(gh release view "$tag" -R "$repo" --json isDraft -q .isDraft 2>/dev/null || echo none)"
  if [ "$state" = false ]; then
    echo "$tag ist auf GitHub schon veröffentlicht. CLOKE_VERSION in config.sh erhöhen." >&2
    exit 1
  fi
fi

dist="$WORK/dist/$CLOKE_VERSION"
rm -rf "$dist"
mkdir -p "$dist"

files=()
rows=""
for arch in "${arches[@]}"; do
  CLOKEOS_ARCH="$arch" "$here/brand.sh"
  img="$WORK/out/clokeos-$CLOKE_VERSION-$arch.img"
  out="$dist/$(basename "$img").xz"
  echo "Packe $(basename "$out")"
  xz -T0 -9 -c "$img" > "$out"
  files+=("$out")
  raw_sum="$(shasum -a 256 "$img" | cut -d' ' -f1)"
  rows+="| \`$(basename "$out")\` | $arch | $(( $(stat -f %z "$out") / 1048576 )) MB | \`$raw_sum\` |"$'\n'
done
(cd "$dist" && shasum -a 256 -- *.img.xz > SHA256SUMS)
files+=("$dist/SHA256SUMS")

notes="$dist/RELEASE-NOTES.md"
cat > "$notes" <<EOF
ClokeOS $CLOKE_VERSION „$CLOKE_CODENAME“ (Build $CLOKE_BUILD): ein macOS-ähnliches System auf dem Darwin-Kernel (XNU), aufgebaut auf PureDarwin $PD_VERSION. Es bootet bis zur zsh; eine grafische Oberfläche gibt es im Image noch nicht (die liegt als Prototyp in \`clokeos.html\`).

## Downloads

| Datei | Architektur | Größe | SHA-256 des entpackten Images |
|---|---|---|---|
$rows
\`SHA256SUMS\` enthält die Prüfsummen der \`.xz\`-Dateien: \`shasum -a 256 -c SHA256SUMS\`.

## Starten

Entpacken (\`xz -dk clokeos-$CLOKE_VERSION-x86_64.img.xz\`, unter macOS \`xz\` aus Homebrew) und mit QEMU starten. Am einfachsten mit den Skripten aus diesem Repo:

\`\`\`bash
cd clokeos-darwin && ./run.sh --image /pfad/zu/clokeos-$CLOKE_VERSION-x86_64.img
\`\`\`

Der Bildschirm zeigt das Bootlogo, die Shell läuft auf der seriellen Konsole im Terminal. Wer beim Start die Taste V gedrückt hält, sieht Kernel-Log und Shell auf dem Bildschirm. Auf einem Mac mit Apple-Chip wird x86_64 emuliert; bis zur Eingabeaufforderung dauert es etwa 75 bis 90 Sekunden.

## Was geht

- XNU (Darwin 20.5.0), launchd, zsh, \`sw_vers\` meldet ClokeOS $CLOKE_VERSION, Rechnername \`$CLOKE_HOST\`, \`clokefetch\`
- Netzwerk in QEMU mit \`-nic user,model=e1000-82545em\`: DHCP, DNS, ping ins Internet
- \`poweroff\` fährt sauber herunter

## Was nicht geht

- Nur ein Prozessorkern (\`hw.ncpu\` = 1)
- Keine grafische Oberfläche im Image, ohne Taste V keine Shell auf dem Bildschirm
- Nur in QEMU getestet, nicht auf echter Hardware
- arm64: die Skripte kennen es (\`CLOKEOS_ARCH=arm64\`), gebaut oder gebootet wurde es noch nicht

## Quellen und Lizenzen

- Basis: [PureDarwin $PD_VERSION]($PD_URL), SHA-256 \`$PD_SHA256\`. Quellcode: https://github.com/PureDarwin/PureDarwin/tree/$PD_VERSION
- Darwin und XNU: Apple Public Source License 2.0. PureDarwin enthält auch GPL-Teile (corecrypto) und weitere freie Lizenzen; der Quellcode steht unter dem Link oben.
- Bootloader (x86_64): [xnu-loader](https://github.com/PureDarwin/xnu-loader/tree/$XNU_LOADER_REV) (BSD-3-Clause) mit dem ClokeOS-Patch aus \`clokeos-darwin/loader/\`, gebaut gegen gnu-efi $GNU_EFI_VERSION
- ClokeOS-Änderungen: dieses Repo, Commit \`$commit\`
EOF
files+=("$notes")

echo
echo "Release $tag in $dist:"
ls -l "$dist"

if [ "$upload" = 1 ]; then
  title="ClokeOS $CLOKE_VERSION „$CLOKE_CODENAME“"
  if [ "$state" = true ]; then
    gh release upload "$tag" -R "$repo" --clobber "${files[@]}"
    gh release edit "$tag" -R "$repo" --title "$title" --notes-file "$notes"
  else
    gh release create "$tag" -R "$repo" --draft --target "$commit" --title "$title" --notes-file "$notes" "${files[@]}"
  fi
  echo "Entwurf: $(gh release view "$tag" -R "$repo" --json url -q .url)"
fi
