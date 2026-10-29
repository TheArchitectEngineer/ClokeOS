# Logic Pro 12.3 on ClokeOS: gap analysis

Status 2026-10-29. Input: `Logic Pro 12.3.pkg` (1.7 GB, flat xar, from the user's disk). Everything below was measured from the package with `xlat/macho-deps.py` (new, in this repo) plus `otool`, `dyld_info`, `vtool`, `codesign`; the app was extracted to a scratch directory and deleted afterwards, nothing of it is in the repository. ClokeOS side: `macapps/` (Darling-based frameworks), PureDarwin 20.5 desktop image (listed with `debugfs`), `docs/translation-study.md`.

**Caveats up front.**
- "Present" for a symbol means *the name is exported* by the framework built in `macapps/` (or by PureDarwin's library). It does not mean the implementation works; Darling/Cocotron contain many stubs. Only `hallo.m` and the Foundation probe have actually been run.
- Nothing from Logic was run on ClokeOS. All "will fail because" statements are predictions from symbols, except where marked *measured*.
- libSystem numbers are an upper bound on what is missing: re-exports of sub-libraries (libdispatch, libclosure, ...) were not followed.

## 1. What the app is (measured)

- Installs to `/Applications/Logic Pro.app`; 104,727 files, 3,140,652 KB installed. Resources (sounds, patches, templates) are ~99% of the files; code is 98 Mach-O files, ~380 MB.
- **All 98 Mach-O files are universal x86_64 + arm64** (arm64, not arm64e). Built with SDK 26.0, `minos 15.6`, **chained fixups** everywhere (`LC_DYLD_CHAINED_FIXUPS`, no `LC_DYLD_INFO`). PureDarwin's `dyld` (x86_64) contains chained-fixup code (strings), so the format itself is probably not the blocker; not run.
- Composition: launcher `MacOS/Logic Pro` (small; principal class `CLgApplication`; links `Logic.framework`, `LogicAppFramework`, `MACore`, and the Swift runtime), 56 frameworks + 3 dylibs in `Frameworks/` (`libaafpgapi`, `libaafintp`, `libcom-api`: plain C++/CoreFoundation, no Swift), 16 MIDI controller-surface plug-in bundles (`Logic.framework/.../PlugIns/MIDI Device Plug-ins`, listed twice via symlink), `Helpers/Impulse Response Utility.app`, `Library/LaunchServices/com.apple.musicapps.InstallerHelperTool` (privileged helper), one XPC service (`MAContentDownloading.framework/.../com.apple.musicapps.MAContentInstallation.xpc`), Quick Look and Thumbnail extensions (`.appex`), a Spotlight importer. **No Audio Unit components are shipped in the app**; Logic's own instruments/effects (Alchemy, ES2, Space Designer, ...) live inside `MADSP`, `MAAudioEngine`, `MAAlchemy`, `MAGenerativeInstruments`, i.e. are ordinary code in the app.
- Largest binaries: Logic 81 MB, MADSP 53 MB, MAAudioEngine 40 MB, MAPlugInGUI 20 MB, MAPlugInGUISwift 19 MB.
- 33 of 98 binaries link `libswiftCore` (the launcher among them); 14 import SwiftUI (Logic itself, MAPlugInGUISwift, MASwiftUIControls, MAApplication, MATower, MAGenerativeInstrumentsUI, MAMobileGeneralUI, MAMixer, ChordsKit, MAToolKit, MAStepSequencer, MASwiftUtilities, MAAccessibility, MACore). The UI is a hybrid AppKit/SwiftUI app.
- Dependency layering (internal): `libswiftCore` → `InternalCollectionsUtilities`/`DequeModule`/`OrderedCollections` (swift-collections, only Foundation + Swift) → `MACore` (needs AppKit, Metal, QuartzCore, CoreImage, AudioToolbox, SwiftUI, Combine, ...) → `MAAudioEngine`/`MADSP`/`MAFiles` → `Logic`.

## 2. Imported symbols per system library (x86_64 slice, unique symbols over all 98 files)

"Swift" = mangled `_$s…` symbols (need a Swift overlay/runtime, not just C/ObjC). "Name present" = exported by ClokeOS' build (non-Swift symbols only; see caveats).

| Library | Imports | of which Swift | Name present (non-Swift) | Status on ClokeOS |
|---|---:|---:|---|---|
| libSystem | 564 | 0 | 482 of 564 (upper bound) | **partial**: missing `os_workgroup_*` (audio real-time threads), `dispatch_block_*`/`dispatch_get_specific`, `_dyld_get_image_*`, `__tlv_*`, `thread_policy`, `kevent64`, `getattrlistbulk`, `fsgetpath`, `CC_MD5/SHA1`, long-double math, `os_signpost` … (82 names; some may sit in re-exported dylibs) |
| libc++ | 383 | 0 | 383 | **present** (PureDarwin libc++ 1.dylib; ABI age vs. libc++ 2000.63 of the app unverified) |
| libobjc | 90 | 0 | 90 | **present** (PureDarwin objc4) |
| libz | 18 | 0 | 18 | **present** |
| libicucore | 1 | 0 | 0 | partial (PD ships ICU 76 as libicucore, symbol not matched) |
| libsqlite3, libxml2, libbz2, libxar, libpcre, libAppleArchive | 44 / 22 / – / – / 4 / 19 | 0 | 0 | **missing** (not in the image; all are open source except AppleArchive → buildable, small) |
| Foundation | 654 | 423 | 194 of 231 | **partial** (Darling): 37 missing mostly error-domain constants, `NSExtensionMain`, iCloud metadata keys. The 423 Swift symbols are the Darwin Foundation *overlay* (JSONEncoder/Decoder, Data, Date, URL …): **missing** |
| CoreFoundation | 401 | 0 | 396 | **present** (Darling CF) |
| AppKit | 553 | 2 | 506 of 551 | **partial**: names mostly there (Cocotron); missing text styles (`NSFontTextStyle*`), accessibility, tracking notifications. Behavior vs. macOS 15 AppKit (layer-backed views, NSHostingView, dark mode, …) unknown/likely very incomplete |
| CoreGraphics | 293 | 20 | 239 of 273 | **partial** (Onyx2D): no `CGLayer`, no `CGPDFContext…`, rounded-rect/stroke path helpers, `CGEventSource…` |
| QuartzCore | 84 | 0 | 60 | **partial**: no `CATransform3D*` helpers, `CAMetalLayer`, `CADisplayLink`, emitter constants. Compositor behavior unknown |
| CoreText | 60 | 0 | 51 | **partial**: font descriptor copy/with-feature/variation, `CTFontManager*` |
| CoreData | 35 | 4 | 23 | **partial** |
| ImageIO | 16 | 0 | 7 | **partial** |
| CoreImage | 12 | 0 | 5 | **partial** |
| ApplicationServices | 29 | 0 | built (Darling), not matched | partial |
| CoreServices | 125 | 0 | not matched | PD ships a `CoreServices.framework` (own, small); Logic uses Carbon-era File Manager (`FSGetCatalogInfo`, `FSPathMakeRef`, aliases), `FSEvents`, `MDQuery/MDItem` (Spotlight), `AE*` Apple Events, Handle Manager → **partial/missing** |
| Security | 26 | 0 | not matched | PD ships Security.framework; Logic uses `Authorization*`, `SecStaticCode*`, `SecRequirement*`, CMS decoder → **unchecked**, code-signing parts **missing** |
| IOKit, SystemConfiguration, DiskArbitration | 20, 12, – | 0 | not matched | present in the image (own builds); symbol coverage **unchecked** |
| Carbon | 20 | 0 | – | **missing** (HIToolbox event loop, TIS keyboard layouts) |
| Accelerate | 177 | 0 | – | **missing** (vDSP, vImage, BLAS/LAPACK; heavy in DSP code) |
| AudioToolbox | 98 | 0 | – | **missing** (AudioComponent host, AudioFile, AudioConverter, AUEventListener/AUParameter) |
| CoreAudio, AudioUnit | 21, 8 | 0 | – | **missing** (AudioObject/AudioDevice HAL) |
| CoreMIDI | 54 | 0 | – | **missing** |
| AVFoundation / AVFAudio / CoreMedia / CoreVideo / VideoToolbox / MediaToolbox / AVKit | 53 / 24 / 63 / 20 / 1 / 1 / 1 | | – | **missing** (video tracks, audio file I/O, tap/sink) |
| SwiftUI | 2076 | 2076 | – | **impossible** as is (closed, huge, imported by 14 binaries incl. the main one); only a reimplementation could help (e.g. the open OpenSwiftUI project; completeness unverified, likely far from enough for 2000 symbols) |
| Combine | 179 | 179 | – | missing; Swift-only, open reimplementations exist (OpenCombine), unverified for this ABI |
| Metal / MetalKit / MPS | 9 / 2 / 1 | | – | **impossible** as is (closed). Surprisingly few imports; 7 binaries (Logic, MACore, MAToolKit, MAVectorUIKit, MAPlugInGUISwift, MAWorkspace, MAMachineLearning). Whether Logic starts without a Metal device is unknown |
| WebKit, JavaScriptCore | 6, 41 | | – | missing (2 binaries each); WebKit/JSC not buildable cheaply; probably stub-able if only help/web views |
| ScreenCaptureKit, StoreKit, Network, CryptoKit, UserNotifications, GameController, MultipeerConnectivity, QuickLookUI, PDFKit, GameplayKit, CoreMotion, iTunesLibrary, DiscRecording (85!), UniformTypeIdentifiers, CoreML (22), DeveloperToolsSupport, Charts (51), RealityKit/RealityFoundation (125) | small each | mostly Swift | – | missing; closed. Mostly stub candidates (ScreenCaptureKit: 4 symbols in 1 binary; DiscRecording: CD burning). CoreML is used by MADSP, MAMusicAnalysis, MAMachineLearning |
| Private: PackageKit, AssetCacheServices, libMobileGestalt, Cinematic | – | | – | missing, closed (PackageKit is used by the content installer XPC service and MACore) |
| Swift runtime | libswiftCore 1168 (1040 Swift + 127 C) | | – | **missing**. Plus overlays imported: Dispatch 59, os 37, Concurrency 61, CoreFoundation 14, UniformTypeIdentifiers 14, ObjectiveC 9, Observation 9, XPC, IOKit, simd, Metal, CoreImage, QuartzCore, CoreMIDI, CoreAudio, Accelerate, Spatial, OSLog … (30 `libswift*.dylib` on the launcher alone) |
| `<weak-def-coalesce>` | 2686 | | | C++/Swift weak definitions exported by the app itself; needs dyld weak coalescing to work with chained fixups (should, untested) |

Summary: of ~8,900 (library, symbol) pairs the app imports from outside itself, roughly **3,000 are Swift symbols of SwiftUI/Combine/Foundation overlay/RealityKit/Charts (closed or overlay)**, ~1,800 are Swift runtime/overlays, and the C/ObjC remainder splits into roughly 2,000 that Darling/PureDarwin name-cover and ~2,000 that nothing covers (Audio*, Accelerate, Metal, AV*, CoreMedia, …).

## 3. What ClokeOS provides today

- Kernel and base: XNU 20.5, PureDarwin dyld/libSystem/libobjc/libc++/libc++abi/ICU, launchd, libxpc (older), configd, notifyd, mdnsd; Mesa gallium + `libvulkan_lvp` (lavapipe), OpenGL/EGL stubs, X11 and Wayland libraries, libcurl, libdbus, wine (separate).
- `macapps/`: CoreFoundation, Foundation (24/25 probe checks), AppKit/Cocoa (Cocotron), CoreGraphics via Onyx2D, CoreText, QuartzCore, CoreImage, CoreData, ImageIO, ApplicationServices; x86_64 and arm64; a Cocoa window runs. These live in `/Library/ClokeOS/MacFrameworks` and are used through `DYLD_FRAMEWORK_PATH`, because they cannot replace PureDarwin's own CoreFoundation inside system daemons. Logic's absolute install names (`/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation`) rely on that path redirect or on symlinks; `Versions/C` vs. the built layout is unchecked.
- Audio: kernel `IOAudioFamily` and `RavynHDAudio` are in the image, but no user-space HAL, no CoreAudio; audio works "on most hardware: no" (README).
- Translation: `xlat/ucrun` runs x86_64 Mach-O (static and dynamic via guest dyld) under Unicorn, slow, threads in progress; native arm64 ClokeOS can run Logic's arm64 slice directly, which makes `ucrun` unnecessary for this goal if arm64 is chosen (and real-time audio could never run under Unicorn).
- Not present: Swift runtime and overlays, libdispatch workgroups, XPC services for apps (launchd/libxpc exist but app-style XPC bundles, SMJobBless, LaunchServices, Spotlight, Quick Look: none), Security/code-signing, GPU API, audio stack, MIDI, AV stack, SQLite/xml2/xar.

## 4. The installer, and what a replacement must do

Measured from the pkg: `Distribution` is a trivial single-choice installer (hostArchitectures arm64,x86_64, install location `/Applications`, auth root); `PackageInfo` lists the bundle tree; **`Scripts` is an empty cpio archive: there is no preinstall/postinstall**, no AU/plug-in registration, no helper installation at install time. Payload is a gzip-compressed cpio of `Logic Pro.app`.

An installer replacement therefore only needs to:
1. Read the flat xar (needs an xar reader, e.g. libxar or the small xar format in a script), extract `Payload` (gzip + cpio), preserve modes, symlinks and `root:wheel` ownership from the cpio; ~3.1 GB, 104,727 files.
2. Optionally write the receipt (`/var/db/receipts/com.apple.logic10.plist` + BOM) so `pkgutil`-like tools see it.
3. Optionally register documents with LaunchServices (33 document types, 19 exported UTIs), the Spotlight importer and the Quick Look/Thumbnail extensions. Not needed to start the app.

What the app itself does on first launch and needs the OS for (not done by the installer): the privileged helper via `SMJobBless` (`SMPrivilegedExecutables` requires an Apple-anchored signing requirement; the helper installs content into system locations; needs launchd + Security + `SecRequirement` evaluation or a shim that returns "valid"); downloading additional sound libraries through the XPC service `com.apple.musicapps.MAContentInstallation` (PackageKit private framework + network); AU cache `~/Library/Preferences/com.apple.audio.AudioComponentCache.plist`; microphone/Apple-Events/removable-volume permission prompts (TCC; seven `NS*UsageDescription` keys); an App Store receipt directory `_MASReceipt` and StoreKit imports exist in the bundle. Receipt/licence checks are part of the product; whether and how a legitimately purchased copy is accepted on a non-Apple OS is a licensing question for the user, not something this document proposes to work around. Observed: the launcher's signature is ad-hoc with no entitlements (other binaries not checked).

## 5. Roadmap: independent work packages

Effort is a rough guess for one developer-session scale: S ≈ days of agent work, M ≈ weeks, L ≈ months, XL = open-ended. "Needs" lists hard dependencies.

| # | Package | Effort | Needs | Notes |
|---|---|---|---|---|
| A | **Tracking** (done in part): `xlat/macho-deps.py` + this table re-run after each step, extended to count Swift vs. C | S | – | Gives the metric |
| B | **Plain C/C++ libs**: build libsqlite3, libxml2, libbz2, libxar, libpcre, libAppleArchive for ClokeOS (x86_64 + arm64) | S | – | Unblocks 5 binaries' link closure |
| C | **libSystem gaps**: `os_workgroup_*`, `dispatch_block_*`, `_dyld_get_image_*`, `__tlv_*`, `kevent64`, `getattrlistbulk`, `fsgetpath`, `_availability_version_check` reporting a 15.x-compatible version (apps decide features by it; ClokeOS reports Darwin 20.5/macOS 11-ish) | M | PureDarwin libSystem sources | Real-time audio threads need workgroups and time-constraint policy; check xnu 20.5 supports `THREAD_TIME_CONSTRAINT_POLICY` (it does in XNU, untested here) |
| D | **Swift runtime**: build `libswiftCore`, `_Concurrency`, `_StringProcessing`, `libswiftDarwin`, `Dispatch`, `ObjectiveC`, `CoreFoundation` overlays for Darwin x86_64 + arm64 from apple/swift (toolchain version must match SDK 26.0, i.e. Swift 6.2 ABI) with install names `/usr/lib/swift/*.dylib` | L | B? C, working objc4 | Biggest single unknown: Apple's closed overlays (XPC, IOKit, os, simd, Metal, QuartzCore, CoreAudio, CoreMIDI, AVFoundation, Accelerate) have no open source for the SDK-26 ABI; they are `weak` links on the launcher, so some may be skippable |
| E | **Swift Foundation overlay** (423 Foundation Swift symbols): swift-foundation / swift-corelibs on top of Darling Foundation, bridging ObjC ↔ Swift | L | D, Foundation completeness | |
| F | **XPC/launchd substitute for app services**: XPC service bundles, `SMJobBless` shim, LaunchServices (open document, UTI), MachServices lookup | M | PureDarwin launchd/libxpc evaluation | Used by content installer, AUv3 hosting, Quick Look |
| G | **Audio stack**: CoreAudio HAL (AudioObject/AudioDevice IOProc over a kernel/user driver: `RavynHDAudio`, USB audio class driver, virtio-snd in VMs), AudioToolbox (AudioComponent registry/host incl. AUv3, AudioFile/ExtAudioFile/AudioConverter with AAC/ALAC/FLAC/MP3), AudioUnit property model | XL | C (workgroups), kernel driver work | Core of the product; ~140 symbols imported, but semantics (real-time callbacks, host time, latency) are what matters. Check Darling for existing partial code first (not looked at) |
| H | **CoreMIDI** (USB MIDI class driver or user-space USB access, virtual endpoints, UMP `EventList` variants) | M–L | USB stack (xHCI present) | |
| I | **Accelerate subset**: vDSP + vImage + BLAS needed by symbol list (177) | M | – | Can be written/ported (e.g. OpenBLAS + own vDSP loops); only the imported subset |
| J | **GUI**: SwiftUI substitute or avoidance | XL | D, E, AppKit | 14 binaries / 2,076 symbols; no known avoidance since the main binary uses it. Realistic options: (1) OpenSwiftUI-class reimplementation, (2) accept that only non-SwiftUI helper components run first |
| K | **AppKit/QuartzCore completeness** for macOS 15 behavior, missing names listed in section 2, layer-backed views and a real compositor | L | – | The 45+37+34 missing names are the cheap part |
| L | **Metal**: either a stub device that reports "no GPU" (if Logic tolerates it; to be tested) or a Metal API subset over Vulkan (lavapipe/virtio-gpu) | S (stub) / XL | – | Only 12 imported symbols but used by Logic, MACore, MAToolKit |
| M | **Video/AV**: AVFoundation, CoreMedia, CoreVideo, VideoToolbox for the video track (`MAVideo`), AVAudioEngine bits used by MAAudioEngine | L | G | |
| N | **Security / code-signing / TCC shims**: `SecStaticCode*`, `SecRequirement*`, `Authorization*`, entitlements, microphone/Apple-Events prompts (always allow) | M | F | Licensing is separate (see section 4) |
| O | **Stubs for the long tail**: ScreenCaptureKit, WebKit, JSC, StoreKit, Network, CryptoKit, DiscRecording, MultipeerConnectivity, Charts/RealityKit (Swift), PackageKit, CoreML | M | D | Make loading succeed; failing at use is fine initially |
| P | **Installer**: xar + gzip/cpio extractor, modes/ownership, optional receipt | S | B | Section 4 |
| Q | **Mac-OS identity**: `SystemVersion.plist`/`sw_vers`, `LSMinimumSystemVersion` 15.6 and `LC_BUILD_VERSION` handling in dyld, `Versions/C` framework layout and install-name redirects | S–M | – | Test first, before building anything big |

Suggested order: Q and B (cheap, show whether dyld accepts the binaries), then C+D (everything Swift depends on them), then in parallel I, G, K, O; J is the real risk and should be decided (substitute vs. stop) before investing deeply in G.

Honest overall estimate: Logic will not start before D, E, J, G and at least stubs for most of O are done. That is a multi-year project; this document does not claim it is achievable, only what the pieces are.

## 6. Tests that show progress (in order of difficulty)

1. `xlat/macho-deps.py <extracted app> --provider …` : sum of unresolved C/ObjC symbols per library goes down (re-run after each package; the `--provider` arguments are listed in the commit message of the doc).
2. `macapps/machcheck.py --frameworks …` reports no missing libraries for `libcom-api.dylib`, `libaafpgapi.dylib`, `libaafintp.dylib` (CoreFoundation + libc++ only), then `OMF.framework` (Carbon/CoreServices/CoreFoundation).
3. `dlopen` of those four under `ucrun` and natively on arm64 (C++ only, a real first test of chained fixups + weak coalescing + `minos 15.6` in PureDarwin's dyld).
4. `InternalCollectionsUtilities`, `OrderedCollections`, `DequeModule` load (needs only Swift runtime + Foundation; the first Swift milestone, package D).
5. `InstallerHelperTool` (CoreFoundation, Security, Foundation only) starts and exits cleanly; `MAContentInstallation.xpc` starts.
6. `MACore.framework` loads (needs AppKit, Combine, SwiftUI, Metal, … at least as stubs).
7. `MAAudioEngine`, `MADSP` load; a small test calls `AudioComponentFindNext` and plays a sine through the HAL (package G).
8. `Impulse Response Utility.app` shows a window (AppKit + Accelerate + AudioToolbox; no SwiftUI import in its own binary).
9. The launcher `Logic Pro` reaches `NSApplicationMain` and shows any window.
10. Logic opens an empty project, plays a software instrument with sound.

## 7. Unknowns

- Whether PureDarwin's dyld/kernel accept `minos 15.6` and Swift 6.2 binaries (never tried).
- How complete Darling/Cocotron are behind exported names (stubs).
- Whether Logic tolerates absent Metal, ScreenCaptureKit, WebKit, StoreKit.
- Whether Darling has partial CoreAudio/AudioToolbox code to start from (not checked).
- Binary-level compatibility of Apple's closed Swift overlays (not available) with an open-source Swift build.
- Behavior of real-time audio under ClokeOS' scheduler and HDA driver.
