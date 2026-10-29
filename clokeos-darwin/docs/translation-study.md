# Translation layer study: x86_64 <-> arm64 (a Rosetta 2 for ClokeOS)

Status: study plus a tiny proof (`xlat/`), 2026-10-29. Goal: run foreign-architecture programs in both directions
(x86_64 on arm64, arm64 on x86_64) for all three binary kinds: Mach-O, Windows PE (through Wine), Linux ELF (through `tux`).
Figures marked *(est.)* are estimates from public experience with these projects, not measurements on ClokeOS.

## 1. Facts about the platform (PureDarwin XNU 20.5, no kernel patch)

Measured in the arm64 VM (HVF, `xlat/jitprobe`, `xlat/x64run`):

| Question | Result |
|---|---|
| Page size | 16 KiB (`hw.pagesize`). x86_64 programs assume 4 KiB, so `mmap`/`mprotect` granularity is wrong by 4x (Rosetta gets 4 KiB user maps from Apple's kernel). |
| `mmap` RWX with `MAP_JIT`, then `mprotect` RX / RWX | works. `pthread_jit_write_protect_np` and `sys_icache_invalidate` link and run; generated code executes (returned 42). No entitlement or signing problem. |
| Write fault on a protected page | `SIGSEGV`/`SIGBUS` handler runs, can `mprotect` and resume. This is enough for self-modifying-code detection (write-protect translated pages), as QEMU does. |
| Address space | `MAP_FIXED` works at 8 GiB, 16 GiB, `0x7ffee0000000` (x86_64 stack), `0x7fff00000000` (dyld region) and 64 TiB. Fails at 0x1000 (below 4 GiB there is no memory for arm64 programs; a smaller `-pagezero_size` gets the process killed with SIGKILL) and at `0x7fffffe00000..` (top 2 MiB below 2^47), **so the x86_64 commpage address is not mappable**. |
| Translator image vs. guest at 4 GiB | arm64 executables are always PIE; dyld slides ours to `0x10xxxxxxx`, which is exactly where x86_64 images want to live (`0x100000000`). `-image_base`/`-no_pie` are ignored by ld for arm64. Unsolved, see section 3. |
| Running an x86_64 Mach-O directly | `zsh: bad CPU type in executable`. There is no kernel hook, so a launcher is needed. |
| CPU features | `hw.optional.arm.FEAT_FlagM/LRCPC` are not reported by this kernel (the same host CPU reports them under macOS); whether the guest CPU has them is **untested** (would need an ID-register read, which EL0 cannot do). |
| TSO | Apple's hardware TSO mode (what Rosetta uses) is switched by the kernel for the thread; no user-space or HVF-guest path is known. **Not available** without a kernel patch, and not tested. |

Other constraints known from the code base: `tux` already rewrites `syscall`/`svc` instructions and runs a signal-based
trap; PureDarwin has no SysV shm; one CPU core by default; the x86_64 guest image runs under TCG (slow), so x86_64-host work
can only be validated on real PCs or slowly in QEMU.

Notable simplification: **Darwin BSD system call numbers and LP64 structure layouts are identical on x86_64 and arm64**
(`x64run` passes `write`/`exit` straight to the host `syscall()`). Mach-O x86_64 -> arm64 needs no struct conversion, unlike
Linux (x86_64 and arm64 differ in call numbers, `stat`, `ioctl`, `sigaction`, `ucontext`), where `tux/nr.h` and `sys.c` would
have to be made guest-ABI-aware instead of host-arch-aware (today `#ifdef TUX_ARM64/TUX_X86_64` select the host's ABI).

## 2. Options

| Option | License | Effort *(est.)* | Speed *(est.)* | Fits ClokeOS? |
|---|---|---|---|---|
| **A. QEMU TCG as a library** (Unicorn 2.x; QEMU's `linux-user` and `bsd-user` do not build for Darwin hosts and have no Darwin guest ABI) | GPL-2.0 (check upstream before shipping; QEMU parts are GPL) | Low for a first run (loader + syscall layer around a CPU core: weeks); no thread story (one engine per thread, shared memory by `uc_mem_map_ptr`) | 5-20x slower than native; fine for tools, poor for GUIs | Both directions from one code base, all ISAs, good as **correctness oracle** and as the arm64->x86_64 direction. Download ~10 MB source. |
| **B. Box64/FEX-style port** (Box64: MIT, x86_64->arm64 dynarec; FEX-Emu: MIT, x86_64/x86->arm64, JIT + thunks) | MIT | High: both assume Linux (ELF loader, glibc wrapping, Linux signals/ucontext, `/proc`). Box64 has a Linux-on-Apple-Silicon (16K pages) mode, not a Darwin one. Reusing only their decoder/IR/arm64 emitter means a large C/C++ extraction | 50-80% of native | Best-known dynarec quality, but the OS layer would be rewritten anyway; one direction only (to arm64). Unverified whether either builds on Darwin. |
| **C. Own translator** (Rosetta-style: block JIT x86_64->arm64 with lazy flags, 1:1 guest memory, syscall pass-through for Darwin; AOT cache later) | own code | Very high: decoder + ~500 integer ops + SSE2 + x87 + flags + signals + threads: months to run busybox-class programs, long tail for Mac apps | 60-90% of native when mature | Full control, no license issue, can exploit Darwin simplicity (Mach-O needs no syscall conversion). Reverse direction (arm64->x86_64) is a second, smaller back end. |
| **D. Hook point** | - | - | - | See below. |

Hook points (independent of the translator choice):
1. **Launcher** (`xrun prog`): works today, no kernel patch; used by Fundus/Terminal via a wrapper like `tux-run`.
2. **`DYLD_INSERT_LIBRARIES` interposer on `execve`/`posix_spawn`** in the shell and desktop processes (children of a translated program are handled by the translator's own `execve`): makes foreign binaries "just run" from native programs. Needs per-process insertion (launchd/zsh startup), no kernel patch.
3. **Kernel `exec_mach_imgact` fallback** (try the translator on `EBADARCH`, plus `binfmt`-like ELF/PE detection): clean and the only fully transparent option; needs a rebuilt or binary-patched kernel (`kcfix.py` shows binary patching is possible but fragile).
4. **`tux`**: already the loader for Linux ELF; a foreign ELF would swap "jump into the program" for "run it in the translator" and keep `sys.c`.

Wine/PE: the cheapest Windows-on-arm64 route needs **no new Wine build**: run the x86_64 `wine` (a Mach-O, already in the x86_64 image) inside the Mach-O translator, so wine, its x86_64 PE side and the Windows program are all translated together. Costs: speed, and Wine's own use of `%gs`/signals/address-space reservation must work in the translator. The alternative (Wine arm64 + FEX's `libwow64fex.dll`, "Hangover" style) is faster but needs an arm64 Wine build for PureDarwin (Nix on Linux, ~60 GB) first.

Dynamic Mach-O (the real target, apps and Wine): needs dyld and libSystem **of the guest architecture** in the tree. The x86_64 system libraries exist in the x86_64 image; for an arm64 image they have to be shipped (e.g. under `/usr/lib/x86_64`), plus an emulated commpage (not mappable at the x86 address, see above: the translator must intercept those loads or patch libSystem).

## 3. Recommendation and order

Decision: **A first as the oracle and the quick start, C as the product** (B is dropped unless a Darwin build of its core turns out to be cheap).

Order:
1. (done here) Probe the platform: JIT, page size, address space. Result above: JIT and signals work; 16K pages, no commpage address and the PIE slide at 4 GiB are the real issues.
2. **Smallest useful next step**: `xlat/x64run` grown into a small x86_64 *interpreter* for static Mach-O (ClokeOS-built static programs, `-nostdlib` or a static libc), with the launcher `xrun`. It needs no JIT, tests the loader, syscall pass-through, stack/auxv and the 16K-page problem, and becomes the reference for the JIT (differential testing against it and against Unicorn).
3. Solve the three platform problems: guest memory model (1:1 with the translator loaded out of the way, e.g. a launcher that re-executes until the dyld slide misses `[4 GiB, 4 GiB + guest)`, or a software `base + addr` offset in the JIT), 4 KiB emulation on 16 KiB pages (split guard pages, track guest protections in a table, `mprotect` only at 16K), commpage emulation.
4. Block JIT for x86_64 -> arm64 (lazy flags with `FlagM` where present, plain `LDR/STR` without TSO; add `LDAR/STLR` or `DMB` only for multi-threaded programs or after measuring), then SSE2/x87, then threads and signals.
5. Dynamic x86_64 Mach-O (dyld + libSystem of the guest ISA), then x86_64 Wine inside it (Windows on arm64), then foreign ELF in `tux` (guest-ABI tables in `nr.h`/`sys.c`).
6. Reverse direction (arm64 -> x86_64): start with option A (Unicorn/TCG) behind the same loader/syscall layer; replace by a small arm64 decoder + x86_64 emitter only if speed demands it. Note arm64 programs of the Mac world will need arm64 Darling frameworks on x86_64 images.
7. Kernel exec hook as the last convenience step.

## 4. What the proof shows (and does not)

`xlat/build.sh` builds: `jitprobe` (platform probe), `x64run` (about 30-instruction x86_64 interpreter: thin Mach-O loader,
`LC_UNIXTHREAD`/`LC_MAIN`, register file, flags for add/sub/cmp/xor, push/pop/call/ret/jmp/jz/jnz, modrm/SIB/RIP-relative,
`syscall` -> host `syscall()` with Darwin class 2 numbers) and `hello-x64` (static x86_64 Mach-O, prints three lines, exit code 7).

Tested in the arm64 VM (ClokeOS 2.5 desktop image copy, QEMU + HVF): `./hello-x64` -> `bad CPU type in executable`;
`./x64run hello-x64` -> three lines, exit code 7 (same as the host Mac). `jitprobe` output as in section 1.

Not tested: x86_64 host side (the proof is arm64-only; the reverse direction is not started); any dynamic Mach-O, threads,
signals, SSE/x87, TSO, `FlagM`/`LRCPC` availability, the 4 KiB emulation, the commpage, performance of any of it, building
Unicorn or Box64/FEX on Darwin, the claimed licenses and speeds (from memory/public experience, not re-verified here).
