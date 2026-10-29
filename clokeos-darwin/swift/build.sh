#!/bin/bash
# Baut libswiftCore.dylib aus den Swift-Quellen (swift.org, Apache-2.0 + Runtime Library Exception) für Darwin
# und die Beispielprogramme hello/hello2/objc (docs/swift-runtime.md) dagegen.
#
#   swift/build.sh [Arbeitsordner]     ergibt <Arbeitsordner>/run-{arm64,x86_64}/{hello,hello2,objc,libswiftCore.dylib}
#
# Braucht Xcode oder die Command Line Tools (Swift 6.4), cmake und ninja. Quellen: nur Teilklon von swiftlang/swift
# (Tag swift-6.4.0-RELEASE, ohne Tests/Docs ~110 MB inkl. .git ~16 MB), kein LLVM: Runtimes/Core bringt LLVMSupport mit.
# Rechenzeit auf einem M-Mac: je Architektur etwa 15 Minuten (der Swift-Teil der Standardbibliothek ist ein Modul).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
w="${1:-${CLOKEOS_WORK:-$HOME/ClokeOS-work}/swiftsrc}"
tag="${SWIFT_TAG:-swift-6.4.0-RELEASE}"
mkdir -p "$w"; cd "$w"
if [ ! -d swift ]; then
  git clone -q --filter=blob:none --depth 1 --branch "$tag" --sparse https://github.com/swiftlang/swift.git swift
  git -C swift sparse-checkout set --no-cone '/*' '!/test' '!/validation-test' '!/benchmark' '!/docs' '!/SwiftCompilerSources' '!/tools' '!/unittests'
  # arm64-libobjc von PureDarwin hat die iOS-Isa-Maske (siehe Patch); danach die Quellen in die neue Runtimes/-Struktur kopieren
  git -C swift apply "$here/patches/isa-mask-old-objc.patch"
  cmake -P swift/Runtimes/Resync.cmake >/dev/null
fi
sdk="$(xcrun --show-sdk-path)"
dep="${SWIFT_DEPLOY:-15.0}"   # Logic Pro verlangt 15.6; die Rückportierungs-Bibliotheken (libswiftCompatibility*.a) der Toolchain haben keine x86_64-Scheibe mehr, ab 15 braucht man sie nicht
for a in arm64 x86_64; do
  extra=()
  [ $a = arm64 ] && extra=(-DCMAKE_CXX_FLAGS=-DCLOKEOS_OLD_OBJC_ISA=1)
  # CMP0195 NEW: verschachtelte .swiftmodule-Ordner, sonst kollidiert der Modulpfad mit dem .swiftinterface.
  # CMAKE_Swift_COMPILER_TARGET: sonst übersetzt swiftc den Swift-Teil immer für den Host (arm64, macOS 27).
  cmake -G Ninja -S swift/Runtimes/Core -B "b-$a" -DCMAKE_POLICY_DEFAULT_CMP0195=NEW -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_SYSROOT="$sdk" -DCMAKE_OSX_ARCHITECTURES=$a -DCMAKE_OSX_DEPLOYMENT_TARGET=$dep \
    -DCMAKE_Swift_COMPILER_TARGET=$a-apple-macos$dep \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_Swift_COMPILER="$(xcrun -f swiftc)" \
    -DBUILD_SHARED_LIBS=ON -DSwiftCore_ENABLE_BACKTRACING=OFF "${extra[@]}" >/dev/null
  ninja -C "b-$a" swiftCore >/dev/null
  r="run-$a"; mkdir -p "$r"; cp "b-$a/Core/libswiftCore.dylib" "$r/"
  clang -arch $a -mmacosx-version-min=$dep -c "$here/overlay-stubs.c" -o "$r/stubs.o"
  for t in hello hello2 objc; do
    # Ohne Autolink: das SDK würde sonst /usr/lib/swift/libswiftCore.dylib (Dyld-Cache des Macs) eintragen.
    swiftc -O -c -target $a-apple-macos$dep -Xfrontend -disable-implicit-concurrency-module-import \
      -Xfrontend -disable-implicit-string-processing-module-import "$here/$t.swift" -o "$r/$t.o"
    clang -arch $a -mmacosx-version-min=$dep "$r/$t.o" "$r/stubs.o" "$r/libswiftCore.dylib" -Wl,-ignore_auto_link \
      -lc++ -lobjc -Wl,-rpath,@executable_path -o "$r/$t"
    rm "$r/$t.o"
  done
  rm "$r/stubs.o"
done
file run-*/hello*
