# ClokeOS

Ein macOS-ähnliches Betriebssystem auf Basis des Darwin-Kernels (XNU), mit einem Umhang als Logo.

## Was es schon gibt

`clokeos.html` ist der Prototyp der Benutzeroberfläche. Er läuft in jedem Browser:

- Startbildschirm mit Umhang-Logo (Taste **V** zeigt den ausführlichen XNU-Bootlog), Anmeldefenster
- Menüleiste mit ClokeOS-Menü, App-Menüs, WLAN, Batterie, Kontrollzentrum und Uhr
- Dock mit Vergrößerung, Laufindikatoren und Papierkorb; Fenster lassen sich ziehen, vergrößern, minimieren und zoomen
- Liquid-Glass-Design im Stil von macOS Golden Gate: durchscheinende Fenster, Dock mit Lichtbrechung (in Chromium-Browsern), Glasmenüs, Regler „Liquid Glass“ von klar bis getönt
- Hintergrundbild „Goldenes Tor“: eine selbst gezeichnete Hängebrücke im Nebel, hell am Tag und dunkel am Abend
- Apps: Fundus (Dateimanager; im Theater ist der Fundus der Raum, in dem Kostüme und Umhänge hängen), Search (Browser mit Tabs, Verlauf und Suche über ClokeOS-Seiten und eigene Dateien; fremde Webseiten öffnen in einem neuen Tab, weil ClokeOS selbst im Browser läuft), Terminal (zsh-ähnlich: `ls`, `cd`, `cat`, `uname -a`, `sw_vers`, `sysctl`, `launchctl list`, `clokefetch` …), Notizen, Rechner, Kalender, Aktivitätsanzeige, Systemeinstellungen
- Suche (Strg + Leertaste), Programme-Übersicht (F4), Hell-/Dunkelmodus, Akzentfarben, fünf Hintergrundbilder
- Ein virtuelles Dateisystem mit Darwin-Struktur (`/System`, `/Users`, `/Applications`, `/bin` …), das im Browser gespeichert bleibt

Lokal starten:

```bash
python -m http.server 8765
```

Danach `http://localhost:8765/clokeos.html` öffnen.

## Echtes ClokeOS auf XNU

Im Ordner `clokeos-darwin/` entsteht das echte System. Es bootet in einer VM einen echten Darwin-Kernel (XNU, Darwin 20.5.0) bis zur zsh und meldet sich dort als ClokeOS:

```text
root@clokeos / # sw_vers
ProductName:		ClokeOS
ProductVersion:		2.0
BuildVersion:		2A17
root@clokeos / # uname -a
Darwin clokeos 20.5.0 Darwin Kernel Version 20.5.0: Sun Aug 23 10:54:37 UTC 2026; nixbld:xnu-build/RELEASE_X86_64 x86_64
```

![ClokeOS startet in QEMU: das Umhang-Logo, weiß auf Schwarz](clokeos-darwin/bootlogo.png)

Grundlage ist das fertige Image von [PureDarwin 20.5](https://github.com/PureDarwin/PureDarwin/releases/tag/20.5) (UEFI, eigener Loader, XNU, launchd, zsh, ext4 als Wurzeldateisystem). ClokeOS ändert daran:

- `SystemVersion.plist`: ClokeOS 2.0 „Presidio“, Build 2A17 (launchd setzt daraus auch `kern.osversion`)
- `sw_vers` liest die Plist wie bei macOS; PureDarwins eigenes `sw_vers` hat „PureDarwin 11.3“ fest einkompiliert
- Rechnername `clokeos` über den launchd-Dienst `com.clokeos.hostname`
- Anmeldetext mit Umhang (`/etc/motd`), Eingabeaufforderung `root@clokeos / #`
- `clokefetch` wie im Terminal der Oberfläche, Volume-Name „Cloke HD“
- Bootlogo: der Umhang aus der Oberfläche statt des Kernel-Logs (eigener Build von PureDarwins Loader, siehe unten); V beim Start gedrückt halten zeigt das Log

Einmal einrichten (Homebrew):

```bash
brew install qemu e2fsprogs mtools mingw-w64
```

Image bauen und starten:

```bash
cd clokeos-darwin && ./brand.sh && ./run.sh
```

`brand.sh` lädt das PureDarwin-Image (275 MB) und die Quellen des Bootloaders (zusammen 300 KB, alle Prüfsummen werden geprüft), baut den Loader mit Bootlogo, schreibt die Dateien aus `overlay/` mit `debugfs` ins ext4-Dateisystem (ohne Linux und ohne Root-Rechte), legt Loader und Boot-Argumente mit `mtools` in die EFI-Partition und erzeugt `~/ClokeOS-work/out/clokeos-2.0-x86_64.img`. `run.sh` öffnet ein QEMU-Fenster mit dem Bildschirm und die serielle Konsole im Terminal. Im Fenster steht ab etwa der vierten Sekunde das Logo; Kernel-Log, Anmeldetext und zsh laufen im Terminal. Auf einem Mac mit Apple-Chip wird x86_64 emuliert; bis zur Eingabeaufforderung dauert es etwa 75 Sekunden. Beenden mit `poweroff` in der VM.

Für Tests ohne Fenster: `./run.sh --headless`, dann `./vmctl.py wait 'Willkommen'`, `./vmctl.py send 'clokefetch'`, `./vmctl.py shot bild.png`, `./vmctl.py quit`. Downloads, Images und VM-Zustand liegen in `~/ClokeOS-work`, nicht im Projektordner. Versionsnummern und Namen stehen in `clokeos-darwin/config.sh`.

### Bootlogo

Den Kernel startet PureDarwins Bootloader [xnu-loader](https://github.com/PureDarwin/xnu-loader) (`EFI/BOOT/BOOTX64.EFI`). ClokeOS baut ihn selbst, mit einer Änderung: Steht in den Boot-Argumenten kein `-v`, zeichnet er vor dem Kernelstart den Umhang (Pfad `LOGO_D` aus `clokeos.html`) über GOP zentriert auf schwarzen Grund, 12 % der Bildschirmhöhe groß (96 Pixel bei QEMUs 1280 × 800). Sein eigenes Protokoll schreibt er dann nur noch auf die serielle Schnittstelle, Fehlermeldungen weiterhin auch auf den Bildschirm.

Das Logo bleibt stehen, bis die VM ausgeschaltet wird. Denn PureDarwins Kernel ist für die Fehlersuche gebaut: Bekommt er einen Framebuffer, malt er farbige Fortschrittsbalken in die oberen Bildschirmzeilen und, solange die Textkonsole aus ist, jedes Zeichen der seriellen Ausgabe auf den Bildschirm. `gopconsole=1` schaltet außerdem nach wenigen Sekunden die Textkonsole mit dem ganzen Log ein. Keins davon lässt sich mit Boot-Argumenten abschalten. Ohne `-v` gibt der Loader dem Kernel deshalb gar keinen Framebuffer, wie auf einem Rechner ohne Bildschirm (der Kernel meldet `No video - forcing serial mode`). Nebenwirkungen: Es gibt dann kein `/dev/fb0`, und die Shell ist nur über die serielle Konsole erreichbar. Für den Fensterserver (Schritt 3) muss der Kernel den Framebuffer wieder bekommen.

Die Boot-Argumente stehen als `CLOKE_BOOT_ARGS` in `clokeos-darwin/config.sh`: `debug=0x218 -nogzalloc_mode keepsyms=1 serial=3 gopconsole=1 gen9_debug=1`, also PureDarwins Vorgabe ohne `-v`. `serial=3` muss bleiben, denn darüber laufen Konsole, Terminal und `vmctl.py`. Mit `-v` vorn und einem neuen `./brand.sh` sieht der Start wieder aus wie bei PureDarwin, mit Kernel-Log und zsh auf dem Bildschirm ([Bildschirmfoto](clokeos-darwin/vm-bildschirm.png)).

Für einen einzelnen ausführlichen Start reicht die Taste V, wie es der Startbildschirm in `clokeos.html` anbietet: V beim Einschalten gedrückt halten, im QEMU-Fenster oder auf der seriellen Konsole. Der Loader sieht zweimal nach, ohne zu warten: gleich nach dem Lesen der Boot-Argumente und noch einmal, wenn der Kernel geladen ist. Findet er ein v oder V, hängt er ` -v` an die Boot-Argumente, löscht den Bildschirm samt Logo, schreibt sein Protokoll wieder auch auf den Bildschirm und gibt dem Kernel den Framebuffer. Der Start sieht dann aus wie mit `-v` in `config.sh`, und im seriellen Protokoll steht `key V: verbose boot, using "… -v"`. Kurz tippen genügt nicht: Die Firmware (OVMF) leert den Tastaturpuffer direkt bevor sie den Loader startet, und der Loader ist in QEMU nach etwa 0,25 Sekunden fertig. Erst die Tastenwiederholung der gehaltenen Taste kommt sicher in dieses Fenster. Ohne Fenster lässt sich das nachstellen, indem man ab dem Start von `./run.sh --headless` alle 50 ms ein `v` an den seriellen Socket (`~/ClokeOS-work/vm/serial.sock`) schickt oder per QMP `send-key` drückt. Ein einziges „Taste gedrückt“ per QMP-`input-send-event` vor dem Start reicht dagegen nicht, weil QEMU den Tastaturzustand zurücksetzt, wenn die Firmware die USB-Tastatur einrichtet.

Gebaut wird der Loader von `clokeos-darwin/loader/build.sh`, das `brand.sh` aufruft. Es entpackt xnu-loader im Stand `8295cd89` (derselbe wie in PureDarwin 20.5) und gnu-efi 4.0.2, wendet `loader/xnu-loader.patch` an, fügt `splash.c` hinzu und erzeugt mit `logo.py` aus `LOGO_D` die Umrisse des Logos als C-Header. Der Loader rastert das Logo erst beim Start in der vorhandenen Auflösung, deshalb ist es auf jedem Bildschirm scharf, und eine Änderung am Logo in `clokeos.html` landet beim nächsten `./brand.sh` von selbst im Bootlogo. Kompiliert wird mit mingw-w64, das direkt EFI-Programme erzeugt; eine Linux-VM ist nicht nötig. Der Build ist reproduzierbar, das Ergebnis liegt in `~/ClokeOS-work/out/BOOTX64.EFI`.

Grenzen: Die VM sieht nur einen Prozessorkern (`hw.ncpu` = 1). Das Netzwerk kommt nicht hoch (configd meldet „Network configuration not updated“). Ohne `-v` (oder V beim Start) zeigt der Bildschirm nach dem Start weiter nur das Logo (siehe oben). Die Kernelzeile von `uname` bleibt PureDarwins Build, denn den Kernel übernimmt ClokeOS unverändert.

### Was Apple nicht veröffentlicht

Der Kernel ist das kleinere Problem, denn XNU ist Open Source (Apple Public Source License 2.0, Quelle: `github.com/apple-oss-distributions/xnu`). Schwierig ist alles, was Apple **nicht** veröffentlicht:

| Schicht | macOS | Open Source? | ClokeOS-Ansatz |
|---|---|---|---|
| Kernel | XNU | ja | unverändert von PureDarwin übernehmen, nur Branding |
| Basis-Userland | Libc, dyld, BSD-Tools | größtenteils | von PureDarwin (dazu toybox und zsh) |
| Init | launchd | nur ältere Versionen | PureDarwins launchd/libxpc |
| Grafik-Stack | Quartz, WindowServer | nein | eigener Fensterserver („CloakServer“) |
| Oberfläche | AppKit, Finder, Dock | nein | diese Shell als native App neu umsetzen |
| Treiber | Grafik, WLAN, Audio | meist nein | zuerst nur in einer VM mit Standardhardware |

### Bauplan

1. ~~**Darwin in einer VM booten.**~~ Erledigt mit PureDarwin 20.5 in QEMU (siehe oben).
2. ~~**Branding.**~~ Erledigt: `SystemVersion.plist`, `sw_vers`, Hostname, Anmeldetext, Volume-Name und das Bootlogo, das PureDarwins xnu-loader jetzt vor dem Kernelstart über GOP zeichnet (siehe Bootlogo).
3. **Fensterserver.** PureDarwin bringt auf x86_64 bereits X11 und Wayland mit (eigener GOP-Grafiktreiber für Xorg, Maus und Tastatur über USB-HID). CloakServer kann deshalb zunächst ein Wayland-Compositor oder X11-Fenstermanager im ClokeOS-Stil sein, statt direkt auf IOFramebuffer aufzusetzen.
4. **Shell portieren.** Menüleiste, Dock, Fundus, Search und die übrigen Apps aus `clokeos.html` als native Programme umsetzen, oder eine Browser-Engine als Shell-Laufzeit einbetten. PureDarwin hat WebKitGTK und NetSurf als Pakete; mit WebKitGTK könnte `clokeos.html` fast unverändert als Oberfläche laufen.
5. **Installer-Image.** Bootfähiges ISO mit Bootloader, Kernel, Userland und CloakServer.

Für Schritt 3 und 4 reicht das fertige Minimal-Image nicht mehr. Dann muss ClokeOS PureDarwins Desktop-Image selbst bauen. Das geht nur unter Linux mit Nix (etwa 60 GB) und braucht Apples `MacOSX11.3.sdk`, das man selbst besorgen muss, weil es nicht weitergegeben werden darf.

Wichtig: Apples Markennamen, Icons und Schriften dürfen nicht übernommen werden. ClokeOS nutzt deshalb eigene Namen, eigene Icons und ein eigenes Logo.
