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

Im Ordner `clokeos-darwin/` entsteht das echte System. Es bootet in einer VM einen echten Darwin-Kernel (XNU, Darwin 20.5.0) bis zur zsh und meldet sich dort als ClokeOS. In der Desktop-Variante startet danach CloakServer, der Fensterserver von ClokeOS, mit Menüleiste, Dock und Fenstern im Stil der Oberfläche (siehe [Desktop: CloakServer](#desktop-cloakserver)).

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
- Desktop-Variante: CloakServer auf Xorg, gestartet von launchd

Einmal einrichten (Homebrew):

```bash
brew install qemu e2fsprogs mtools mingw-w64
```

Image bauen und starten:

```bash
cd clokeos-darwin && ./brand.sh && ./run.sh
```

`brand.sh` lädt das PureDarwin-Image (275 MB) und die Quellen des Bootloaders (zusammen 300 KB, alle Prüfsummen werden geprüft), baut den Loader mit Bootlogo, schreibt die Dateien aus `overlay/` mit `debugfs` ins ext4-Dateisystem (ohne Linux und ohne Root-Rechte), legt Loader und Boot-Argumente mit `mtools` in die EFI-Partition und erzeugt `~/ClokeOS-work/out/clokeos-2.0-x86_64.img`. `run.sh` öffnet ein QEMU-Fenster mit dem Bildschirm und die serielle Konsole im Terminal. Im Fenster steht ab etwa der vierten Sekunde das Logo, später zeichnet der Kernel einen Fortschrittsbalken darunter; Kernel-Log, Anmeldetext und zsh laufen im Terminal. Auf einem Mac mit Apple-Chip wird x86_64 emuliert; bis zur Eingabeaufforderung dauert es etwa 75 Sekunden. Beenden mit `poweroff` in der VM.

Für Tests ohne Fenster: `./run.sh --headless`, dann `./vmctl.py wait 'Willkommen'`, `./vmctl.py send 'clokefetch'`, `./vmctl.py shot bild.png`, `./vmctl.py quit`. Downloads, Images und VM-Zustand liegen in `~/ClokeOS-work`, nicht im Projektordner. Versionsnummern und Namen stehen in `clokeos-darwin/config.sh`.

### Bootlogo

Den Kernel startet PureDarwins Bootloader [xnu-loader](https://github.com/PureDarwin/xnu-loader) (`EFI/BOOT/BOOTX64.EFI`). ClokeOS baut ihn selbst, mit einer Änderung: Steht in den Boot-Argumenten kein `-v`, zeichnet er vor dem Kernelstart den Umhang (Pfad `LOGO_D` aus `clokeos.html`) über GOP zentriert auf schwarzen Grund, 12 % der Bildschirmhöhe groß (96 Pixel bei QEMUs 1280 × 800). Sein eigenes Protokoll schreibt er dann nur noch auf die serielle Schnittstelle, Fehlermeldungen weiterhin auch auf den Bildschirm.

Danach gehört der Bildschirm dem Kernel, und PureDarwins Kernel ist für die Fehlersuche gebaut: Bekommt er einen Framebuffer, malt er farbige Fortschrittsbalken in die oberen Bildschirmzeilen und, solange die Textkonsole aus ist, jedes Zeichen der seriellen Ausgabe auf den Bildschirm, auch später noch über einen Desktop. Mit Boot-Argumenten lässt sich das nicht abschalten. Alle diese Malroutinen zeichnen aber nur bei 32 Bit Farbtiefe, und XNUs Videokonsole kennt 24 Bit gar nicht. PureDarwins Grafiktreiber `IOGOPFramebuffer` behandelt 24 Bit dagegen genau wie 32 (32-Bit-Pixel, dieselben Farbmasken). Ohne `-v` übergibt der Loader dem Kernel deshalb denselben Framebuffer im Grafikmodus, nur mit der Angabe 24 Bit. Der Kernel lässt das Logo dann stehen, `/dev/fb0` gibt es, und Xorg kann zeichnen. Sobald `IOGOPFramebuffer` läuft, zeichnet XNU seinen Fortschrittsbalken unter das Logo, so wie der Startbildschirm in `clokeos.html` einen hat. Im Minimal-Image bleiben Logo und der halb gefüllte Balken stehen, bis die VM ausgeschaltet wird; die Shell läuft auf der seriellen Konsole. In der Desktop-Variante übernimmt Xorg den Bildschirm.

(Bis zur Desktop-Variante gab der Loader dem Kernel ohne `-v` gar keinen Framebuffer, wie auf einem Rechner ohne Bildschirm. Dann gab es aber auch kein `/dev/fb0` und keinen Grafiktreiber.)

Die Boot-Argumente stehen als `CLOKE_BOOT_ARGS` in `clokeos-darwin/config.sh`: `debug=0x218 -nogzalloc_mode keepsyms=1 serial=3 gen9_debug=1`, also PureDarwins Vorgabe ohne `-v` und ohne `gopconsole=1`. `gopconsole=1` schaltet die Textkonsole mit dem ganzen Log ein, sobald `IOGOPFramebuffer` startet. `serial=3` muss bleiben, denn darüber laufen Konsole, Terminal und `vmctl.py`. Mit `-v gopconsole=1` vorn und einem neuen `./brand.sh` sieht der Start wieder aus wie bei PureDarwin, mit Kernel-Log und zsh auf dem Bildschirm ([Bildschirmfoto](clokeos-darwin/vm-bildschirm.png)).

Für einen einzelnen ausführlichen Start reicht die Taste V, wie es der Startbildschirm in `clokeos.html` anbietet: V beim Einschalten gedrückt halten, im QEMU-Fenster oder auf der seriellen Konsole. Der Loader sieht zweimal nach, ohne zu warten: gleich nach dem Lesen der Boot-Argumente und noch einmal, wenn der Kernel geladen ist. Findet er ein v oder V, hängt er ` -v gopconsole=1` an die Boot-Argumente, löscht den Bildschirm samt Logo, schreibt sein Protokoll wieder auch auf den Bildschirm und gibt dem Kernel den Framebuffer als Textkonsole. Der Start sieht dann aus wie mit `-v gopconsole=1` in `config.sh`, und im seriellen Protokoll steht `key V: verbose boot, using "… -v gopconsole=1"`. In der Desktop-Variante startet danach trotzdem Xorg; die Textkonsole des Kernels schreibt dann aber auch über den Desktop. Kurz tippen genügt nicht: Die Firmware (OVMF) leert den Tastaturpuffer direkt bevor sie den Loader startet, und der Loader ist in QEMU nach etwa 0,25 Sekunden fertig. Erst die Tastenwiederholung der gehaltenen Taste kommt sicher in dieses Fenster. Ohne Fenster lässt sich das nachstellen, indem man ab dem Start von `./run.sh --headless` alle 50 ms ein `v` an den seriellen Socket (`~/ClokeOS-work/vm/x86_64/serial.sock`) schickt oder per QMP `send-key` drückt. Ein einziges „Taste gedrückt“ per QMP-`input-send-event` vor dem Start reicht dagegen nicht, weil QEMU den Tastaturzustand zurücksetzt, wenn die Firmware die USB-Tastatur einrichtet.

Gebaut wird der Loader von `clokeos-darwin/loader/build.sh`, das `brand.sh` aufruft. Es entpackt xnu-loader im Stand `8295cd89` (derselbe wie in PureDarwin 20.5) und gnu-efi 4.0.2, wendet `loader/xnu-loader.patch` an, fügt `splash.c` hinzu und erzeugt mit `logo.py` aus `LOGO_D` die Umrisse des Logos als C-Header. Der Loader rastert das Logo erst beim Start in der vorhandenen Auflösung, deshalb ist es auf jedem Bildschirm scharf, und eine Änderung am Logo in `clokeos.html` landet beim nächsten `./brand.sh` von selbst im Bootlogo. Kompiliert wird mit mingw-w64, das direkt EFI-Programme erzeugt; eine Linux-VM ist nicht nötig. Der Build ist reproduzierbar, das Ergebnis liegt in `~/ClokeOS-work/out/BOOTX64.EFI`.

Netzwerk: `run.sh` gibt der x86-VM eine Intel-Karte vom Typ 82545EM (`-nic user,model=e1000-82545em`), denn PureDarwins Treiber `PDE1000` erkennt nur diese; mit QEMUs Standardkarten bekommt die VM keine Adresse. Damit holt sie sich per DHCP `10.0.2.15`, löst Namen auf und erreicht das Internet (`ping github.com`). Als Maus hängt eine USB-Maus (`usb-mouse`) an der VM, denn PureDarwins HID-Treiber kennt nur Mäuse mit Boot-Protokoll, kein `usb-tablet`. Im QEMU-Fenster fängt ein Klick die Maus ein, Strg+Alt+G gibt sie wieder frei.

Grenzen: Die VM sieht nur einen Prozessorkern (`hw.ncpu` = 1). Im Minimal-Image zeigt der Bildschirm ohne `-v` (oder V beim Start) nach dem Start weiter Logo und Fortschrittsbalken (siehe oben). Kernel und Treiber übernimmt ClokeOS von PureDarwin; geändert sind nur 15 Byte in `IOGOPFramebuffer` (siehe unten). Die Kernelzeile von `uname` bleibt PureDarwins Build. Weil PureDarwin keine Zeitzonen-Datenbank hat, setzt ClokeOS die Zeitzone als POSIX-Regel (`CLOKE_TZ` in `config.sh`, Mitteleuropa mit Sommerzeit); Shell und Menüleiste zeigen damit deutsche Zeit.

#### Falsche Farben in PureDarwins Grafiktreiber

Bei der PCI-Konfiguration verlegt PureDarwins `IOPCIFamily` die Konsole und schreibt ihre Adresse mit gesetztem Bit 0 zurück (`0x80000001` statt `0x80000000`; bei XNU heißt das „physische Adresse“). `IOGOPFramebuffer::start()` übernimmt diese Adresse ungefiltert als Basis des Framebuffers. Damit liegen `/dev/fb0` und die Abbildung, in die Xorg zeichnet, ein Byte neben dem Bildschirm: Blau erscheint grün, Weiß gelb, Rot schwarz. Das passiert auch mit PureDarwins eigenem Loader und `-v`. `clokeos-darwin/kcfix.py` korrigiert das im Kernel Collection (`EFI/BOOT/kernel`), das `brand.sh` für x86_64 aufruft: Der Treiber ist ohne Optimierung übersetzt, deshalb lassen sich 15 Byte direkt nach `getConsoleInfo()` durch gleich lange Befehle ersetzen, die die Basisadresse auf 4 Byte abrunden. Das Skript sucht den Code am umgebenden Muster und ändert nur bei genau einem Treffer. PureDarwin hat den Fehler im Entwicklungszweig `upstream-xnu-12377` schon behoben (Commit `867967e`, `fbBase = bootDisplay.v_baseAddr & ~3`); in `main` und in den Images vom 23.08.2026 fehlt die Korrektur noch. Kommt ein neueres Image mit dieser Korrektur, findet `kcfix.py` den alten Code nicht mehr: Es gibt dann eine Warnung aus, ändert nichts, und `brand.sh` baut weiter. Zeigt Xorg danach trotzdem falsche Farben, ist es ein anderer Kernel mit dem alten Fehler, und das Muster in `kcfix.py` muss angepasst werden.

### Desktop: CloakServer

![ClokeOS-Desktop in QEMU am Tag: Das ClokeOS-Menü liegt als Glas über Fundus (Thunar), das Dock über dem Terminal mit clokefetch; die Fenster haben runde Ecken und weiche Schatten](clokeos-darwin/desktop.png)

Die Desktop-Variante baut auf PureDarwins Vorschau-Image `puredarwin.img` auf (6,1 GiB, von puredarwin.org; Xorg mit PureDarwins eigenem Treiber `puredarwingop`, Wayland/sway, i3, Xfce, Wine, Compiler). Grafisch startet dort von selbst nichts: PureDarwin öffnet zsh auf der Konsole, Xorg, sway oder Xfce startet man von Hand. ClokeOS startet beim Hochfahren über den launchd-Dienst `com.clokeos.cloakserver` Xorg mit CloakServer (`/usr/libexec/cloakserver-start` und `cloakserver-session`, mit einem D-Bus für die Sitzung):

```bash
cd clokeos-darwin && CLOKEOS_FLAVOR=desktop ./brand.sh && CLOKEOS_FLAVOR=desktop ./run.sh
```

`brand.sh` lädt dafür einmal das Desktop-Image (Prüfsumme wird geprüft) und erzeugt `~/ClokeOS-work/out/clokeos-2.0-x86_64-desktop.img`. Bei Emulation auf einem Mac mit Apple-Chip steht etwa 100 Sekunden lang das Bootlogo mit Fortschrittsbalken, dann startet Xorg, CloakServer zeigt kurz wieder den Umhang an derselben Stelle, und nach gut zwei Minuten ist der Desktop da. Die serielle Konsole mit zsh bleibt daneben erhalten, `vmctl.py` funktioniert wie beim Minimal-Image.

CloakServer (`clokeos-darwin/cloakserver/`) ist der Fensterserver von ClokeOS, zunächst als X11-Fenstermanager mit eigener Oberfläche, geschrieben in C mit Xlib, cairo, pango und librsvg:

- Hintergrund „Goldenes Tor“: `drawBay()` aus `clokeos.html` Zeile für Zeile nach cairo übertragen (`wall.c`), mit demselben Zufallsgenerator, damit Sterne, Lichter und Nebel an denselben Stellen sitzen; hell am Tag, dunkel von 19 bis 7 Uhr
- Menüleiste: ClokeOS-Menü (Über ClokeOS, Systemeinstellungen, Aktivitätsanzeige, Programme, Neustart … und Ausschalten … mit Rückfrage), Name des aktiven Programms mit Menü (ausblenden, beenden), Ablage (neues Fenster, Fenster schließen), Fenster (ins Dock, zoomen, Liste der Fenster), Netzwerk mit IP-Adresse, Suche (öffnet den App-Finder), Erscheinungsbild (automatisch, hell, dunkel) und Uhr mit Datum
- Dock mit den Icons aus `clokeos.html`, Laufanzeigen, Namensschild beim Überfahren und Kontextmenü auf der rechten Maustaste; ein Klick startet das Programm oder holt seine Fenster zurück
- Compositor (`comp.c`): CloakServer setzt das Bild selbst zusammen (X-Erweiterungen Composite, Damage, XFixes, Render). Fenster bekommen weiche Schatten und runde Ecken ohne Treppchen, das aktive einen größeren Schatten. Dock, Menüs und Namensschilder sind Glas: Sie zeigen unscharf, was dahinter liegt, auch Fenster, mit Tönung und Lichtkante wie im Liquid-Glass-Stil des Prototyps. Neu gezeichnet wird nur, was sich geändert hat; unter Emulation kostet ein ganzes Bild 100 bis 300 ms, ein Teilbild meist 50 bis 60 ms, ein blinkender Cursor 5 bis 15 ms. CloakServer meldet sich als Compositor (`_NET_WM_CM_S0`), GTK-Programme mit eigener Durchsicht bekommen dann 32-Bit-Rahmen. Fehlt Composite, oder mit `--compositor aus`, arbeitet CloakServer wie zuvor ohne Compositor; das Glas zeigt dann nur das Hintergrundbild, die Ecken schneidet XShape ab.
- Fenster: Rahmen mit Titelleiste und Ampelknöpfen (schließen, ins Dock, zoomen), runde Ecken, ziehen, Größe an der Ecke unten rechts ändern, Doppelklick auf den Titel zoomt, Alt+Tab wechselt das Fenster, Alt+F4 schließt es
- „Über ClokeOS“ wie im Prototyp, mit Version und Build aus `/etc/clokeos-release` und den Werten des Rechners (`sysctl`)

Hinter den Dock-Symbolen stehen Programme des PureDarwin-Images:

| Dock | Programm |
|---|---|
| Fundus | Thunar |
| Programme | xfce4-appfinder |
| Search | NetSurf |
| Terminal | xfce4-terminal, mit Farben und Schrift wie im Prototyp (`/etc/xdg/xfce4/terminal/terminalrc`) |
| Notizen | nano in xfce4-terminal (`~/Notizen.txt`) |
| Rechner | xcalc |
| Kalender | `/usr/libexec/clokeos-kalender` in xfce4-terminal: drei Monate, deutsch, heute markiert |
| Aktivitätsanzeige | `/usr/libexec/clokeos-aktivitaet` in xfce4-terminal: Prozesse nach Rechenzeit |
| Systemeinstellungen | xfce4-settings-manager |
| Papierkorb | Thunar in `~/.Trash` |

Gebaut wird CloakServer auf dem Mac, ohne Linux und ohne die VM: `cloakserver/build.sh`, das `brand.sh` aufruft, holt mit `debugfs` Header und Bibliotheken direkt aus dem Desktop-Image nach `~/ClokeOS-work/sysroot/` und übersetzt mit Apples clang für `x86_64-apple-macos11.0`, so wie PureDarwins eigene Programme gebaut sind (Mach-O für macOS 11, klassische dyld-Info). Zwei Eigenheiten: PureDarwins `sys/cdefs.h` braucht `-DPLATFORM_MacOSX`, sonst verweist das Programm auf Symbole wie `fopen$UNIX2003`, die PureDarwins libSystem nicht hat; und `ifaddrs.h` fehlt, `getifaddrs` gibt es aber. `cloakserver/resources.py` lässt den Icon-Code aus `clokeos.html` von macOS' eigener JavaScript-Engine ausführen (`osascript -l JavaScript`) und schreibt Logo und Icons als SVG nach `Resources/`. Eine Änderung an den Icons im Prototyp landet so beim nächsten `./brand.sh` im Image.

Grenzen:

- Die Unschärfe ist eine Näherung: Der Compositor verkleinert den Hintergrund viermal auf die Hälfte und zieht ihn wieder auf. Titelleisten sind deckend, nicht aus Glas, und es gibt keine Animationen (Öffnen, Ins Dock).
- Die Programme sind englisch, denn PureDarwins libc kennt keine deutsche Locale (`Locale not supported by C library`). Ihre Menüs stehen im Programmfenster, nicht in der Menüleiste.
- Bei Emulation brauchen Programme 10 bis 25 Sekunden zum Start. Es gibt keine Anmeldung, die Sitzung läuft als root.
- Nur x86_64. PureDarwins Desktop-Image für arm64 (3,1 GiB) ist nicht geladen; dort startet Xorg, GL ist laut PureDarwin aber kaputt.
- Das Desktop-Image gehört nicht zu den Releases (6 GiB), `release.sh` baut nur das Minimal-Image.

### Architekturen

| Ziel | Stand | Basis |
|---|---|---|
| x86_64 (QEMU q35, UEFI) | gebaut, gebootet, als Release | PureDarwin 20.5, `puredarwin-minimal.img` |
| x86_64 mit Desktop | gebaut, gebootet bis CloakServer, kein Release | PureDarwins Vorschau-Image `puredarwin.img` (6,1 GiB, von puredarwin.org) |
| arm64 (QEMU virt) | Skripte vorbereitet, nie gebaut oder gebootet | PureDarwins Vorschau-Image `puredarwin-arm64-virt-minimal.img` (578 MiB, von puredarwin.org) |
| Raspberry Pi 3 | offen | PureDarwin bootet dort bis zur Shell, bietet aber kein fertiges Image an |

Die Architektur wählt `CLOKEOS_ARCH`, für alle Skripte gleich:

```bash
CLOKEOS_ARCH=arm64 ./brand.sh && CLOKEOS_ARCH=arm64 ./run.sh
```

Für arm64 lädt `fetch.sh` PureDarwins Vorschau-Image. Dort ist die Wurzel eine RAM-Disk in der EFI-Partition (`ramdisk.img`, Boot-Argument `rd=md0`), deshalb schreibt `brand.sh` das Overlay auch dort hinein, wenn es eine findet. `run.sh` startet dann `qemu-system-aarch64` wie PureDarwins eigener Runner (virt, GICv3, virtio-Disk und -Netz, ramfb). Auf einem Mac mit Apple-Chip kann `--hvf` Apples Hypervisor statt Emulation nutzen, ausprobiert ist das noch nicht. Ein Bootlogo gibt es auf arm64 noch nicht: Dafür muss der Loader-Patch für `BOOTAA64.EFI` gebaut werden, und mingw-w64 aus Homebrew kann kein arm64.

Ein Raspberry-Pi-Image müsste ClokeOS selbst bauen (PureDarwins Nix-Ziel `image-arm64-bcm2837`, siehe unten). Das `.img.xz`-Format der Releases ist so gewählt, dass Raspberry Pi Imager und balenaEtcher es direkt schreiben können.

### Releases

```bash
cd clokeos-darwin && ./release.sh                 # x86_64; mehrere: ./release.sh x86_64 arm64
cd clokeos-darwin && ./release.sh --upload        # zusätzlich als Entwurf v<version> auf GitHub
```

`release.sh` baut jedes Image frisch, packt es mit xz und legt `SHA256SUMS` und `RELEASE-NOTES.md` (Downloads, Starten, was geht und was nicht, Quellen und Lizenzen) nach `~/ClokeOS-work/dist/<version>/`. Mit `--upload` landet alles als Release-Entwurf `v<version>` im GitHub-Repo, aber nur wenn alles eingecheckt und gepusht ist; veröffentlicht wird von Hand. Ist die Version dort schon veröffentlicht, bricht das Skript ab, dann `CLOKE_VERSION` in `config.sh` erhöhen. Die Images enthalten PureDarwin und damit auch GPL-Teile (corecrypto); die Release-Notizen verweisen deshalb auf die genauen Quellstände. Releases enthalten nur das Minimal-Image; die Desktop-Variante (6 GiB) baut man mit `CLOKEOS_FLAVOR=desktop ./brand.sh` selbst.

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
3. **Fensterserver.** Begonnen: CloakServer läuft in der Desktop-Variante als X11-Fenstermanager auf Xorg und PureDarwins GOP-Treiber, mit Hintergrund, Menüleiste, Dock und Fenstern im ClokeOS-Stil (siehe [Desktop: CloakServer](#desktop-cloakserver)). Dafür übergibt der Loader dem Kernel den Framebuffer wieder, und eine 15-Byte-Korrektur in `IOGOPFramebuffer` behebt die falschen Farben. CloakServer setzt das Bild inzwischen selbst zusammen (Schatten, runde Ecken, Glas über Fenstern). Als Nächstes: eine Anmeldung wie im Prototyp, Animationen, arm64 (dort startet Xorg, GL ist laut PureDarwin aber noch kaputt), und auf längere Sicht ein eigener Wayland-Compositor statt Xorg (wlroots samt Headern ist im Desktop-Image, PureDarwin hat dafür ein eigenes Backend).
4. **Shell portieren.** Menüleiste und Dock gibt es jetzt nativ in CloakServer. Offen sind Fundus, Search und die übrigen Apps aus `clokeos.html` als native Programme; bis dahin stehen Thunar, NetSurf und andere Programme des PureDarwin-Images hinter den Dock-Symbolen. Alternativ eine Browser-Engine als Shell-Laufzeit: Mit WebKitGTK könnte `clokeos.html` fast unverändert laufen; PureDarwin arbeitet daran, laut Statusseite läuft es noch nicht.
5. **Installer-Image.** Bootfähiges ISO mit Bootloader, Kernel, Userland und CloakServer.

Für Schritt 3 und 4 reicht das Minimal-Image nicht mehr. PureDarwin bietet auf seiner Website Vorschau-Images mit Desktop an: `puredarwin.img` (x86_64, 6,1 GiB, X11, Wayland, Xfce, Wine, Compiler) und `puredarwin-arm64-virt.img` (3,1 GiB). Das x86_64-Image ist die Basis der Desktop-Variante; CloakServer entsteht darauf, ohne PureDarwin selbst zu bauen. Eigene Images (etwa für den Raspberry Pi oder mit eigenem Kernel) gehen nur mit PureDarwins Nix-Build unter Linux (etwa 60 GB) und Apples `MacOSX11.3.sdk`, das man selbst besorgen muss, weil es nicht weitergegeben werden darf; es gehört zu Xcode 12.5 (developer.apple.com). PureDarwins Build erwartet eine bestimmte Prüfsumme des SDK-Archivs, ein selbst gepacktes SDK braucht deshalb einen angepassten Hash.

Wichtig: Apples Markennamen, Icons und Schriften dürfen nicht übernommen werden. ClokeOS nutzt deshalb eigene Namen, eigene Icons und ein eigenes Logo.
