# Mac-Programme auf ClokeOS

Der Weg, damit Mac-Programme auf ClokeOS laufen: Darlings Mac-Frameworks werden für ClokeOS gebaut,
auf PureDarwins echtem objc4, dyld und libSystem. Hintergrund, Optionen und Messungen stehen in
`~/ClokeOS-work/upstream/appkit-machbarkeit.md`.

Stand 20.10.2026:
- **Foundation-Probe.** CoreFoundation und Foundation aus Darling, ein Prüfprogramm aus Apples SDK
  besteht in der VM 24 von 25 Prüfungen.
- **AppKit-Probe.** AppKit, Cocoa, CoreGraphics, Onyx2D, CoreText, QuartzCore, CoreImage, CoreData
  und ImageIO aus darling-cocotron. Eine x86_64-Cocoa-App aus Apples SDK (`hallo.m`) zeigt ihr
  Fenster in CloakServer, reagiert auf Maus und Tastatur und beendet sich sauber.

```bash
macapps/machcheck.py /pfad/zu/Programm.app
```
zeigt, welche Bibliotheken und Symbole einem Mac-Programm auf ClokeOS fehlen (ohne VM). Mit
`--frameworks ~/ClokeOS-work/out/macapps/root/Library/ClokeOS/MacFrameworks` prüft es wie mit
`DYLD_FRAMEWORK_PATH` im Gast gegen die hier gebauten Frameworks.

```bash
macapps/foundation.sh
macapps/appkit.sh
```
bauen die Frameworks nach `~/ClokeOS-work/out/macapps/root/Library/ClokeOS/MacFrameworks`, erst
Foundation, dann AppKit. Die Quellen braucht es vorher unter `~/ClokeOS-work/download/darling/`
(`git clone --depth 1` von darlinghq/darling-corefoundation, darlinghq/darling-foundation und
darlinghq/darling-cocotron).

```bash
clang -target x86_64-apple-macos11.0 -fobjc-arc -framework Foundation -o ~/ClokeOS-work/out/macapps/root/Library/ClokeOS/Probe/fprobe macapps/fprobe.m
```
```bash
clang -target x86_64-apple-macos11.0 -fobjc-arc -framework Cocoa -o ~/ClokeOS-work/out/macapps/root/Library/ClokeOS/Probe/hallo macapps/hallo.m
```
bauen das Prüfprogramm und die Test-App. `macapps/probe-image.sh` legt damit eine Testkopie des
Desktop-Images an. Danach startet `./run.sh --headless --image ~/ClokeOS-work/tmp/fprobe-desktop.img`
die VM, `./vmctl.py send 'zsh /Library/ClokeOS/Probe/probe.sh' 300` führt die Foundation-Probe aus.
Für die Test-App nach dem Start mit `./vmctl.py key ret` anmelden, dann
`./vmctl.py send 'zsh /Library/ClokeOS/Probe/hallo.sh --klick 20 > /tmp/hallo.log 2>&1 &'` und
`./vmctl.py shot hallo.png`.

Die Frameworks ersetzen nichts im System. Programme benutzen sie über
`DYLD_FRAMEWORK_PATH=/Library/ClokeOS/MacFrameworks`, weil launchd, configd und Security weiter
PureDarwins CoreFoundation laden und zwei CFs in einem Prozess nicht gehen. PureDarwins eigenes
Onyx2D und CoreGraphics bleiben ebenfalls.

Dateien:
- `include/`: Ersatz für private Apple-Header.
- `syscompat.c`: Ersatz für Funktionen, die PureDarwins libSystem fehlen.
- `cfcompat.m`, `fndcompat.m`: Literal-Klassen, die Apples clang voraussetzt, und kleine Lücken in
  Darling.
- `appkit/x11present.m`: AppKit-Fenster ohne OpenGL auf den Bildschirm (XShmPutImage, sonst
  XPutImage). `CLOKE_X11_DEBUG=1` zeigt Weg und Dauer, `CLOKE_NO_XSHM=1` erzwingt XPutImage.
- `appkit/imageio.m`: ImageIO (CGImageSource, CGImageDestination) über Onyx2D.
- `appkit/xkbrules.c`: die eine Funktion aus libxkbfile, die PureDarwin fehlt.
- `appkit/sysstubs.c`: Platzhalter für OpenGL, IOKit und LaunchServices, nicht exportiert.
- `appkit/include/`: Ersatz für Darlings private Header und schlankere Apple-Header.
- `patches/`: Korrekturen an Darling-Fehlern und Anpassungen an ClokeOS (sed).
- `fprobe.m`, `hallo.m`: Prüfprogramm (Foundation) und Test-App (Cocoa).
