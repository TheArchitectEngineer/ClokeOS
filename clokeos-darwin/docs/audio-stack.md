# Audio on ClokeOS: what exists, what is missing, how to get to Core Audio

Status 29.10.2026. Everything below was measured on the PureDarwin 20.5 images that ClokeOS is built on
(x86_64 desktop `puredarwin.img`, arm64 `clokeos-2.5-arm64-desktop.img`) with Homebrew QEMU, except where
marked *untested* or *from memory*. Long-term goal: Logic Pro (CoreAudio, AudioToolbox, AudioUnit,
AVFAudio, CoreMIDI, CoreAudioKit, Accelerate; Audio Units hosted via XPC).

## 1. What PureDarwin 20.5 has

| Piece | x86_64 | arm64 |
|---|---|---|
| `IOAudioFamily` (Apple's, APSL, vendored unmodified, `src/Kernel/Extensions/IOAudioFamily`) | in `/System/Library/Extensions` and in the kernel collection (`pd-rebuild-kc` lists it) | **absent**: not in `Extensions`, not in the kernel |
| `RavynHDAudio.kext` (Intel HD Audio controller + codec, playback only) | loaded, attaches | file is in `Extensions` but is not in the kernel (no IOAudioFamily to link against), never matches |
| matches | PCI `8086:2668` (ICH6 HDA = QEMU `intel-hda`), `8086:293e` (ICH9), `8086:3198` (Gemini Lake) | the same IDs, never used |
| `/dev/dsp0` | yes, character device (21,0), `write()` only, fixed 48 kHz / 16 bit / stereo, no ioctl | no |
| AppleHDA, IOUSBAudio, virtio-sound, AC97, ES1370, HDA codecs other than the one `RavynHDAudio` enumerates | none | none |
| user-space audio (coreaudiod, CoreAudio.framework, AudioToolbox, AudioUnit, CoreMIDI) | none: `/System/Library/Frameworks` has only CoreFoundation, CoreServices, DiskArbitration, DriverKit, IOKit, Kernel, OpenGL, Security, System, SystemConfiguration | same |

`kextstat`/`kextload`/`kmutil` do not exist: kexts are linked into the kernel collection at image build time
(`pd-rebuild-kc`, `kc-builder`), nothing is loaded at run time. A driver is therefore "there" only if it is in
that list. The arm64 kernel is a different, monolithic build (`/System/Library/Kernels/kernel.release.virt`)
and ClokeOS has no arm64 kext build path yet (`kext/build.sh` is x86_64 only).

### What the guest kernel sees (ioreg)

QEMU devices attached: `intel-hda` + `hda-duplex`/`hda-output`, `virtio-sound-pci`, `usb-audio` on the xHCI.
All four exist in the installed QEMU (also `ac97`, `ES1370`, `ich9-intel-hda`, `virtio-sound-device`);
audio backends there: `none`, `coreaudio`, `dbus`, `wav`.

* **x86_64 (TCG):** `pci8086,2668` gets child `RavynHDAudio` → `RavynHDAudioEngine` (flavor 1,
  "Intel HD Audio", 48000 Hz, 65536 sample frames per buffer) → one output `IOAudioStream` (16-bit signed
  linear PCM, 2 ch, mixable). `IOAudioDevice`, `IOAudioEngine`, `IOAudioStream` instances exist in
  `IOKitDiagnostics`; `/dev/dsp0` appears. `virtio-sound` (`1af4:1059`) is a bare `IOPCIDevice` without a
  driver; the USB audio device enumerates as `IOUSBDevice` only (no interface driver).
* **arm64 (HVF):** the same PCI devices are enumerated (`pci8086,2668`, `pci1af4,1059`, USB device 0x46f4),
  all without a driver; there is no `/dev/dsp*`. Audio on arm64 therefore needs the kernel pieces to be built
  first, not just a new driver.

## 2. Design options for a Core Audio HAL

What Logic needs from the system: `AudioObject`/`AudioDevice` HAL API with IOProc callbacks driven by the
device clock (`AudioDeviceStart`, `kAudioDevicePropertyBufferFrameSize`, `…SafetyOffset`, `…Latency`, device
and stream formats, `AudioHardwareCreateAggregateDevice`), AudioToolbox (`AudioUnit` graph/`AUGraph`,
`AudioConverter`, `ExtAudioFile`, `AudioFile`), AUv2 component registry and out-of-process AUv3 hosting, and
CoreMIDI (`MIDIClient`, ports, endpoints, USB MIDI).

### Kernel side: IOAudioFamily-based kext vs. own kext vs. user-space driver

1. **Keep IOAudioFamily + one `IOAudioEngine` subclass per hardware (recommended first step).** It is already
   in PureDarwin, it gives per-client buffers (`IOAudioEngineUserClient`), mixing, format conversion and the
   sample-clock timestamps (`takeTimeStamp`, `IOAudioEngineUserClient::performClientIO`) that a user-space HAL
   needs, and it is exactly the interface Apple's own HAL (`AppleHDA` → `coreaudiod`) sits on. Cost: a
   `coreaudiod` substitute must speak this user-client protocol (shared sample buffer + clock mach port) – the
   headers are in the tree. New drivers (virtio-sound, USB audio class, more HDA codecs and capture) follow
   the style of `kext/ClokeI2CHID`/`ClokeRtw88`: a single `.cpp` + `Info.plist` built by `kext/build.sh`.
   Matching lists in `Info.plist` and a line in `pd-rebuild-kc` are all that registers it.
2. **Own minimal kext with a simple device node (`/dev/dsp0`-style, ring buffer + `read`/`write`/`ioctl`).**
   Less code per device but no clock timestamps, no mixing, each driver reinvents the node, and Core Audio
   would still need an adapter. This is what `RavynHDAudio` does today for `/dev/dsp0`; fine for a sine wave,
   wrong for a DAW.
3. **User-space driver (DriverKit-style or `IOUserClient` on `IOPCIDevice`).** `DriverKit.framework`
   headers are in the image, but nothing in the image implements dext hosting (no `kernelmanagerd`, no
   `IOUserServer` clients seen: `IOUserServer`=0 in diagnostics). PCI access from a normal process needs
   `IOPCIDeviceUserClient`, not verified here. Attractive only for USB audio class (libusb-style via the
   existing `IOUSBDevice` user client). *Not recommended now.*

### User-space side

* `coreaudiod` substitute: a daemon that opens the `IOAudioEngine` user client(s), publishes the HAL
  objects over a mach service (Apple uses `com.apple.audio.coreaudiod`; the client library
  `CoreAudio.framework` talks to it with MIG/XPC), and keeps per-device IO threads at real-time priority.
  Real Mac frameworks are binary-incompatible with a re-implementation of a private protocol, so the plan is a
  *new* `CoreAudio.framework` with the public HAL API (`AudioObjectGetPropertyData`, `AudioDeviceCreateIOProcID`
  …) that is a thin client of our own daemon, not Apple's.
* AudioToolbox/AudioUnit/CoreMIDI: pure user-space API surfaces on top of the HAL. `AudioConverter`,
  `ExtAudioFile`, `AudioFile` are the bulk (codecs: PCM, ALAC/AAC need Apple's closed codecs; Logic ships
  mostly PCM/CAF/AIFF). AUv2 component lookup works off bundle `Info.plist` (`AudioComponents`) and is
  straightforward; AUv3 needs XPC + `NSExtension`, which depends on Foundation XPC that does not exist yet.
* **Darling** (checked against `darlinghq/darling` master, GitHub API, not cloned): `src/frameworks/` has
  `CoreMIDI`, `AVFAudio`, `CoreAudioKit`, `Accelerate`, `AVFoundation`, `AudioVideoBridging` and
  `dev-stubs/AudioToolbox`; `CoreMIDI.m` is a generated stub that prints "not implemented" with
  `STUB_VERBOSE`. No `CoreAudio` or `AudioUnit` directory in `src/frameworks`. So Darling contributes
  **headers and exported-symbol lists (link-time stubs), not an implementation**. The three repos that
  `macapps/` builds (`darling-corefoundation`, `-foundation`, `-cocotron`) contain no audio. This lets
  Mac apps *link* (`macapps/machcheck.py` can list what is missing), but every call still needs our own
  code. Accelerate (vDSP) is big, mostly math; needs a real implementation (or Apple's if ever possible).

### Latency expectations for a DAW

A DAW wants an IO buffer of 32–128 frames at 44.1–96 kHz (< 3 ms), in a real-time thread that is woken by the
device clock with a safety offset of a few tens of frames. What we have is far from that:

* `RavynHDAudio` uses a **65536-frame ring** (1.36 s at 48 kHz) and **polls LPIB from `writePCM`**; there is no
  interrupt-driven position update used yet and the engine's timestamps are coarse. Interactive latency today
  is therefore on the order of **tens to hundreds of ms**, set by the write-ahead the writer chooses, not
  by hardware.
* Under QEMU the host adds its own buffering (CoreAudio/`wav` backend, `hda` timer 10 ms by default);
  realistically **20–50 ms** round trip in a VM is the floor, usable for a demo, not for tracking.
* On real hardware an HDA codec can do 128 frames/48 kHz (2.7 ms) if the driver uses BDL interrupts at that
  granularity, takes timestamps from them, and the scheduler can give the IO thread a real-time class. XNU 20.5
  has `THREAD_TIME_CONSTRAINT_POLICY`, so this is feasible; it needs the PureDarwin scheduler to behave on
  one core (`CLOKEOS_VM_SMP` = 1 today). *Untested.*
* Plug-in hosting out of process (AUv3) adds one buffer of latency per hop; Logic's default in-process AUv2
  path avoids that.

## 3. Smallest proof

**Done (x86_64 only, TCG):** booted the unmodified PureDarwin desktop image with
`-device intel-hda -device hda-output,audiodev=snd0` and `-audiodev wav,out.frequency=48000`, then wrote a
3 s, 440 Hz, 12000-peak s16le stereo sine to `/dev/dsp0` from the guest's `python3`.

* `open`/`write` succeed: all 576000 bytes accepted, the write blocks at roughly real time, and QEMU's WAV
  file grew to 4.6 s at 48 kHz. So the HDA stream is running and DMA is being consumed.
* **The captured WAV is all zeros (peak 0). No tone was heard or recorded, so this is *not* a working output
  path.** An earlier run with a 2 s tone and the default 44.1 kHz backend was likewise silent (and was cut
  off by quitting QEMU before the WAV was finalised).
* A second run with `hda-duplex`, a 480 Hz tone at peak 20000 and 2 s of 20000-peak data also gave an all-zero
  WAV (96549 frames). `dmesg` shows only `RavynHDAudio: published /dev/dsp0` from the driver (no codec
  enumeration lines at the default log level), so codec setup could not be confirmed from the log.
* Not determined why. Candidates, none checked: the image's `RavynHDAudio` binary is older than the source
  that was read here (image 23.08., source rev 1bbaeb2 25.08.; the source has fixes for garbled playback);
  codec routing/amp verbs not taking effect on QEMU's `hda-output` codec; ring data written but not seen by
  DMA; or `python3`'s unbuffered write path. Next step: boot with `IOLog` visible (`dmesg | grep Ravyn`),
  read back the SD registers, try `hda-duplex`, and rebuild `RavynHDAudio` from the pinned source with
  `kext/build.sh` (x86_64) to rule out the stale binary.
* arm64: nothing to test, there is no driver in the kernel (section 1). Missing there, exactly:
  (1) `IOAudioFamily` and `RavynHDAudio` built for arm64 and added to the arm64 kernel/`pd-rebuild-kc`
  equivalent (no arm64 kext build path in `kext/build.sh`), (2) a driver for something QEMU `virt` offers
  that is not PCI-HDA-specific-risky: `virtio-sound-pci` (1af4:1059, no PureDarwin driver, would be a new
  ~500-line `IOAudioEngine` over the virtio queues, `IOVirtIOFamily` exists) is the natural one.
  `intel-hda` also works on `virt` in QEMU, so (1) alone may be enough for a first sound, *untested*.
* Not done: any user-space client of `IOAudioEngineUserClient`, any CoreAudio API, capture, MIDI.
