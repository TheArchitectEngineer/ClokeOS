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
  id=$(plutil -extract CFBundleIdentifier raw "$dir/Info.plist")
  ver=$(plutil -extract CFBundleVersion raw "$dir/Info.plist")
  objs=()
  for c in "$dir"/*.cpp; do
    o="$tmp/$name-$(basename "$c" .cpp).o"
    clang++ "${cflags[@]}" -fapple-kext -std=gnu++17 -fno-exceptions -fno-rtti -nostdinc++ -c "$c" -o "$o"
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
for p in "$base"/root/Extensions/System.kext/PlugIns/*.kext; do args+=(-codeless "$p"); done
runner=()
[ "$(uname -m)" = arm64 ] && runner=(arch -x86_64)
"${runner[@]}" "$base/kc-builder" "${args[@]}" -o "$WORK/out/kernel" > "$tmp/kc-builder.log" 2>&1 ||
  { tail -20 "$tmp/kc-builder.log" >&2; exit 1; }
for k in ${kexts[@]+"${kexts[@]}"}; do
  id=$(plutil -extract CFBundleIdentifier raw "$k/Contents/Info.plist")
  grep -q "patched kmod_info: $id" "$tmp/kc-builder.log" || { echo "kc-builder hat $id nicht eingebunden" >&2; exit 1; }
done
python3 -I "$HERE/kcfix.py" "$WORK/out/kernel" >/dev/null
echo "Kernel Collection: $WORK/out/kernel (${#kexts[@]} eigene Treiber)"
