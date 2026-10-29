#!/bin/bash
# Accelerate-Teilmenge: baut macapps/accelerate/*.c als Accelerate.framework für ClokeOS (vDSP, vForce, CBLAS,
# LAPACK-Kern, vImage-Auszug) und das Prüfprogramm acctest.
#
#   macapps/accelerate.sh                         x86_64, nach ~/ClokeOS-work/out/macapps/root/
#   CLOKEOS_ARCH=arm64 macapps/accelerate.sh      arm64, nach ~/ClokeOS-work/out/macapps-arm64/root/
#
# Braucht nur PureDarwins libSystem (kein CoreFoundation). Übersetzt mit Apples clang und SDK-Headern.
set -euo pipefail
source "$(dirname "$0")/../config.sh"
CLOKEOS_FLAVOR=desktop
source "$(dirname "$0")/../config.sh"
need clang xcrun

M="$HERE/macapps"
SDK="$(xcrun --show-sdk-path)"
TARGET="$CLOKE_ARCH-apple-macos11.0"
out="$WORK/out/macapps"; ARCHCC=()
[ "$CLOKE_ARCH" = arm64 ] && { out="$WORK/out/macapps-arm64"; ARCHCC=(-fno-stack-check); }
root="$out/root"; pd="$out/pdlibs"
fwdir="/Library/ClokeOS/MacFrameworks"
[ -d "$pd" ] || { echo "Fehlt: $pd (zuerst macapps/foundation.sh)" >&2; exit 1; }

d="$root$fwdir/Accelerate.framework"
rm -rf "$d"; mkdir -p "$d/Versions/A/Resources"
clang -target "$TARGET" -isysroot "$SDK" -O3 -g0 -w ${ARCHCC[@]+"${ARCHCC[@]}"} -dynamiclib -nostdlib \
  -Wl,-platform_version,macos,11.0,11.5 -Wl,-syslibroot,"$pd" \
  $([ "$CLOKE_ARCH" = arm64 ] && echo -Wl,-no_fixup_chains) \
  -install_name /System/Library/Frameworks/Accelerate.framework/Versions/A/Accelerate \
  -compatibility_version 1 -current_version 1 \
  -o "$d/Versions/A/Accelerate" "$M"/accelerate/{vdsp,vforce,blas,lapack,vimage,ccompat}.c \
  "$pd/usr/lib/libSystem.B.dylib" 2>&1 | grep -v '^ld: warning' || true
[ -f "$d/Versions/A/Accelerate" ] || exit 1
cat > "$d/Versions/A/Resources/Info.plist" <<'PL'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>com.apple.Accelerate</string>
<key>CFBundleName</key><string>Accelerate</string>
<key>CFBundleExecutable</key><string>Accelerate</string>
<key>CFBundlePackageType</key><string>FMWK</string>
<key>CFBundleVersion</key><string>1</string>
</dict></plist>
PL
ln -s A "$d/Versions/Current"; ln -s Versions/Current/Accelerate "$d/Accelerate"; ln -s Versions/Current/Resources "$d/Resources"

# Prüfprogramm gegen Apples SDK gelinkt (wie ein Mac-Programm), zur Laufzeit gilt unser Framework
mkdir -p "$root/Library/ClokeOS/Probe"
clang -target "$TARGET" -isysroot "$SDK" -O2 -fno-stack-check -w -framework Accelerate \
  -o "$root/Library/ClokeOS/Probe/acctest" "$M/accelerate/acctest.c" "$M/accelerate/acctest_lapack.c" "$M/accelerate/ccompat.c"
echo "Fertig: $d"
nm -gU "$d/Versions/A/Accelerate" | wc -l
