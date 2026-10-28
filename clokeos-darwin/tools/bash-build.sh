#!/bin/bash
# Baut GNU bash auf dem Mac für PureDarwin und legt es als /bin/bash in <stage>.
#
#   tools/bash-build.sh <stage>     Architektur wie überall mit CLOKEOS_ARCH (Standard: x86_64)
#
# PureDarwins libSystem hat weder ___chkstk_darwin (daher -fno-stack-check) noch strchrnul,
# mbsnrtowcs, wcsnrtombs, wcswidth, imaxdiv, getservent und iconv; ein Termcap gibt es auch nicht
# (bash bringt seins mit); /dev/fd gibt es auch nicht, Prozesssubstitution <(…) läuft über eine Datei (tools/bash-clokeos.patch). Configure bekommt das als Cache-Werte, danach prüft das Skript, dass
# jedes unaufgelöste Symbol in PureDarwins libSystem steht.
set -euo pipefail
source "$(dirname "$0")/../config.sh"
need "$DEBUGFS" clang nm

stage="$1"
read -r root_start _ < <(gpt_part "$PD_IMG" "$GUID_LINUX_FS")
fs="$PD_IMG?offset=$((root_start * 512))"

fetch_file "$BASH_URL" "$BASH_SHA256" "$BASH_TARBALL"
build="$WORK/tmp/bash-$CLOKE_ARCH"
rm -rf "$build"
mkdir -p "$build"
tar -xzf "$BASH_TARBALL" -C "$build" --strip-components=1
cd "$build"
# <(cmd) ohne /dev/fd und ohne FIFOs (PureDarwin hat beides nicht): cmd läuft zuerst in eine Datei;
# setlocale-Warnungen entfallen (PureDarwin hat keine Locale-Dateien)
patch -s -p1 < "$HERE/tools/bash-clokeos.patch"

target=x86_64-apple-macos11.0
[ "$CLOKE_ARCH" = arm64 ] && target=arm64-apple-macos11.0
cflags="-target $target -O2 -fcommon -fno-stack-check -D_FORTIFY_SOURCE=0 -Wno-implicit-function-declaration -Wno-deprecated-non-prototype"
ldflags="-target $target -Wl,-no_fixup_chains -Wl,-platform_version,macos,11.0,11.5 -Wl,-dead_strip"

./configure --prefix=/usr --build="$(uname -m)-apple-darwin" --host="${target%%-*}-apple-darwin" \
  --without-bash-malloc --disable-nls --with-curses=no bash_cv_termcap_lib=gnutermcap \
  ac_cv_func_strchrnul=no ac_cv_func_mbsnrtowcs=no ac_cv_func_wcsnrtombs=no ac_cv_func_wcswidth=no \
  ac_cv_func_imaxdiv=no ac_cv_func_getservent=no bash_cv_dev_fd=absent bash_cv_sys_named_pipes=present am_cv_func_iconv=no ac_cv_func_iconv=no \
  CC=clang CFLAGS="$cflags" LDFLAGS="$ldflags" > configure.log 2>&1
# Mach-O zieht Common-Symbole nicht aus Archiven: die Termcap-Variablen, die readline braucht, selbst anlegen
printf 'char PC;\nchar *BC;\nchar *UP;\n' > termvars.c
clang $cflags -c termvars.c
make -j"$(sysctl -n hw.ncpu)" LOCAL_LIBS="$PWD/termvars.o" > make.log 2>&1 || { tail -30 make.log >&2; exit 1; }

# Alle Symbole, die bash aus libSystem braucht, müssen in PureDarwins libSystem stehen.
"$DEBUGFS" -R "dump /usr/lib/libSystem.B.dylib $build/libSystem.B.dylib" "$fs" >/dev/null 2>&1
nm -u bash | grep -v ':' | sort -u > undefined.txt
nm -gU libSystem.B.dylib | awk '{print $3}' | sort -u > exported.txt
missing="$(comm -23 undefined.txt exported.txt | grep -v '^dyld_stub_binder$' || true)"
[ -z "$missing" ] || { echo "bash braucht Symbole, die PureDarwins libSystem nicht hat:" >&2; echo "$missing" >&2; exit 1; }

mkdir -p "$stage/bin"
cp bash "$stage/bin/bash"
chmod 755 "$stage/bin/bash"
echo "Fertig: bash $BASH_VERSION_CLOKE für $CLOKE_ARCH"
