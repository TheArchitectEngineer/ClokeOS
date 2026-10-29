# WebKit for ClokeOS (cross-build on the Mac, target PureDarwin 20.5 arm64)

Status 2026-10-29: **JavaScriptCore (`jsc`) builds and runs in the arm64 VM** (C-loop interpreter, no JIT, no WebAssembly yet).
The GTK port (`gtk-options.sh`: GTK3, no JIT/WASM, no GStreamer/GBM/WebGL) builds, links and **MiniBrowser opens its window in the arm64 VM**
(first light 2026-10-29); the first page load (https://example.com) was still stuck at the start, cause not yet found (suspects: no GIO TLS
backend / glib-networking in the image, network process). Run: `stage.sh` -> tgz -> unpack to /opt/webkit in the VM, helpers must be linked
into /usr/local/libexec/webkit2gtk-4.1/, start with `runmb.sh` (ignores SIGSYS for SysV shm, adds `compat/crashbt.c` for backtraces, SIGINFO dumps all
threads). Fixes found on the way (all in the patch): Darwin IPC uses `SOCK_DGRAM` (needs `sysctl net.local.dgram.maxdgram=16384`),
WebKit-layer `OS(DARWIN)` -> `PLATFORM(COCOA)`, SQLite init no longer waits for the main thread (deadlock with IconDatabase). Needs `libgpg-error`
(built from source into the sysroot, not in the image) and stub `.pc` files for packages the image lacks (see `stubpc` note in sysroot.sh). See `../docs/browser-study.md`.

```
CLOKEOS_ARCH=arm64 CLOKEOS_FLAVOR=desktop ./sysroot.sh            # libs + headers from the image -> $WORK/sysroot/webkit-arm64
curl -O https://webkitgtk.org/releases/webkitgtk-2.54.2.tar.xz && tar xf webkitgtk-2.54.2.tar.xz
(cd webkitgtk-2.54.2 && patch -p1 < ../patches/0001-puredarwin.patch)
CLOKEOS_ARCH=arm64 ./configure.sh JSCOnly $PWD/webkitgtk-2.54.2 $PWD/build-jsc -DENABLE_STATIC_JSC=ON \
   -DENABLE_JIT=OFF -DENABLE_C_LOOP=ON -DENABLE_WEBASSEMBLY=OFF -DENABLE_SAMPLING_PROFILER=OFF
ninja -C build-jsc jsc          # about 10 minutes on 12 cores
```

Why it looks like this
- The release tarball contains no Apple sources (`mac/`, `cocoa/`), so CMake must not see "Darwin": `toolchain-pd-arm64.cmake`
  names the system `PureDarwin` (generic code paths) and `PD_DARWIN`/`CLOKEOS_PD` mark the Mach-O target.
- System headers come from the Mac's SDK (PureDarwin's header set lacks `os/lock.h`, `mach/thread_act.h`, `mach-o/dyld.h`, ...);
  libraries and third-party headers (glib, gtk, icu, ...) come from the image; `-nostdlib` linking against the image's libSystem/libc++.
- `compat/`: tiny replacements for Cocoa-only files (`OSLogPrintStream`, bmalloc `ProcessCheck`) and C shims
  (`os_unfair_lock_lock_with_flags`, readline stub, `___chkstk_darwin` from `../libcompat/chkstk.S`).
- `shim/wtf/cocoa/Entitlements.h`: PureDarwin has no entitlements.
- `patches/0001-puredarwin.patch`: six small CMake/header edits against the 2.54.2 tarball.

Open: JIT (needs `wtf/spi/cocoa/MachVMSPI.h` replacement), WebAssembly, the GTK port itself (libxslt/woff2 absent, `Source/WebKit`
IPC uses `SOCK_DGRAM` on Darwin), Skia/Cairo rendering choice, X11 window integration.
