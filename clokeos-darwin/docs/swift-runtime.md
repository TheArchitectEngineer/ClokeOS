# Swift runtime on ClokeOS: feasibility study and first proof

Goal behind it: Logic Pro 12.3 (universal x86_64 + arm64) links `libswiftCore` and about 30 `/usr/lib/swift/libswift*.dylib`
overlays, SwiftUI and Combine (see `translation-study.md`, section 6). On a real Mac all of these live in the dyld shared
cache; PureDarwin 20.5 has none of them (`/usr/lib/swift` does not exist in any image). Date of the measurements: 2026-10-29.

## 1. Result in short

- **The Swift runtime itself is open source and builds for Darwin 20 without LLVM and without building a compiler.**
  `libswiftCore.dylib` (Apache-2.0 with Runtime Library Exception) is built from the `swift-6.4.0-RELEASE` tag with the host's Swift 6.4 toolchain
  via the new standalone project `Runtimes/Core`. Source download: 105 MB working tree / 16 MB `.git` (partial sparse clone of one repository).
  Result: 7.7 MB (arm64) and 7.8 MB (x86_64) dylibs that depend only on `libSystem`, `libc++` and `libobjc`. Of the 183 undefined symbols of the arm64 one, all but a weak
  import (`os_system_version_get_current_version`) are exported by the images' own libs; for x86_64, the 17 missing ones are the weak import plus long-double libm
  (`sinl`, `expl`, ... for `Float80`), which only matter if a program uses `Float80` (lazy binding, not touched by the tests).
- **A Swift program runs natively on the arm64 ClokeOS (HVF VM) with that dylib**, including structs, protocols/existentials, closures,
  generics, enums with payload, `throw`/`catch`, `Mirror`, `Set`/`Dictionary`, random, floating point formatting, Unicode strings and a Swift
  class derived from `NSObject` (ObjC metadata registered through PureDarwin's `libobjc`). Details in section 5.
- **One real incompatibility found and patched:** the arm64 `libobjc` of PureDarwin has the old (iOS-style) isa mask `0x0000000ffffffff8`, the open-source
  runtime hardcodes `0x00007ffffffffff8` for macOS arm64 (and Apple's real arm64 macOS libobjc matches that). Without the patch
  (`swift/patches/isa-mask-old-objc.patch`, one line plus a define) every ObjC-interop object access (`throw`, any `NSObject` subclass) crashes in `swift_getObjectType`.
  x86_64 is not affected (same mask `0x00007ffffffffff8`).
- **Not open and not buildable:** the Apple overlays (`Darwin`, `ObjectiveC`, `CoreFoundation`, `Dispatch` wrapper as shipped, `Foundation`, `CoreAudio`, `CoreMIDI`,
  `AVFoundation`, `Metal`, `QuartzCore`, `XPC`, `os`/`OSLog`, `Accelerate`, `simd`, ...), plus SwiftUI and Combine. Their API shape is public as text
  (`*.swiftinterface` in the SDK), the code is not. For ClokeOS that means: write stub/shim dylibs against those interfaces (section 3), exactly the thin layer
  that Darling-style framework work needs anyway.
- **The x86_64 build runs in the arm64 VM through `xlat/ucrun`** (Unicorn, the guest's own x86_64 `dyld`/`libSystem`/`libobjc`/`libc++` from `XLAT_ROOT`): `hello`, `hello2`, `objc` give the same
  output and exit codes (3, 0, 0) as natively; each takes less than a second. `ucrun` needed **no change**; it only reports `fsgetpath` (427) and `madvise` (75) as not implemented, and the runtime copes.
- **A hidden constraint for Logic itself:** the runtime must be built with deployment target 11. With 15 the compiler emits `objc_claimAutoreleasedReturnValue` (macOS 12+ libobjc),
  PureDarwin's 781-era `libobjc` does not have it, and `dyld` stops with "Symbol not found" at load. Logic (minimum macOS 15.6) is compiled that way, so ClokeOS needs either a newer `libobjc`
  (Apple's `objc4` is open source, APSL-2.0, but not tried here) or shims for the newer entry points (only this one symbol was hit, `dyld` reports one missing symbol at a time).

## 2. What is open source, what is buildable, what is closed

All of the following was checked against the trees at tag `swift-6.4.0-RELEASE` and the GitHub repositories (licenses from the repos' `LICENSE`/API, sizes of the repositories including history).

| Component | Source | License | Darwin 20 build | Remark |
|---|---|---|---|---|
| `libswiftCore` (stdlib + runtime) | `swiftlang/swift` `Runtimes/Core` | Apache-2.0 + Runtime Library Exception | **done**, arm64 7.7 MB, x86_64 7.8 MB (build about 15 minutes each on an M-series Mac) | host Swift 6.4 as compiler, no LLVM checkout (bundled `LLVMSupport`). Needs the patch for old arm64 `libobjc` |
| `libswiftSwiftOnoneSupport` | same | same | built (0.4 MB) | only for `-Onone` binaries; Logic is optimized |
| `libswift_Concurrency` | same (`Runtimes/Core/Concurrency`) | same | not tried, option `SwiftCore_ENABLE_CONCURRENCY`; default executor uses `libdispatch`, which is in the images' `libSystem` (70 `dispatch_*` exports) | Logic needs it (async/await) |
| `libswift_StringProcessing`, `_RegexParser`, `RegexBuilder` | `swift-experimental-string-processing` (6 MB), pulled by `Resync.cmake` | Apache-2.0 | not tried | pure Swift, no OS dependence expected |
| `libswiftSynchronization`, `libswiftObservation`, `libswiftDistributed`, `Cxx`, `Volatile`, `Differentiation` | `swift` `Runtimes/Supplemental` | Apache-2.0 | not tried | `Observation` is what `@Observable` SwiftUI models need; macros of the *user* code run at compile time, not here |
| `libswiftSystem` | `apple/swift-system` | Apache-2.0 | not tried | small |
| `libdispatch` + `Dispatch` Swift overlay | `swift-corelibs-libdispatch` (5 MB) | Apache-2.0 | not needed for C; the Swift overlay source is open, but the macOS `libswiftDispatch.dylib` is Apple's build | libSystem already has the C library |
| Foundation | `swift-foundation` (10 MB) + `swift-corelibs-foundation` (27 MB) | Apache-2.0 | not tried | it is the *Linux/Windows* Foundation; Apple's macOS Foundation (ObjC, NSString bridging, ~all of Logic's Foundation use) is closed. Darling/Cocotron Foundation (already in the images) fills the ObjC side, the Swift `Foundation` overlay on top of it is a closed shim |
| `Darwin`, `ObjectiveC`, `CoreFoundation`, `IOKit`, `UniformTypeIdentifiers`, `os` overlays | were in `stdlib/public/Darwin` until about Swift 5.3 (checked: tags 5.3 yes, 5.7/5.8/5.9/6.0/6.4 no) | - | **closed since** | sizes in the SDK stubs: Darwin 153 exported Swift symbols, ObjectiveC 84, CoreFoundation 549, os 755 |
| `CoreAudio` (786), `CoreMIDI` (288), `AVFoundation` (1034), `Metal` (181), `MetalKit` (3), `QuartzCore` (14), `XPC` (633), `OSLog` (12), `Accelerate` (4109), `simd` (1609), `Network` (20506) overlays | Apple only | proprietary | impossible | numbers are exported Swift-mangled symbols per `libswift*.tbd` in the SDK (all architectures together, rough). Most are thin wrappers over a C/ObjC framework, i.e. the framework underneath is the real problem (no CoreAudio/Metal/AVFoundation on ClokeOS) |
| SwiftUI, Combine, AppKit overlay | Apple only | proprietary | impossible | SwiftUI is a full framework (`SwiftUI.framework`), not an overlay; no open replacement exists; this is the real blocker for Logic's UI, not the runtime |

Observations that change the plan:
- The SDK's `libswift*.tbd` list every exported symbol. A dylib with the right install name and that symbol list, with all functions trapping, is mechanically generable;
  replacing used functions by real code is then a per-symbol job. `swiftinterface` files (e.g. 684 lines for CoreAudio, 2004 for AVFoundation) show the Swift API;
  non-`@inlinable` bodies are missing. SDK files fall under the Xcode license, so they must not be shipped; generate stubs on the user's Mac or from a recorded symbol list.
- With a macOS 15 deployment target (Logic needs 15.6) `swiftc` marks the overlay libs `LC_LOAD_WEAK_DYLIB` (checked on a test binary: CoreFoundation, Dispatch,
  IOKit, ObjectiveC, XPC, `_Builtin_float`), so a missing overlay does not stop `dyld`; only *used* symbols matter. A bare `print` program links just `libSystem` and `libswiftCore`.
- The toolchain on the build host (Xcode CLT, macOS 27 SDK) no longer ships x86_64 slices of `libswiftCompatibility56/Concurrency/Packs.a` (checked: "fat file missing arch x86_64"),
  and `swiftc -static-stdlib` is "no longer supported for Apple platforms". So there is no ready static runtime, and x86_64 Swift programs need a deployment target of 15 or higher on this host.

## 3. How an Apple-compiled Swift binary finds its runtime, and what must exist

Observed with the host toolchain (`swiftc -target arm64-apple-macos15`, same mechanism for x86_64):
- The executable has `LC_LOAD_DYLIB /usr/lib/swift/libswiftCore.dylib` (absolute) and, depending on imports, `LC_LOAD_WEAK_DYLIB /usr/lib/swift/libswift<Overlay>.dylib`, plus `LC_RPATH /usr/lib/swift`.
  `import Foundation` adds Darwin, CoreFoundation, Dispatch, IOKit, ObjectiveC, XPC and Foundation overlays. Deployment targets below 10.14.4 embed Swift's own copy
  instead (`@rpath/libswiftCore.dylib` in `Frameworks/`; the toolchain has these copies in `lib/swift-5.0/macosx`, x86_64 only, Swift 5.0-era, linking Foundation, so no use here).
- On ClokeOS the paths are looked up on disk: put the runtime into `/usr/lib/swift/`. Build the dylib with `-install_name /usr/lib/swift/libswiftCore.dylib`
  (the proof uses `@rpath` with `@executable_path`, which the native guest `dyld` and the guest `dyld` under `ucrun` both resolve).
- Needs at load time, all present in the images: PureDarwin's `dyld` (no shared cache needed), `libSystem` (monolithic), `libc++.1`, `libobjc.A` (781-era, 0x...ffffffff8 isa mask on arm64 = the patch).
- Metadata discovery: the compiler emits `__swift5_types`, `__swift5_protos`, `__swift5_proto`, `__swift5_fieldmd`, `__swift5_typeref`, `__swift5_reflstr`, `__swift5_entry`
  (checked in the test binaries). `libswiftCore` registers for images via `_dyld_register_func_for_add_image` (exported by the guest's libdyld), so nothing extra is needed from the OS.
  `__objc_imageinfo` carries the Swift bits for `libobjc`; classes derived from `NSObject` work (test `objc`).
- Foundation bridging: `String` <-> `NSString`, `Error` <-> `NSError`, `Array` <-> `NSArray` only act when `Foundation` classes exist. The runtime tolerates a missing `NSError`
  (`ErrorObject.mm`: "do something even without Foundation"), which is why `throw` works here without any Foundation. With Darling's Foundation present, `NSError` is found by `objc_lookUpClass`; whether the
  layout assumptions of `__SwiftNativeNSError` (checked only by an `assert`) hold against Darling's `NSError` is untested.
- `libswift_Concurrency` and `libswift_StringProcessing` are implicit imports of Swift >= 5.5/5.7 code (not linked when unused at deployment target 15, see above); a program using `async` needs `libswift_Concurrency` and its executor (libdispatch).
- Swift 5.x compatibility shims (`libswiftCompatibility56.a`, `...Concurrency.a`) are static archives linked into the *app* for deployment targets < 12/13; irrelevant for Logic (15.6). The test programs here are built for macOS 11 and replace the archives' force-load markers by empty symbols (they have no x86_64 slice on this host).
- Chained fixups and code signing: the test binaries are unsigned and built for macOS 11. A variant of `hello` linked for macOS 13 (arm64, has `LC_DYLD_CHAINED_FIXUPS`) also ran natively in the VM, so the guest `dyld` reads chained fixups (not tried under `ucrun`/x86_64, where the older linker would not emit them for macOS 11 anyway). Entitlements and signature checks are not enforced by PureDarwin's kernel.

## 4. What the images contain

Checked in PureDarwin 20.5 (`puredarwin.img`, x86_64) and ClokeOS 2.6 arm64 desktop image:
`/usr/lib`: `dyld`, `libSystem.B`, `libc++.1`, `libc++abi`, `libobjc.A`, `libicucore.A` (arm64: a 16 KB shim; x86_64: ICU 76 libs `libicuuc/libicui18n/libicudata`), `libCoreFoundation`, `libFoundation` (Darling-based),
`libSecurity`, `libSystemConfiguration`, `libIOKitCF`, `libDiskArbitration`, `libiconv`, `libcharset`, `system/libdyld` (+ `libclokeos_compat` on arm64). **No `/usr/lib/swift`, no Swift anywhere, no `libswift*`.**
`libSystem` exports `dispatch_*` (70), `xpc_connection_create`, blocks, `_dyld_register_func_for_add_image`. No `launchd`/XPC services, no dyld shared cache.
`libobjc` x86_64: isa mask `0x00007ffffffffff8`; arm64: `0x0000000ffffffff8` (iOS layout, the cause of the patch).

## 5. The proof

Files: `swift/hello.swift` (print, struct, protocol + existential array, closure, `String`, `Array`, `Dictionary`), `swift/hello2.swift` (classes/ARC/`deinit`, generics, indirect enum, `throw`,
`Mirror`, key path, `Set` hashing with random seed, `SystemRandomNumberGenerator`, float formatting, Unicode counts), `swift/objc.swift` (`NSObject` subclass), `swift/overlay-stubs.c`
(two empty marker symbols that stand in for `libswiftObjectiveC`/`lib_Builtin_float`, see below), `swift/crash.c` (a `DYLD_INSERT_LIBRARIES` crash reporter for the guest: PC, LR, fault address, frame chain with image names; it found the isa-mask bug in one run),
`swift/build.sh` (clone, patch, build runtime and programs for both architectures).

Key facts and traps from building:
- `Runtimes/Core` needs `-DCMAKE_POLICY_DEFAULT_CMP0195=NEW` with CMake 4.x (otherwise the module path `Swift.swiftmodule` collides with the nested `.swiftinterface` directory), `-DCMAKE_OSX_SYSROOT` (otherwise CMake's Swift check fails) and
  `-DCMAKE_Swift_COMPILER_TARGET=<arch>-apple-macos<N>` (otherwise the Swift half of the stdlib is compiled for the host: an x86_64 build then fails at link with an arm64 `Swift.o`).
- The programs must **not** autolink: the SDK's `libswiftCore.tbd` would put `/usr/lib/swift/libswiftCore.dylib` (the Mac's cache copy) into the binary, which silently runs the Mac's runtime. Compile with `-c`, link with `clang` against the own dylib and `-Wl,-ignore_auto_link`.
- `import ObjectiveC` makes the compiler reference `__swift_FORCE_LOAD_$_swiftObjectiveC` and `..._swift_Builtin_float`: the closed overlays' autolink markers. Two empty symbols (`overlay-stubs.c`) are enough for this test. Classes need `-lobjc` (`__objc_empty_cache`).
  `@objc` requires `import Foundation` for the compiler; the test therefore uses the ObjC runtime without `@objc` attributes.
- Native arm64 result in the VM (ClokeOS 2.6 arm64 desktop image clone, `run.sh --headless`, files via `curl http://10.0.2.2:PORT`): `hello`, `hello2` (`ALL OK`), `objc` (`42 7 true main.Foo true`) run with the output of the host Mac and the expected exit codes (3, 0, 0).
  Before the isa patch: a three-line `throw`/`catch` program and `objc` died with `SIGSEGV` in `swift_getObjectType`, address `0x41a102da51a0` (an isa with `magic`/`extra_rc` bits not masked off).

## 6. x86_64 slice through `ucrun`

Rig: clone of the ClokeOS 2.6 arm64 desktop image (HVF), `CLOKEOS_WORK` private, `xlat/xroot.sh` libs in `/tmp/x`, `XLAT_ROOT=/tmp/x /tmp/ucrun ./hello` in the folder that contains the x86_64 `hello` and its `libswiftCore.dylib`.
The x86_64 binaries were built by `swift/build.sh` (same sources, `-arch x86_64`), also verified under Rosetta on the host Mac.

| Program | arm64 native in VM | x86_64 via `ucrun` in VM |
|---|---|---|
| `hello` (print, struct, protocol, closure, String, Array, Dictionary) | ok, exit 3 | ok, exit 3 |
| `hello2` (ARC, generics, enum, throw, Mirror, Set, random, float, Unicode) | ok, `ALL OK` | ok, `ALL OK` |
| `objc` (`NSObject` subclass over the guest's `libobjc`) | ok | ok |

Messages from `ucrun`: `Systemaufruf 427 nicht umgesetzt` (`fsgetpath`, twice, at start-up) and, in `hello2`, `75` (`madvise`) once; both answered `ENOSYS` and the runtime does not care.
Speed was not measured beyond "under one second" (clock with 1 s resolution in the guest shell).

## 7. What this means for Logic Pro and what is untested

Order of work, smallest first: (1) `/usr/lib/swift` with the built `libswiftCore` (+ `_Concurrency`, `_StringProcessing`, `Synchronization`, `Observation`, `Distributed`, `_RegexParser`, `RegexBuilder`, `System`); (2) generated stub dylibs for the closed overlays from `libswift*.tbd`, trapping on first use, to see which symbols Logic actually touches;
(3) real shims for `Darwin`/`ObjectiveC`/`CoreFoundation`/`Dispatch`/`Foundation` (largest value, small code: they mostly re-export C/ObjC functions); (4) the frameworks underneath (CoreAudio, AVFoundation, Metal, SwiftUI) - the actual wall, unrelated to this study.

Untested: the x86_64 build on real use beyond section 6; `libswift_Concurrency` and everything beyond `libswiftCore`; Swift code that touches Foundation, threads (`pthread` use by the runtime was not exercised beyond a single thread), `Task`; a real Apple-built Swift app (none was available to run, Logic is not installed here and
would anyway need AppKit); the build script from a clean directory on other machines; arm64e (Apple's slice for system libs; third-party apps like Logic use plain arm64 for the app, but their dependencies `libswiftCore` would be arm64e in the cache, we build plain arm64, which is what an arm64 app expects when it is itself arm64);
licenses were read from the repositories' metadata, not reviewed by a lawyer.
