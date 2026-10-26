#!/bin/bash
# Baut die eigenen Kernel-Treiber von ClokeOS (kext/<Name>/: *.cpp und Info.plist) und daraus mit
# PureDarwins kc-builder den Kernel Collection für die EFI-Partition (EFI/BOOT/kernel), x86_64.
#
#   kext/build.sh            Ergebnis: ~/ClokeOS-work/out/kexts/<Name>.kext und ~/ClokeOS-work/out/kernel
#
# Header (Kernel.framework), Kernel, PureDarwins Treiber, kc-builder und die Liste der Treiber
# (pd-rebuild-kc) kommen mit debugfs aus dem Desktop-Image, ohne es zu booten. kc-builder ist ein
# x86_64-Programm, das nur libSystem braucht; auf einem Mac mit Apple-Chip läuft es über Rosetta.
# Ohne eigene Treiber ist das Ergebnis byte-gleich mit dem Kernel aus PureDarwins EFI-Partition
# (nach kcfix.py). Die Treiber werden wie PureDarwins eigene gelinkt: MH_BUNDLE gegen libSystem,
# Kernelsymbole per dynamic_lookup, Zeiger als Rebase-/Bind-Anweisungen (kc-builder wendet klassische
# lokale Relokationen nicht an).
set -euo pipefail
export CLOKEOS_FLAVOR=desktop
CLOKEOS_ARCH=x86_64
export CLOKEOS_ARCH
source "$(dirname "$0")/../config.sh"
need "$DEBUGFS" clang clang++ python3
src="$HERE/kext"
sdk=$(xcrun --show-sdk-path)

read -r root_start _ < <(gpt_part "$PD_IMG" "$GUID_LINUX_FS")
fs="$PD_IMG?offset=$((root_start * 512))"
base="$WORK/sysroot/kc-${PD_SHA256:0:12}"
if [ ! -f "$base/.fertig" ]; then
  rm -rf "$base"
  mkdir -p "$base/headers" "$base/root" "$base/avail"
  "$DEBUGFS" -R "rdump /System/Library/Frameworks/Kernel.framework/Versions/A/Headers $base/headers" "$fs" >/dev/null 2>&1
  "$DEBUGFS" -R "rdump /System/Library/Extensions $base/root" "$fs" >/dev/null 2>&1
  "$DEBUGFS" -R "rdump /System/Library/Kernels $base/root" "$fs" >/dev/null 2>&1
  "$DEBUGFS" -R "dump /usr/bin/kc-builder $base/kc-builder" "$fs" >/dev/null 2>&1
  "$DEBUGFS" -R "dump /usr/sbin/pd-rebuild-kc $base/pd-rebuild-kc" "$fs" >/dev/null 2>&1
  "$DEBUGFS" -R "dump /usr/lib/libSystem.B.dylib $base/libSystem.B.dylib" "$fs" >/dev/null 2>&1
  # Availability.h und Co. fehlen im Kernel.framework (bei Apple kommen sie über -isysroot)
  for h in Availability.h AvailabilityInternal.h AvailabilityMacros.h AvailabilityVersions.h TargetConditionals.h; do
    "$DEBUGFS" -R "dump /usr/include/$h $base/avail/$h" "$fs" >/dev/null 2>&1
  done
  chmod +x "$base/kc-builder"
  for f in headers/Headers/IOKit/IOService.h root/Kernels/kernel root/Extensions/System.kext/Info.plist kc-builder \
           pd-rebuild-kc libSystem.B.dylib avail/Availability.h; do
    [ -s "$base/$f" ] || { echo "Nicht aus dem Image geholt: $f" >&2; exit 1; }
  done
  touch "$base/.fertig"
fi
hdr="$base/headers/Headers"

# --- Treiber übersetzen ------------------------------------------------------------------------
out="$WORK/out/kexts"
tmp="$WORK/tmp/kexts"
rm -rf "$out" "$tmp"
mkdir -p "$out" "$tmp"
cflags=(-target x86_64-apple-macos11 -mkernel -fno-builtin -fno-common -nostdinc -I"$hdr" -idirafter "$base/avail"
        -DKERNEL -O2 -Wall -Wextra -Wno-unused-parameter -Werror=return-type
        -ffile-prefix-map="$HERE"=clokeos-darwin -ffile-prefix-map="$WORK"=work)
kexts=()
for dir in "$src"/*/; do
  [ -f "$dir/Info.plist" ] || continue
  name=$(basename "$dir")
  # kc-builder liest die Info.plist mit einem eigenen Parser, der nach einem XML-Kommentar den Rest des
  # Wörterbuchs verliert (der Treiber hatte dann keine Match-Schlüssel und wurde nie zugeordnet)
  ! grep -q '<!--' "$dir/Info.plist" || { echo "$dir/Info.plist: keine XML-Kommentare (kc-builder)" >&2; exit 1; }
  id=$(plutil -extract CFBundleIdentifier raw "$dir/Info.plist")
  ver=$(plutil -extract CFBundleVersion raw "$dir/Info.plist")
  objs=()
  # ClokeI2CHID malt seine Schritte auf den Bildschirm (ClokeTrace aus kext/pdacpi, mit eigenen Namen, damit
  # sich die Symbole nicht mit denen der ACPI-Plattform beißen), um zu sehen, wo ein Hänger sitzt
  extra=()
  xflags=()
  if [ "$name" = ClokeI2CHID ]; then
    extra=("$src/pdacpi/ClokeTrace.cpp")
    xflags=(-Dcloke_trace=i2c_trace -Dcloke_trace_show=i2c_trace_show -I"$src/pdacpi")
  fi
  for c in "$dir"/*.cpp ${extra[@]+"${extra[@]}"}; do
    o="$tmp/$name-$(basename "$c" .cpp).o"
    clang++ "${cflags[@]}" ${xflags[@]+"${xflags[@]}"} -fapple-kext -std=gnu++17 -fno-exceptions -fno-rtti -nostdinc++ -c "$c" -o "$o"
    objs+=("$o")
  done
  cat > "$tmp/$name-info.c" <<EOF
#include <mach/mach_types.h>
extern kern_return_t _start(kmod_info_t *ki, void *data);
extern kern_return_t _stop(kmod_info_t *ki, void *data);
__attribute__((visibility("default"))) KMOD_EXPLICIT_DECL($id, "$ver", _start, _stop)
__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t *_antimain = 0;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
EOF
  clang "${cflags[@]}" -c "$tmp/$name-info.c" -o "$tmp/$name-info.o"
  k="$out/$name.kext"
  mkdir -p "$k/Contents/MacOS"
  cp "$dir/Info.plist" "$k/Contents/Info.plist"
  clang++ -target x86_64-apple-macos11 -bundle -undefined dynamic_lookup -nostdlib -Wl,-no_fixup_chains \
    -L"$sdk/usr/lib" "${objs[@]}" "$tmp/$name-info.o" -lkmodc++ -lkmod "$base/libSystem.B.dylib" \
    -o "$k/Contents/MacOS/$name" 2> >(grep -v 'built for newer' >&2)
  kexts+=("$k")
  echo "Treiber: $k"
done

# --- Kernel Collection --------------------------------------------------------------------------
args=(-kernel "$base/root/Kernels/kernel")
while read -r line; do
  k=$(printf '%s\n' "$line" | sed -n -E 's/.*-kext "\$KEXTS\/([^"]+)".*/\1/p')
  [ -n "$k" ] && args+=(-kext "$base/root/Extensions/$k")
done < "$base/pd-rebuild-kc"
for k in ${kexts[@]+"${kexts[@]}"}; do args+=(-kext "$k"); done
# Zwei Kernel: kernel mit PureDarwins ACPI-Plattform PDACPIPlatform (uACPI-Interpreter, liest die MADT und startet die
# weiteren Prozessorkerne; läuft auf dem HP mit 12 Threads; Boot-Arg cpus=N löst dort XNUs Topologie-Panik aus, nicht
# benutzen) und kernel-up mit der einfachen AppleI386PlatformExpert (ein Kern). Beide passen auf den Knoten "ACPI";
# in einer Kopie mit höherem IOProbeScore gewinnt die ACPI-Plattform. Der Loader nimmt kernel-up, wenn beim
# Einschalten U gehalten wird (die Taste kommt auf manchen PCs nicht an, z. B. dem HP).
smp="$tmp/PDACPIPlatform.kext"
rm -rf "$smp"
# PDACPIPlatform wird aus PureDarwins Quelle neu gebaut (kext/pdacpi/pdacpi.patch): ohne Änderung stürzt XNU
# auf Hybrid-Prozessoren (Alder Lake) in x86_validate_topology() ab, und nach initialize_screen(6) sieht
# man die Panik nicht. Zusätzlich malt ClokeTrace mit Boot-Arg cloke_trace=1 den Fortschritt auf den Schirm.
fetch_file "$PDSRC_URL" "$PDSRC_SHA256" "$PDSRC_TGZ" >/dev/null
pd="$WORK/tmp/pdacpi-src"
rm -rf "$pd"
mkdir -p "$pd"
tar -xzf "$PDSRC_TGZ" -C "$pd" --strip-components=1 '*/src/Kernel/Extensions/PDACPIPlatform' \
  '*/src/Kernel/Extensions/IOACPIFamily/include' '*/src/Kernel/Extensions/AppleAPIC/PICShared.h'
patch -s -p1 -d "$pd" < "$src/pdacpi/pdacpi.patch"
ext="$pd/src/Kernel/Extensions"
pdir="$ext/PDACPIPlatform"
res=$(clang -print-resource-dir)/include
pflags=("${cflags[@]}" -isystem "$res" -I"$src/pdacpi/include" -I"$src/pdacpi" -I"$pdir/uacpi/include"
        -I"$ext/IOACPIFamily/include" -include "$src/pdacpi/ipi_handler_t.h" -DUACPI_BAREBONES_MODE -Wno-everything)
pobjs=()
for c in "$pdir"/*.cpp "$src/pdacpi/ClokeTrace.cpp"; do
  o="$tmp/pdacpi-$(basename "$c" .cpp).o"
  clang++ "${pflags[@]}" -fapple-kext -std=gnu++17 -fno-exceptions -fno-rtti -nostdinc++ -c "$c" -o "$o"
  pobjs+=("$o")
done
for c in "$pdir"/uacpi/source/*.c; do
  o="$tmp/pdacpi-uacpi-$(basename "$c" .c).o"
  clang "${pflags[@]}" -std=c11 -ffreestanding -c "$c" -o "$o"
  pobjs+=("$o")
done
cat > "$tmp/pdacpi-info.c" <<'EOF'
#include <mach/mach_types.h>
extern kern_return_t _start(kmod_info_t *ki, void *data);
extern kern_return_t _stop(kmod_info_t *ki, void *data);
__attribute__((visibility("default"))) KMOD_EXPLICIT_DECL(org.puredarwin.driver.PDACPIPlatform, "1.0.0", _start, _stop)
__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t *_antimain = 0;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
EOF
clang "${cflags[@]}" -c "$tmp/pdacpi-info.c" -o "$tmp/pdacpi-info.o"
mkdir -p "$smp/Contents/MacOS"
cp "$pdir/Info.plist" "$smp/Contents/Info.plist"
clang++ -target x86_64-apple-macos11 -bundle -undefined dynamic_lookup -nostdlib -Wl,-no_fixup_chains \
  -L"$sdk/usr/lib" "${pobjs[@]}" "$tmp/pdacpi-info.o" -lkmodc++ -lkmod "$base/libSystem.B.dylib" \
  -o "$smp/Contents/MacOS/PDACPIPlatform" 2> >(grep -v 'built for newer' >&2)
python3 -I - "$smp/Contents/Info.plist" <<'PYEOF2'
import plistlib, sys
p = sys.argv[1]
d = plistlib.load(open(p, 'rb'))
d['IOKitPersonalities']['PDACPI Platform']['IOProbeScore'] = 5000
plistlib.dump(d, open(p, 'wb'), fmt=plistlib.FMT_XML)
PYEOF2
for p in "$base"/root/Extensions/System.kext/PlugIns/*.kext; do args+=(-codeless "$p"); done
runner=()
[ "$(uname -m)" = arm64 ] && runner=(arch -x86_64)
build_kc() { # <ausgabe> [weitere kc-builder-Argumente]
  local o="$1"
  shift
  "${runner[@]}" "$base/kc-builder" "${args[@]}" "$@" -o "$o" > "$tmp/kc-builder.log" 2>&1 ||
    { tail -20 "$tmp/kc-builder.log" >&2; exit 1; }
  for k in ${kexts[@]+"${kexts[@]}"}; do
    id=$(plutil -extract CFBundleIdentifier raw "$k/Contents/Info.plist")
    grep -q "patched kmod_info: $id" "$tmp/kc-builder.log" || { echo "kc-builder hat $id nicht eingebunden" >&2; exit 1; }
  done
  python3 -I "$HERE/kcfix.py" "$o" >/dev/null
}
build_kc "$WORK/out/kernel"
mv "$WORK/out/kernel" "$WORK/out/kernel-up"
build_kc "$WORK/out/kernel" -kext "$smp"
echo "Kernel Collection: $WORK/out/kernel (Mehrkern) und kernel-up (ein Kern) (${#kexts[@]} eigene Treiber)"
