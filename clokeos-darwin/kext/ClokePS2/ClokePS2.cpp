/*
 * ClokePS2: PS/2-Controller (i8042) für Xorg, also für die eingebaute Tastatur und das Touchpad
 * (im PS/2-Modus) von Notebooks.
 *
 * PureDarwins ApplePS2Controller gibt Tasten nur an IOHIDSystem und damit an die Textkonsole, für die
 * Maus hat er keinen Treiber. Xorg liest Eingaben bei PureDarwin aber nur aus Zeichengeräten
 * (puredarwininput, sonst /dev/usb_hid_kbd und /dev/usb_hid_mouse von IOUSBHIDDriver). Dieser Treiber
 * übernimmt den Controller deshalb ganz (höhere IOProbeScore als ApplePS2Controller auf demselben
 * Knoten "ps2controller") und legt zwei Geräte im selben Format an:
 *
 *   /dev/clokeos_kbd     8 Byte je Taste: [4] HID-Usage (Seite 7), [5] 1 gedrückt / 0 losgelassen
 *   /dev/clokeos_mouse  12 Byte je Bewegung: [5] Tasten (Bit 0 links, 1 rechts, 2 Mitte),
 *                       [6] dx, [7] dy (nach unten positiv), [8] Rad (nach oben positiv)
 *
 * Gelesen wird nicht-blockierend (puredarwininput öffnet mit O_NONBLOCK) und mit select.
 * Die Tastatur liefert Scancodes Satz 1 (der Controller übersetzt), die Maus 3-Byte-Pakete oder mit
 * Rad (IntelliMouse) 4 Byte.
 */
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOService.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <architecture/i386/pio.h>

extern "C" {
#include <miscfs/devfs/devfs.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/select.h>
#include <sys/uio.h>
#include <sys/vnode.h>
}

#define LOG(fmt, ...) IOLog("ClokePS2: " fmt "\n", ##__VA_ARGS__)

enum { kData = 0x60, kCmd = 0x64 };
enum { kOBF = 0x01, kIBF = 0x02, kAux = 0x20 };

/* ---- Ringpuffer mit einem Zeichengerät ---------------------------------------------------- */

struct EventQueue {
  enum { kSize = 4096 };
  uint8_t buf[kSize];
  unsigned head, tail; /* in Bytes; immer ganze Ereignisse */
  unsigned recsize;
  IOLock *lock;
  uint8_t sel[256]; /* struct selinfo ist nicht öffentlich; XNU legt sie bei Bedarf in Nullspeicher an */
  bool open;

  bool init(unsigned rs) {
    recsize = rs;
    head = tail = 0;
    lock = IOLockAlloc();
    bzero(sel, sizeof sel);
    return lock != nullptr;
  }
  void push(const uint8_t *rec) {
    IOLockLock(lock);
    if (open) {
      unsigned next = (tail + recsize) % (kSize - kSize % recsize);
      if (next != head) { /* sonst voll: verwerfen */
        memcpy(buf + tail, rec, recsize);
        tail = next;
      }
      IOLockWakeup(lock, this, false);
    }
    IOLockUnlock(lock);
    selwakeup((struct selinfo *)sel);
  }
};

static EventQueue gKbd, gMouse;
static int gMajor = -1;
static void *gNodeKbd, *gNodeMouse;

static EventQueue *queueFor(dev_t dev) { return ((uint32_t)dev & 0xffffff) == 0 ? &gKbd : &gMouse; }

static int ps2_open(dev_t dev, int, int, proc_t) {
  EventQueue *q = queueFor(dev);
  IOLockLock(q->lock);
  q->open = true;
  q->head = q->tail = 0;
  IOLockUnlock(q->lock);
  return 0;
}

static int ps2_close(dev_t dev, int, int, proc_t) {
  EventQueue *q = queueFor(dev);
  IOLockLock(q->lock);
  q->open = false;
  IOLockUnlock(q->lock);
  return 0;
}

static int ps2_read(dev_t dev, struct uio *uio, int ioflag) {
  EventQueue *q = queueFor(dev);
  uint8_t rec[16];
  int err = 0;
  bool any = false;
  IOLockLock(q->lock);
  while (!err && uio_resid(uio) >= (user_ssize_t)q->recsize) {
    if (q->head == q->tail) {
      if (any) break;
      if (ioflag & IO_NDELAY) {
        err = EWOULDBLOCK;
        break;
      }
      if (IOLockSleep(q->lock, q, THREAD_INTERRUPTIBLE) != THREAD_AWAKENED) {
        err = EINTR;
        break;
      }
      continue;
    }
    memcpy(rec, q->buf + q->head, q->recsize);
    q->head = (q->head + q->recsize) % (EventQueue::kSize - EventQueue::kSize % q->recsize);
    IOLockUnlock(q->lock);
    err = uiomove((char *)rec, (int)q->recsize, uio);
    any = true;
    IOLockLock(q->lock);
  }
  IOLockUnlock(q->lock);
  return err;
}

static int ps2_select(dev_t dev, int which, void *wql, proc_t p) {
  EventQueue *q = queueFor(dev);
  if (which != FREAD) return 0;
  IOLockLock(q->lock);
  bool ready = q->head != q->tail;
  IOLockUnlock(q->lock);
  if (ready) return 1;
  selrecord(p, (struct selinfo *)q->sel, wql);
  return 0;
}

static int ps2_write(dev_t, struct uio *, int) { return ENODEV; }
static int ps2_ioctl(dev_t, u_long, caddr_t, int, proc_t) { return ENOTTY; }
static int ps2_stop(struct tty *, int) { return 0; }
static int ps2_reset(int) { return 0; }
static void ps2_strategy(struct buf *) {}

static struct cdevsw gCdev = {ps2_open,   ps2_close, ps2_read, ps2_write,    ps2_ioctl, ps2_stop, ps2_reset,
                              nullptr,    ps2_select, nullptr, ps2_strategy, nullptr,   nullptr,  0};

/* ---- Scancodes Satz 1 -> HID-Usages (Seite 7) ----------------------------------------------- */

static const uint8_t kSet1[0x59] = {
    0,    0x29, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2d, 0x2e, 0x2a, 0x2b, /* 00 */
    0x14, 0x1a, 0x08, 0x15, 0x17, 0x1c, 0x18, 0x0c, 0x12, 0x13, 0x2f, 0x30, 0x28, 0xe0, 0x04, 0x16, /* 10 */
    0x07, 0x09, 0x0a, 0x0b, 0x0d, 0x0e, 0x0f, 0x33, 0x34, 0x35, 0xe1, 0x31, 0x1d, 0x1b, 0x06, 0x19, /* 20 */
    0x05, 0x11, 0x10, 0x36, 0x37, 0x38, 0xe5, 0x55, 0xe2, 0x2c, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, /* 30 */
    0x3f, 0x40, 0x41, 0x42, 0x43, 0x53, 0x47, 0x5f, 0x60, 0x61, 0x56, 0x5c, 0x5d, 0x5e, 0x57, 0x59, /* 40 */
    0x5a, 0x5b, 0x62, 0x63, 0,    0,    0x64, 0x44, 0x45,                                           /* 50 */
};

static uint8_t extUsage(uint8_t code) { /* nach E0 */
  switch (code) {
  case 0x1c: return 0x58; /* Enter im Ziffernfeld */
  case 0x1d: return 0xe4; /* Strg rechts */
  case 0x20: return 0x7f; /* Ton aus */
  case 0x2e: return 0x81; /* leiser */
  case 0x30: return 0x80; /* lauter */
  case 0x35: return 0x54; /* / im Ziffernfeld */
  case 0x37: return 0x46; /* Druck */
  case 0x38: return 0xe6; /* Alt Gr */
  case 0x47: return 0x4a; /* Pos1 */
  case 0x48: return 0x52; /* hoch */
  case 0x49: return 0x4b; /* Bild hoch */
  case 0x4b: return 0x50; /* links */
  case 0x4d: return 0x4f; /* rechts */
  case 0x4f: return 0x4d; /* Ende */
  case 0x50: return 0x51; /* runter */
  case 0x51: return 0x4e; /* Bild runter */
  case 0x52: return 0x49; /* Einfg */
  case 0x53: return 0x4c; /* Entf */
  case 0x5b: return 0xe3; /* Windows/Befehl links */
  case 0x5c: return 0xe7; /* rechts */
  case 0x5d: return 0x65; /* Menü */
  default: return 0;      /* darunter die falschen Umschalttasten E0 2A/E0 AA */
  }
}

/* ---- Treiber ------------------------------------------------------------------------------- */

class ClokePS2 : public IOService {
  OSDeclareDefaultStructors(ClokePS2)

  IOWorkLoop *wl = nullptr;
  IOInterruptEventSource *irqKbd = nullptr, *irqMouse = nullptr;
  bool haveMouse = false, wheel = false;
  bool e0 = false;
  int e1 = 0;
  uint8_t down[32] = {}; /* gedrückte Usages, damit Wiederholungen der Tastatur nicht als neue Tasten kommen */
  uint8_t pkt[4] = {};
  int npkt = 0;

  bool waitWrite();
  bool waitRead(uint8_t *b, int ms);
  bool cmd(uint8_t c);
  bool writeData(uint8_t d);
  bool auxCmd(uint8_t c, uint8_t *ack = nullptr);
  void flush();
  void key(uint8_t usage, bool pressed);
  void kbdByte(uint8_t b);
  void mouseByte(uint8_t b);
  void interrupt(IOInterruptEventSource *, int);
  void drain(bool fromIrq);
  void pollTick(IOTimerEventSource *);
  IOTimerEventSource *pollTimer = nullptr;
  unsigned irqs = 0, polled = 0, kbdBytes = 0, mouseBytes = 0, ticks = 0;
  bool bsdReady(void *, IOService *, IONotifier *);
  IONotifier *bsdNotifier = nullptr;

public:
  bool start(IOService *provider) override;
  void stop(IOService *provider) override;
};

OSDefineMetaClassAndStructors(ClokePS2, IOService)

bool ClokePS2::waitWrite() {
  for (int i = 0; i < 10000; i++) {
    if (!(inb(kCmd) & kIBF)) return true;
    IODelay(10);
  }
  return false;
}

bool ClokePS2::waitRead(uint8_t *b, int ms) {
  for (int i = 0; i < ms * 100; i++) {
    if (inb(kCmd) & kOBF) {
      *b = inb(kData);
      return true;
    }
    IODelay(10);
  }
  return false;
}

bool ClokePS2::cmd(uint8_t c) {
  if (!waitWrite()) return false;
  outb(kCmd, c);
  return true;
}

bool ClokePS2::writeData(uint8_t d) {
  if (!waitWrite()) return false;
  outb(kData, d);
  return true;
}

void ClokePS2::flush() {
  for (int i = 0; i < 64 && (inb(kCmd) & kOBF); i++) {
    (void)inb(kData);
    IODelay(50);
  }
}

/* Befehl an die Maus; true bei ACK (0xFA) */
bool ClokePS2::auxCmd(uint8_t c, uint8_t *ack) {
  uint8_t b = 0;
  if (!cmd(0xd4) || !writeData(c) || !waitRead(&b, 300)) return false;
  if (ack) *ack = b;
  return b == 0xfa;
}

bool ClokePS2::start(IOService *provider) {
  if (!IOService::start(provider)) return false;
  uint8_t st = inb(kCmd);
  LOG("status 0x%02x", st);
  if (st == 0xff) { /* kein i8042 an den Ports */
    LOG("no controller (status 0xff)");
    return false;
  }
  if (!gKbd.init(8) || !gMouse.init(12)) return false;

  /* Geräte während der Einrichtung ab, Interrupts aus */
  cmd(0xad);
  cmd(0xa7);
  flush();
  uint8_t cfg = 0;
  if (!cmd(0x20) || !waitRead(&cfg, 100)) {
    LOG("no answer from the controller");
    return false;
  }
  LOG("config 0x%02x", cfg);
  cfg &= (uint8_t)~0x03;
  cfg |= 0x40; /* Scancodes in Satz 1 übersetzen */
  cmd(0x60);
  writeData(cfg);

  /* Tastatur */
  cmd(0xae);
  writeData(0xf4); /* Abtasten an */
  uint8_t b = 0;
  bool ack = waitRead(&b, 100);
  LOG("keyboard enable: %s 0x%02x", ack ? "answer" : "no answer", b);

  /* Maus/Touchpad am Aux-Port */
  cmd(0xa8);
  if (cmd(0x20) && waitRead(&cfg, 100) && !(cfg & 0x20)) { /* Takt des Aux-Ports läuft: es gibt ihn */
    uint8_t id = 0;
    if (auxCmd(0xff)) { /* Reset: danach AA 00 */
      uint8_t r1 = 0, r2 = 0;
      waitRead(&r1, 1000);
      waitRead(&r2, 100);
      haveMouse = r1 == 0xaa;
    }
    if (haveMouse) {
      /* IntelliMouse: Abtastraten 200, 100, 80 schalten das Rad frei, die Kennung ist dann 3 */
      static const uint8_t rates[] = {200, 100, 80};
      for (uint8_t r : rates) {
        auxCmd(0xf3);
        auxCmd(r);
      }
      if (auxCmd(0xf2)) waitRead(&id, 100);
      wheel = id == 3;
      auxCmd(0xf3);
      auxCmd(100);
      auxCmd(0xe8); /* Auflösung 4 Punkte/mm */
      auxCmd(3);
      auxCmd(0xf4); /* Daten an */
    }
    LOG("mouse port: %s, id %u%s", haveMouse ? "device" : "nothing", id, wheel ? " (wheel)" : "");
  }
  flush();

  wl = IOWorkLoop::workLoop();
  if (!wl) return false;
  auto action = OSMemberFunctionCast(IOInterruptEventAction, this, &ClokePS2::interrupt);
  irqKbd = IOInterruptEventSource::interruptEventSource(this, action, provider, 1);
  if (!irqKbd || wl->addEventSource(irqKbd) != kIOReturnSuccess) {
    LOG("keyboard interrupt (IRQ 1) not available, polling only");
    OSSafeReleaseNULL(irqKbd);
  }
  if (haveMouse) {
    irqMouse = IOInterruptEventSource::interruptEventSource(this, action, provider, 12);
    if (!irqMouse || wl->addEventSource(irqMouse) != kIOReturnSuccess) haveMouse = false;
  }
  if (irqKbd) irqKbd->enable();
  if (haveMouse && irqMouse) irqMouse->enable();
  /* Zusätzlich alle 10 ms abfragen: auf manchen Rechnern kommen die Interrupts des Controllers nicht
   * an (Weiterleitung über ACPI/IOAPIC, die PureDarwin nicht kennt); die Bytes gehen sonst verloren */
  pollTimer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this,
                                                                             &ClokePS2::pollTick));
  if (pollTimer && wl->addEventSource(pollTimer) == kIOReturnSuccess) pollTimer->setTimeoutMS(10);

  cmd(0x20);
  waitRead(&cfg, 100);
  cfg |= 0x01;
  if (haveMouse) cfg |= 0x02;
  cmd(0x60);
  writeData(cfg);

  /* Der Treiber startet vor BSD (OSBundleRequired Console); devfs gibt es erst, wenn IOBSD da ist */
  bsdNotifier = addMatchingNotification(gIOPublishNotification, resourceMatching("IOBSD"),
                                        OSMemberFunctionCast(IOServiceMatchingNotificationHandler, this,
                                                             &ClokePS2::bsdReady),
                                        this, nullptr, 0);
  registerService();
  return true;
}

bool ClokePS2::bsdReady(void *, IOService *, IONotifier *) {
  if (gMajor >= 0) return true;
  gMajor = cdevsw_add(-1, &gCdev);
  if (gMajor < 0) {
    LOG("cdevsw_add failed");
    return true;
  }
  gNodeKbd = devfs_make_node(makedev((uint32_t)gMajor, 0), DEVFS_CHAR, 0, 0, 0644, "clokeos_kbd");
  if (haveMouse) gNodeMouse = devfs_make_node(makedev((uint32_t)gMajor, 1), DEVFS_CHAR, 0, 0, 0644, "clokeos_mouse");
  LOG("ready: /dev/clokeos_kbd%s%s", haveMouse ? ", /dev/clokeos_mouse" : "",
      gNodeKbd && (gNodeMouse || !haveMouse) ? "" : " (devfs_make_node failed)");
  return true;
}

void ClokePS2::stop(IOService *provider) {
  if (bsdNotifier) bsdNotifier->remove();
  if (pollTimer) {
    pollTimer->cancelTimeout();
    if (wl) wl->removeEventSource(pollTimer);
  }
  OSSafeReleaseNULL(pollTimer);
  if (irqKbd) irqKbd->disable();
  if (irqMouse) irqMouse->disable();
  if (gNodeKbd) devfs_remove(gNodeKbd);
  if (gNodeMouse) devfs_remove(gNodeMouse);
  if (gMajor >= 0) cdevsw_remove(gMajor, &gCdev);
  if (wl) {
    if (irqKbd) wl->removeEventSource(irqKbd);
    if (irqMouse) wl->removeEventSource(irqMouse);
  }
  OSSafeReleaseNULL(irqKbd);
  OSSafeReleaseNULL(irqMouse);
  OSSafeReleaseNULL(wl);
  IOService::stop(provider);
}

void ClokePS2::interrupt(IOInterruptEventSource *, int) {
  irqs++;
  drain(true);
}

void ClokePS2::pollTick(IOTimerEventSource *) {
  drain(false);
  if (++ticks == 3000) /* nach 30 s einmal festhalten, was ankam */
    LOG("after 30 s: %u interrupts, %u polled reads, %u keyboard and %u mouse bytes", irqs, polled, kbdBytes, mouseBytes);
  pollTimer->setTimeoutMS(10);
}

void ClokePS2::drain(bool fromIrq) {
  for (int i = 0; i < 64; i++) {
    uint8_t st = inb(kCmd);
    if (!(st & kOBF)) break;
    uint8_t b = inb(kData);
    if (!fromIrq) polled++;
    if (st & kAux) {
      if (mouseBytes++ < 4) LOG("mouse byte 0x%02x (%s)", b, fromIrq ? "interrupt" : "polled");
      mouseByte(b);
    } else {
      if (kbdBytes++ < 6) LOG("keyboard byte 0x%02x (%s)", b, fromIrq ? "interrupt" : "polled");
      kbdByte(b);
    }
  }
}

void ClokePS2::key(uint8_t usage, bool pressed) {
  if (!usage) return;
  bool was = down[usage >> 3] & (1 << (usage & 7));
  if (pressed == was) return; /* Wiederholung der Tastatur: Xorg wiederholt selbst */
  if (pressed) down[usage >> 3] |= (uint8_t)(1 << (usage & 7));
  else down[usage >> 3] &= (uint8_t)~(1 << (usage & 7));
  uint8_t rec[8] = {0, 0, 0, 0, usage, (uint8_t)pressed, 0, 0};
  gKbd.push(rec);
}

void ClokePS2::kbdByte(uint8_t b) {
  if (b == 0xfa || b == 0xfe || b == 0x00 || b == 0xff) return; /* ACK, Wiederholen, Fehler */
  if (e1) { /* Pause: E1 1D 45 E1 9D C5 */
    if (++e1 == 6) {
      e1 = 0;
      key(0x48, true);
      key(0x48, false);
    }
    return;
  }
  if (b == 0xe1) {
    e1 = 1;
    return;
  }
  if (b == 0xe0) {
    e0 = true;
    return;
  }
  bool pressed = !(b & 0x80);
  uint8_t code = b & 0x7f;
  uint8_t usage = e0 ? extUsage(code) : (code < sizeof kSet1 ? kSet1[code] : 0);
  e0 = false;
  key(usage, pressed);
}

void ClokePS2::mouseByte(uint8_t b) {
  if (npkt == 0 && !(b & 0x08)) return; /* erstes Byte hat immer Bit 3: sonst neu einrasten */
  pkt[npkt++] = b;
  if (npkt < (wheel ? 4 : 3)) return;
  npkt = 0;
  if (pkt[0] & 0xc0) return; /* Überlauf */
  int dx = pkt[1] - ((pkt[0] & 0x10) ? 256 : 0);
  int dy = pkt[2] - ((pkt[0] & 0x20) ? 256 : 0);
  int dz = wheel ? (int8_t)pkt[3] : 0;
  dy = -dy; /* PS/2: nach oben positiv */
  uint8_t buttons = pkt[0] & 0x07;
  do { /* in Schritten, die in ein Byte passen */
    int sx = dx > 127 ? 127 : dx < -127 ? -127 : dx;
    int sy = dy > 127 ? 127 : dy < -127 ? -127 : dy;
    uint8_t rec[12] = {0, 0, 0, 0, 0, buttons, (uint8_t)(int8_t)sx, (uint8_t)(int8_t)sy, (uint8_t)(int8_t)(-dz), 0, 0, 0};
    gMouse.push(rec);
    dx -= sx, dy -= sy, dz = 0;
  } while (dx || dy);
}
