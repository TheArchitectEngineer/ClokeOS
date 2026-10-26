/*
 * ClokeRtw88: WLAN-Chips von Realtek (RTL8822CE im HP 15s-fq5655ng), Stufe 1: erkennen und lesen.
 *
 * Der Treiber hängt sich an die PCI-Kennung, schaltet Speicher und Busmaster ein, bringt das Gerät in D0,
 * bildet BAR2 (die Register, wie rtw88 in Linux: pci.c, bar_id = 2) ab und schreibt Kennung, Versionsregister
 * und den PCIe-Verbindungsstatus ins Kernel-Log (dmesg, "ClokeRtw88:"), damit es das Diagnosefenster
 * (clokeos-diag) zeigt. Geschrieben wird nur das PCI-Befehlsregister und der Energiezustand, an den Chip-
 * Registern nichts. Weiter (Stufe 2: Einschalten, Firmware, Ringe) steht in NOTES.md.
 */
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOService.h>

#define LOG(fmt, ...) IOLog("ClokeRtw88: " fmt "\n", ##__VA_ARGS__)

/* wie in ClokeI2CHID: nur die Konfigurationszugriffe, die kc-builder gegen den Kernel bindet */
class IOPCIDevice : public IOService {
public:
  UInt8 extendedConfigRead8(IOByteCount offset);
  UInt16 extendedConfigRead16(IOByteCount offset);
  UInt32 extendedConfigRead32(IOByteCount offset);
  void extendedConfigWrite16(IOByteCount offset, UInt16 data);
};

enum { REG_SYS_ISO_CTRL = 0x00, REG_SYS_FUNC_EN = 0x02, REG_SYS_CLK_CTRL = 0x08, REG_SYS_CFG1 = 0xF0, REG_SYS_CFG2 = 0xFC };

class ClokeRtw88 : public IOService {
  OSDeclareDefaultStructors(ClokeRtw88)
  IOMemoryMap *map = nullptr;
  IOPCIDevice *pci = nullptr;

public:
  bool start(IOService *provider) override;
  void stop(IOService *provider) override;
};
OSDefineMetaClassAndStructors(ClokeRtw88, IOService)

bool ClokeRtw88::start(IOService *provider) {
  if (!IOService::start(provider)) return false;
  pci = (IOPCIDevice *)provider;
  uint16_t vendor = pci->extendedConfigRead16(0), device = pci->extendedConfigRead16(2);
  uint32_t cls = pci->extendedConfigRead32(8), sub = pci->extendedConfigRead32(0x2c);
  LOG("%04x:%04x rev 0x%02x class 0x%06x subsystem %04x:%04x", vendor, device, cls & 0xff, cls >> 8, sub & 0xffff, sub >> 16);

  pci->extendedConfigWrite16(4, (uint16_t)(pci->extendedConfigRead16(4) | 0x0006)); /* Speicher + Busmaster */
  for (uint8_t cap = pci->extendedConfigRead8(0x34), n = 0; cap && n < 32; cap = pci->extendedConfigRead8(cap + 1u), n++) {
    uint8_t id = pci->extendedConfigRead8(cap);
    if (id == 0x01) { /* Power Management: D0 */
      uint16_t pmcsr = pci->extendedConfigRead16(cap + 4u);
      LOG("power state D%u", pmcsr & 3);
      if (pmcsr & 3) {
        pci->extendedConfigWrite16(cap + 4u, (uint16_t)(pmcsr & ~3u));
        IOSleep(10);
      }
    } else if (id == 0x10) { /* PCI Express: Verbindungsstatus */
      uint16_t ls = pci->extendedConfigRead16(cap + 0x12u);
      LOG("PCIe link: speed gen%u, width x%u", ls & 0xf, (ls >> 4) & 0x3f);
    }
  }

  uint32_t bar2 = pci->extendedConfigRead32(0x18);
  uint64_t phys = bar2 & ~0xfull;
  if ((bar2 & 6) == 4) phys |= (uint64_t)pci->extendedConfigRead32(0x1c) << 32;
  LOG("BAR2 config 0x%08x -> physical 0x%llx", bar2, (unsigned long long)phys);
  if ((bar2 & 1) || !phys || phys == 0xfffffff0ull) {
    LOG("BAR2 unusable (I/O BAR or unassigned by the firmware)");
    return true;
  }
  IOMemoryDescriptor *md = IOMemoryDescriptor::withPhysicalAddress(phys, 0x1000, kIODirectionNone);
  map = md ? md->map(kIOMapInhibitCache) : nullptr;
  OSSafeReleaseNULL(md);
  if (!map || map->getLength() < 0x1000) {
    LOG("could not map BAR2");
    OSSafeReleaseNULL(map);
    return true;
  }
  volatile uint8_t *base = (volatile uint8_t *)map->getVirtualAddress();
  auto rd32 = [&](unsigned off) { return *(volatile uint32_t *)(base + off); };
  uint32_t cfg1 = rd32(REG_SYS_CFG1), cfg2 = rd32(REG_SYS_CFG2);
  LOG("SYS_ISO_CTRL 0x%04x SYS_FUNC_EN 0x%04x SYS_CLK_CTRL 0x%08x", rd32(REG_SYS_ISO_CTRL) & 0xffff, (rd32(REG_SYS_ISO_CTRL) >> 16) & 0xffff,
      rd32(REG_SYS_CLK_CTRL));
  LOG("SYS_CFG1 0x%08x (cut %u, chip id bits 0x%02x) SYS_CFG2 0x%08x", cfg1, (cfg1 >> 12) & 0xf, (cfg1 >> 24) & 0xff, cfg2);
  if (cfg1 == 0xffffffffu) LOG("registers read as all ones: chip powered off or link down");
  return true;
}

void ClokeRtw88::stop(IOService *provider) {
  OSSafeReleaseNULL(map);
  IOService::stop(provider);
}
