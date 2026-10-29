# Optional kernel: Darwin 20.6.0 (xnu-7195.141) on arm64

The stock image uses PureDarwin's Darwin 20.5 kernel. `CLOKEOS_KERNEL=/path/to/kernel-collection`
(arm64 only) replaces `EFI/BOOT/kernel` with a Darwin 20.6.0 kernel collection; stock stays the default.

    CLOKEOS_ARCH=arm64 CLOKEOS_KERNEL=~/kc-darwin-20.6.0-arm64-virt ./build.sh

Tested in the arm64 desktop VM (HVF): boots to the desktop, `uname -a` = Darwin 20.6.0, one CPU.

## Building the kernel collection

1. Clone PureDarwin (rev 1bbaeb2), apply `tools/xnu-7195.141-puredarwin.patch` (xnu 7195.121 -> 7195.141 merge
   plus the flags below), build on a Mac with its CMake (`ninja xnu`, kexts as in the notes of the PD repo).
2. Three things are needed when building with a Mac host toolchain (Apple clang 21):
   - `-fno-stack-protector` in `cmake/MakeInc.def.in` (clang enables it by default; the kernel panics in IOLibInit).
   - `-Wl,-headerpad,0x3000` instead of 152: the fileset header of the collection (about 13 KB with all kexts)
     otherwise overwrites the start of `__TEXT,__const` (jump tables) and the kernel jumps into the middle of a function.
   - Link kexts as `-bundle` with PureDarwin's own ld64 (`cmake/kext.cmake`: env `PD_KEXT_LD`, `PD_KEXT_STUB` = empty
     dylib with install_name /usr/lib/libSystem.B.dylib), not `-kext` with Apple's ld: only bundles carry rebase info,
     without it kc-builder leaves `kmod_info.start` unrelocated and corecrypto never registers the PRNG.
3. `kc-builder -kernel xnu_sym/kernel.release.virt -codeless <System.kext PlugIns> -kext <each kext>` (PureDarwin/kc-tools 802fbc7).
4. Replace the build user name in the result by a same-length string before publishing.
