# Browser study: a modern browser for ClokeOS

Status: study plus a baseline measurement, 2026-10-29. Figures marked *(est.)* are estimates, not measurements on ClokeOS.
Web facts (Ladybird, Servo, WebKitGTK) come from a quick public search and were **not** verified against source trees.

## 1. What is there today (measured)

arm64 desktop image (HVF VM, freshly branded 2.6): **Search = NetSurf 3 GTK3** (`/bin/netsurf-gtk3`, libcss/libdom/libhubbub,
no JS engine: no `libduktape` in the image). HTTPS works (libcurl + `/etc/ssl/cert.pem`, `curl https://example.com` verifies, 200).

![Search on en.wikipedia.org](browser-study/search-netsurf-wikipedia-arm64.png)

Wikipedia main page over HTTPS loads in 3.5 s but is broken: no SVG/WebP images (boxes), no flexbox/grid, no JS. NetSurf is a
document viewer, not a modern browser. This also holds on x86_64 (same package).

Already in the base image (arm64 and x86_64 alike, from PureDarwin's nix build): GTK 3, libsoup 3, libcurl, GnuTLS, ICU 76, cairo,
pango, freetype, Mesa (llvmpipe/lavapipe `libvulkan_lvp`, EGL/GL), **clang 21, cmake, ninja, meson, python 3.13, git**. So building
natively in the VM is possible in principle, but the VM has 4 GiB RAM and one core: not realistic for any engine below.
No GPU: everything is software rendering (virtio-gpu is scanout only).

## 2. Options

| Option | License | Size *(est.)* | Software render, no GPU | PD 20.5 fit | x86_64 + arm64 | Effort |
|---|---|---|---|---|---|---|
| **Keep NetSurf** + Duktape JS | GPL-2 | +1 MB | yes | already runs | both | Low, but gains only partial JS; **still no flex/grid**. Dead end for "modern". |
| **WebKitGTK** (2.5x, also WPE) | LGPL-2.1/BSD | 60-90 MB installed; build 8-16+ GB RAM, hours | yes (Cairo/Skia CPU path) | GTK3 + libsoup3 + ICU already present. Missing: harfbuzz?, libxml2/xslt, sqlite, woff2, brotli, libwebp/jxl, gstreamer (optional), many more. Linux-isms: multiprocess IPC uses `SOCK_SEQPACKET` (Darwin sockets behave differently, a 2021 patch note says nobody had built WebKitGTK on macOS in a long time), bubblewrap/seccomp (disable), memfd/`/proc` use, JSC JIT (MAP_JIT works, see translation-study) or LLInt-only | yes in theory | **High**: port IPC + build ~20 deps. Best engine compatibility (Safari-class) once done. |
| **Ladybird** (LibWeb/LibJS) | BSD-2 | ~100 MB build, deps via vcpkg (Skia, ICU, curl, harfbuzz, ...) | yes (Skia CPU) | Project targets Linux **and macOS**; its macOS port uses Mach ports + AppKit, Linux uses Qt6. PD has Mach IPC, no AppKit/Qt. A GTK3 or X11 front-end would be new code. Pre-alpha (alpha announced for 2026 on Linux/macOS only); web compat growing fast (passed 90% WPT in 2025) but not Safari-class. | maybe | **High**, but fewest Linux assumptions of the modern engines; clean C++23 code. |
| **Servo** (libservo, 0.4 in 2026, crates.io, GTK widget prototype exists) | MPL-2 | 50-100 MB; Rust toolchain on host | yes (software GL via surfman/Mesa) | Rust tier-3 target `aarch64/x86_64-apple-darwin` exists, but PD is not macOS: needs a custom target/sysroot; GL via Mesa EGL/X11 plausible. Web compat behind WebKit/Gecko. | maybe | High; Rust cross-build of ~700 crates. |
| **Firefox/Gecko from source** | MPL-2 | 500 MB+ build tree | yes | Needs GTK3 build (Linux-only code paths), Rust, huge build (30+ GB, hours). | x86_64 only realistic | Very high |
| **Chromium / CEF / Electron** | BSD-3 | multi-GB build | yes (SwiftShader) | Linux/Ozone-X11 or Cocoa port; neither matches PD. Sandbox, zygote, `/proc`, Linux syscalls | no | Prohibitive |
| **Firefox/Chrome/Safari Mac binary** (via MacFrameworks/Darling) | proprietary / MPL | 250 MB | needs Metal/CoreAnimation/AppKit | Mac apps run today only for tiny AppKit apps (StuffIt); Firefox needs the full Cocoa/CoreGraphics/Metal stack, JIT entitlements, XPC, and x86_64 binary only on arm64 (no translator yet). | no | Not feasible on this route |
| **Linux browser via `tux`** (ELF loader) | - | - | - | tux handles a tiny Alpine userland; browsers need clone/futex/epoll/memfd/seccomp/`/proc`, glibc/musl, GL stack. Far beyond today's tux. | no | Not feasible near-term |
| **Windows browser via Wine** (x86_64 image only) | - | 100 MB+ | CPU render ok | Wine 11 is in the x86_64 desktop image but first start takes ~13 min under TCG and wineboot still hangs (see wine notes); no arm64 Wine. | x86_64 only | Fragile, not a product |
| **Homebrew bottles** (Big Sur) | - | - | - | brew is for CLI tools; Firefox is a cask (an app bundle), WebKitGTK/Epiphany have no macOS bottles. | no | n/a |

## 3. Recommendation and order

1. **Ladybird first as a research spike, WebKitGTK as the fallback**, both hosted in the existing GTK/X11 shell (Search app in
   CloakServer keeps its name, dock icon and back/forward behaviour). Reason: only these two combine a real modern engine, software
   rendering and a license that allows shipping in the image. Ladybird has the cleaner, newer, Darwin-aware code base (its Mach IPC
   path is the only one of the engines that already speaks XNU); WebKitGTK is far more mature (Safari-class compatibility) but is a Linux
   port whose IPC and sandbox would need real porting. Servo is the third candidate and only worth it if its GTK embedding matures.
2. **Spike steps** (each is a day-scale task, none done here):
   1. Cross-build on the Mac host against the PD sysroot (`clang -target arm64-apple-macos11 -isysroot <image>`, as for CloakServer),
      engine core only, no UI: load `https://example.com` and write a PNG (Ladybird's headless screenshot mode, or WebKit `MiniBrowser --screenshot`).
      This answers the real questions: do the Mach/socket IPC, JIT-less JS, and Skia CPU raster work on PD 20.5?
   2. Window: a minimal GTK3 or plain X11 front-end that blits the engine's bitmap and forwards input (CloakServer already handles WM duties).
   3. Package as an optional download (`clokeos-install-browser`, like `clokeos-install-homebrew`), not in the base image: keeps release size down.
   4. x86_64 after arm64 (arm64 HVF is ~10x faster to test); x86_64 JS speed under TCG will be poor for testing, fine on real PCs.
3. **Meanwhile keep NetSurf as Search** (small, instant, works offline pages and docs). Cheap polish only: ship librsvg/libwebp support for
   NetSurf so images on modern sites show up (the screenshot's boxes). Do **not** invest in Duktape.
4. Not recommended now: Gecko/Chromium builds, Mac/Windows/Linux browser binaries (blocked by missing OS layers, see table).

## 4. What this session did and did not do

Done: inventory of the shipped image (libs, toolchain, NetSurf), baseline in the arm64 VM (HVF): Search + HTTPS to a real site, screenshot above.
**Not done (no cheap step exists):** no modern engine was built or run; no JS/flex/grid page renders in ClokeOS yet. Untested: all
sizes/effort/performance numbers marked *(est.)*, WebKit/Ladybird/Servo Darwin viability (only public information), x86_64 behaviour
(not run; NetSurf package identical by construction), whether `SOCK_SEQPACKET`-style IPC works on PD (not probed).
