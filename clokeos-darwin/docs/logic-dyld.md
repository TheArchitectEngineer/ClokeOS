# Logic Pro's C++ libraries in PureDarwin's dyld (test ladder steps 2 and 3, package Q)

Date: 2026-10-29. Image: `clokeos-2.5-arm64-desktop.img` (PureDarwin 20.5 dyld/libSystem, XNU arm64 virt), QEMU with HVF.
Subjects (thinned with `lipo -thin arm64`, copied from the user's own `/Applications/Logic Pro.app`, never committed):
`libcom-api.dylib` (5.8 MB, 12.7k exports), `libaafpgapi.dylib`, `libaafintp.dylib`, `OMF.framework/Versions/A/OMF`.
All four: `LC_DYLD_CHAINED_FIXUPS`, `LC_BUILD_VERSION platform 1 (macOS) minos 15.6 sdk 26.0`, install names `@rpath/...`,
dependencies only CoreFoundation, `/usr/lib/libc++.1.dylib` (current 2000.63), libSystem; OMF also Carbon and CoreServices.

## Result

All four load with `dlopen(RTLD_NOW)` in the arm64 VM after two small additions in this repo
(`libcompat/wide.c`, `macapps/carbon.sh`; `libcompat/chkstk.S` was already on main). Without them none of
`libcom-api` and `OMF` loads.

| Question | Answer (measured) |
|---|---|
| Chained fixups (`LC_DYLD_CHAINED_FIXUPS`) | accepted; rebases and binds are applied correctly: `AAFCreateRawStorageMemory` returns a C++ object whose vtable slot 2 (`Release`) is called and returns 0, in both `libaafpgapi` and `libcom-api` |
| `minos 15.6`, SDK 26.0 build version | accepted, no check at all; dyld only names it in error text ("which was built for Mac OS X 15.6") |
| Weak-def coalescing (`__Znam`, `__ZdlPv`, `__ZTISt12length_error`, ...) | works (libc++ from PureDarwin, libc++abi) |
| `/usr/lib/libc++.1.dylib` ABI age (PureDarwin: libc++ of the macOS 11 era, current version older than 2000.63) | dyld checks only the compatibility version (1.0.0) and all imported libc++ symbols exist; the C++ code in these four libs runs (construction, virtual calls, `Release`). Not exercised: exceptions thrown through libc++abi, `std::` types added after the macOS 11 libc++ |
| `@rpath` chains | works, but the same as on a Mac: `libaafintp` needs `@rpath/libaafpgapi.dylib` and has no `LC_RPATH`, so a plain `dlopen("./libaafintp.dylib")` from a program without rpath fails; with `DYLD_LIBRARY_PATH=.` or a program built with `-Wl,-rpath,<dir>` it loads (dyld uses the executable's rpaths: `DYLD_PRINT_RPATHS` shows the expansion) |
| `_availability_version_check` / `SystemVersion.plist` reporting 15.x | not needed for these four libs (none imports `_availability_version_check`, `___isPlatformVersionAtLeast`); 10 other Logic binaries do (`Logic`, `MAApplication`, `MATower`, `MAMixer`, `MAFiles`, ..., Impulse Response Utility), so package C/Q is still open for them. ClokeOS reports ProductVersion 2.5, Darwin 20.5.0 |
| `Versions/C` layout | not part of these four (OMF is `Versions/A`); unchecked |

## What stopped loading, in order, and the fix

PureDarwin's `dlopen` returns only `dlopen(./x.dylib): image not loaded`; the real message appears with `DYLD_PRINT_APIS=1`
(`dlopen_internal() failed, error: ...`). dyld stops at the first missing symbol, so the list was found one by one and
then confirmed statically.

1. `libcom-api`: `Symbol not found: ___chkstk_darwin` (clang emits it for frames > 4 KiB). Fix: `libcompat/chkstk.S`
   (already on main, re-exported through a patched `/usr/lib/libSystem.B.dylib`). The base 2.5 desktop image
   did not yet contain `libcompat`; the test image got it with `libcompat/build.sh`.
2. `libcom-api`: `Symbol not found: _swscanf` (PureDarwin's libSystem has `sscanf`/`vsscanf` but no wide scanf).
   Fix: `libcompat/wide.c` converts text and format to UTF-8 and calls `vsscanf`; `%ls %lc %S %C` return
   EOF/EINVAL (untested against real use by Logic, which only imports the symbol).
3. `OMF`: `___strncat_chk` (FORTIFY variant, missing in libSystem). Fix: `libcompat/wide.c`, aborts like Apple's
   when the result would not fit.
4. `OMF`: library `/System/Library/Frameworks/Carbon.framework/Versions/A/Carbon` (compat 2.0.0) is missing; no symbol is
   imported from it. Fix: `macapps/carbon.sh` builds an empty Carbon with Apples version numbers, re-exporting CoreServices.
5. `OMF`: five CoreServices symbols the PureDarwin CoreServices stub does not export: `_NewPtr`, `_DisposePtr`, `_TickCount`,
   `_UCConvertCFAbsoluteTimeToSeconds`, `_PBGetVolumeInfoSync`. Fix: `macapps/carbon.c` (`libclokeos_carbon.dylib`), added to
   CoreServices as an extra `LC_REEXPORT_DYLIB` (`libcompat/reexport.py`; PureDarwin's CoreServices has room for it).
   `PBGetVolumeInfoSync` only returns `nsvErr`; real semantics would need a volume manager.

`brand.sh` now runs `macapps/carbon.sh` after `libcompat/build.sh` (desktop images). Not rebuilt: a full `brand.sh` image.
The test image was patched with `macapps/dlprobe.sh` (debugfs writes into an APFS clone of the desktop image).

## Tool fixes

- `macapps/machcheck.py --arch arm64` (it was x86_64 only).
- `machcheck.py` never reported missing symbols with current `dyld_info`: the line format gained a leading address, so the
  import regex matched nothing and every program was "Alles da". Fixed; the tool now finds exactly the gaps above (same
  seven for the x86_64 slices, statically; with `--arch arm64` it also finds `libaafintp`'s `@rpath` as missing unless an rpath is given).

## How to repeat

```
cp -c ~/ClokeOS-work/dd/out/clokeos-2.5-arm64-desktop.img $CLOKEOS_WORK/out/d.img
CLOKEOS_ARCH=arm64 CLOKEOS_FLAVOR=desktop libcompat/build.sh S; macapps/carbon.sh S   # S = empty stage dir
macapps/dlprobe.sh d.img S /                  # system files over the root
macapps/dlprobe.sh d.img <dir with thinned libs and the dl test program> Logic
CLOKEOS_ARCH=arm64 CLOKEOS_FLAVOR=desktop ./run.sh --headless --image d.img
./vmctl.py send 'cd /Library/ClokeOS/Logic; DYLD_PRINT_APIS=1 ./dlr ./libcom-api.dylib'
```
The test program is 5 lines: `dlopen(argv[i], RTLD_NOW)` and `dlerror()`.

## Not tested

- x86_64 (dyld in TCG, `ucrun`): only the static check with `machcheck.py` (same gaps); no VM run.
- Real hardware; any function beyond `AAFCreateRawStorageMemory` + `Release`; thread-local variables, exceptions, `std::locale`/iostream paths in `libcom-api`.
- `swscanf` with wide conversions; `TickCount` and the date conversion have no test.
- `dlclose`/unload of these libs and loading them together with Darling's CoreFoundation instead of PureDarwin's (the frameworks in
  `/Library/ClokeOS/MacFrameworks`), which is how Mac programs run on ClokeOS; the test used PureDarwin's CoreFoundation (A layout).

## Conclusion for the roadmap

Package Q's dyld question is answered favorably for C/C++ code: PureDarwin's dyld accepts chained fixups, `minos 15.6` and
SDK 26.0 without any change. What blocks is libSystem/CoreServices coverage, found per missing symbol. Still open: the 10 binaries
that import `_availability_version_check` family and everything Swift (package D).
