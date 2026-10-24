/*
 * ClokePayload: die Installationsabbilder, die der Loader in reservierten Speicher gelegt hat
 * (loader/xnu-loader.patch, clokeos_load_payload), als Zeichengerät /dev/clokeos_payload.
 *
 * Der USB-Treiber für Massenspeicher von PureDarwin gibt auf echter Hardware nach einigen Dutzend MB
 * mit E/A-Fehlern auf; die Firmware liest den Stick dagegen zuverlässig. Der Loader kopiert deshalb
 * Beschreibung und Abbilder in Speicher, den XNU nicht anfasst (EfiReservedMemoryType), und nennt ihn im
 * Gerätebaum unter /chosen/memory-map/ClokePayload (Adresse und Länge, je 64 Bit). Dieser Treiber liest
 * den Bereich über einen IOMemoryDescriptor und reicht ihn an read() weiter; pread geht (uio_offset).
 * Kopf des Bereichs: Textzeilen "CLOKEOS-PAYLOAD 1", dann "<Name> <Offset> <Größe>" je Datei.
 */
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IORegistryEntry.h>
#include <IOKit/IOService.h>

extern "C" {
#include <miscfs/devfs/devfs.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <sys/vnode.h>
}

#define LOG(fmt, ...) IOLog("ClokePayload: " fmt "\n", ##__VA_ARGS__)

static IOMemoryDescriptor *gMd;
static uint64_t gSize;
static int gMajor = -1;
static void *gNode;

static int pl_open(dev_t, int flags, int, proc_t) { return (flags & FWRITE) ? EROFS : 0; } /* nur lesen; O_RDONLY kommt als FREAD an */
static int pl_close(dev_t, int, int, proc_t) { return 0; }

static int pl_read(dev_t, struct uio *uio, int) {
  static uint8_t buf[65536]; /* ein Leser zugleich genügt: der Installer */
  while (uio_resid(uio) > 0) {
    uint64_t off = (uint64_t)uio_offset(uio);
    if (off >= gSize) return 0;
    uint64_t n = (uint64_t)uio_resid(uio);
    if (n > sizeof buf) n = sizeof buf;
    if (n > gSize - off) n = gSize - off;
    if (gMd->readBytes(off, buf, n) != n) return EIO;
    int err = uiomove((char *)buf, (int)n, uio);
    if (err) return err;
  }
  return 0;
}
static int pl_write(dev_t, struct uio *, int) { return EROFS; }
static int pl_ioctl(dev_t, u_long, caddr_t, int, proc_t) { return ENOTTY; }
static int pl_stop(struct tty *, int) { return 0; }
static int pl_reset(int) { return 0; }
static int pl_select(dev_t, int which, void *, proc_t) { return which == FREAD ? 1 : 0; }
static void pl_strategy(struct buf *) {}
static struct cdevsw gCdev = {pl_open,   pl_close, pl_read, pl_write,    pl_ioctl, pl_stop, pl_reset,
                              nullptr,   pl_select, nullptr, pl_strategy, nullptr,  nullptr, 0};

class ClokePayload : public IOService {
  OSDeclareDefaultStructors(ClokePayload)
public:
  bool start(IOService *provider) override;
};

OSDefineMetaClassAndStructors(ClokePayload, IOService)

bool ClokePayload::start(IOService *provider) {
  if (!IOService::start(provider)) return false;
  IORegistryEntry *mm = IORegistryEntry::fromPath("/chosen/memory-map", gIODTPlane);
  OSData *d = mm ? OSDynamicCast(OSData, mm->getProperty("ClokePayload")) : nullptr;
  if (!d || d->getLength() < 16) {
    if (mm) mm->release();
    LOG("none (the loader did not stage the install images)");
    return false;
  }
  uint64_t phys = ((const uint64_t *)d->getBytesNoCopy())[0], len = ((const uint64_t *)d->getBytesNoCopy())[1];
  mm->release();
  gMd = IOMemoryDescriptor::withPhysicalAddress(phys, len, kIODirectionIn);
  if (!gMd) {
    LOG("cannot describe 0x%llx + 0x%llx", (unsigned long long)phys, (unsigned long long)len);
    return false;
  }
  gSize = len;
  uint8_t hdr[64] = {};
  gMd->readBytes(0, hdr, sizeof hdr - 1);
  gMajor = cdevsw_add(-1, &gCdev);
  if (gMajor >= 0) gNode = devfs_make_node(makedev((uint32_t)gMajor, 0), DEVFS_CHAR, 0, 0, 0444, "clokeos_payload");
  LOG("%llu MB at 0x%llx, header \"%.17s\", /dev/clokeos_payload %s", (unsigned long long)(len >> 20),
      (unsigned long long)phys, (const char *)hdr, gNode ? "ready" : "failed");
  registerService();
  return gNode != nullptr;
}
