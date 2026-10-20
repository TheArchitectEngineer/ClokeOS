# Mac-Programme auf ClokeOS

Der Weg, damit Mac-Programme auf ClokeOS laufen: Darlings Mac-Frameworks werden für ClokeOS gebaut,
auf PureDarwins echtem objc4, dyld und libSystem. Hintergrund, Optionen und Messungen stehen in
`~/ClokeOS-work/upstream/appkit-machbarkeit.md`.

Stand 20.10.2026: **Foundation-Probe.** CoreFoundation und Foundation aus Darling, ein Prüfprogramm
aus Apples SDK besteht in der VM 24 von 25 Prüfungen. AppKit fehlt noch.

```bash
macapps/machcheck.py /pfad/zu/Programm.app
```
zeigt, welche Bibliotheken und Symbole einem Mac-Programm auf ClokeOS fehlen (ohne VM).

```bash
macapps/foundation.sh
```
baut CoreFoundation und Foundation nach `~/ClokeOS-work/out/macapps/root/Library/ClokeOS/MacFrameworks`.
Die Quellen braucht es vorher unter `~/ClokeOS-work/download/darling/` (`git clone --depth 1` von
darlinghq/darling-corefoundation und darlinghq/darling-foundation).

```bash
clang -target x86_64-apple-macos11.0 -fobjc-arc -framework Foundation -o ~/ClokeOS-work/out/macapps/root/Library/ClokeOS/Probe/fprobe macapps/fprobe.m
```
baut das Prüfprogramm. `macapps/probe-image.sh` legt damit eine Testkopie des Desktop-Images an.
Danach startet `./run.sh --headless --image ~/ClokeOS-work/tmp/fprobe-desktop.img` die VM, und
`./vmctl.py send 'zsh /Library/ClokeOS/Probe/probe.sh' 300` führt die Probe aus.

Die Frameworks ersetzen nichts im System. Programme benutzen sie über
`DYLD_FRAMEWORK_PATH=/Library/ClokeOS/MacFrameworks`, weil launchd, configd und Security weiter
PureDarwins CoreFoundation laden und zwei CFs in einem Prozess nicht gehen.

Dateien:
- `include/`: Ersatz für private Apple-Header.
- `syscompat.c`: Ersatz für Funktionen, die PureDarwins libSystem fehlen.
- `cfcompat.m`, `fndcompat.m`: Literal-Klassen, die Apples clang voraussetzt, und kleine Lücken in
  Darling.
- `patches/`: Korrekturen an Darling-Fehlern.
