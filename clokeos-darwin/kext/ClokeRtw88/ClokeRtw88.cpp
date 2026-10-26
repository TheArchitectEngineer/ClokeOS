/*
 * ClokeRtw88: WLAN-Chips von Realtek (RTL8822CE im HP 15s-fq5655ng), Stufe 1: erkennen und lesen.
 *
 * Der Treiber hängt sich an die PCI-Kennung, schaltet Speicher und Busmaster ein, bringt das Gerät in D0,
 * bildet BAR2 (die Register, wie rtw88 in Linux: pci.c, bar_id = 2) ab und schreibt Kennung, Versionsregister
 * und den PCIe-Verbindungsstatus ins Kernel-Log (dmesg, "ClokeRtw88:"), damit es das Diagnosefenster
 * (clokeos-diag) zeigt. Geschrieben wird nur das PCI-Befehlsregister und der Energiezustand, an den Chip-
 * Registern nichts. Nur mit Boot-Arg cloke_wifi=1 aktiv (2: zusätzlich aus- und einschalten, Stufe 2b). Weiter (Stufe 2: Einschalten, Firmware, Ringe) steht in NOTES.md.
 */
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOService.h>
#include <pexpert/pexpert.h>
#include "rtw8821c_pwr.h"

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

/* ---- Stufe 2a: eFuse lesen (rtw88 efuse.c, rtw8821c: kein efuse_grant) ---------------------------------
 * Das eFuse enthält die Kennungen und die MAC-Adresse. Die Zahlen müssen zu denen der PCI-Konfiguration
 * passen (10ec:c821, Subsystem 103c:884d); das beweist, dass Schreiben und Warten auf Register klappt, bevor
 * es an Einschalten und Firmware geht. Geschrieben werden nur REG_LDO_EFUSE_CTRL (Bank, 2,5-V-LDO) und
 * REG_EFUSE_CTRL; der Chip lief schon (die Firmware des Notebooks hat ihn eingeschaltet). */
enum { REG_EFUSE_CTRL = 0x30, REG_LDO_EFUSE_CTRL = 0x34, PHY_EFUSE_SIZE = 512, PROTECT_SIZE = 96 };

static void dump_efuse(volatile uint8_t *base) {
  auto rd32 = [&](unsigned o) { return *(volatile uint32_t *)(base + o); };
  auto wr32 = [&](unsigned o, uint32_t v) { *(volatile uint32_t *)(base + o) = v; };
  static uint8_t phy[PHY_EFUSE_SIZE], logm[PHY_EFUSE_SIZE];
  /* Bank WLAN (Bits 8,9 = 0), 2,5-V-LDO aus */
  wr32(REG_LDO_EFUSE_CTRL, rd32(REG_LDO_EFUSE_CTRL) & ~(3u << 8));
  base[REG_LDO_EFUSE_CTRL + 3] &= (uint8_t)~0x80;
  uint32_t ctl = rd32(REG_EFUSE_CTRL);
  for (unsigned a = 0; a < PHY_EFUSE_SIZE; a++) {
    ctl &= ~(0xffu | (0x3ffu << 8));
    ctl |= (a & 0x3ffu) << 8;
    wr32(REG_EFUSE_CTRL, ctl & ~(1u << 31));
    unsigned cnt = 20000;
    do {
      IODelay(5);
      ctl = rd32(REG_EFUSE_CTRL);
    } while (!(ctl & (1u << 31)) && --cnt);
    if (!cnt) {
      LOG("eFuse read timed out at byte %u (CTRL 0x%08x)", a, ctl);
      return;
    }
    phy[a] = (uint8_t)ctl;
  }
  memset(logm, 0xff, sizeof logm);
  unsigned p = 0, blocks = 0;
  bool bad = false;
  while (p < PHY_EFUSE_SIZE - PROTECT_SIZE) {
    uint8_t h1 = phy[p], h2 = phy[p + 1];
    if (h1 == 0xff || ((h1 & 0x1f) == 0xf && h2 == 0xff)) break;
    unsigned blk, wen;
    if ((h1 & 0x1f) == 0xf) {
      blk = ((h2 & 0xf0u) >> 1) | ((h1 >> 5) & 7u);
      wen = h2 & 0xfu;
      p += 2;
    } else {
      blk = (h1 & 0xf0u) >> 4;
      wen = h1 & 0xfu;
      p += 1;
    }
    for (int i = 0; i < 4; i++) {
      if (wen & (1u << i)) continue;
      unsigned li = (blk << 3) + ((unsigned)i << 1);
      if (p + 1 > PHY_EFUSE_SIZE - PROTECT_SIZE || li + 1 >= sizeof logm) {
        bad = true;
        break;
      }
      logm[li] = phy[p];
      logm[li + 1] = phy[p + 1];
      p += 2;
    }
    if (bad) break;
    blocks++;
  }
  LOG("eFuse: %u blocks decoded%s, physical bytes used %u", blocks, bad ? " (BAD)" : "", p);
  /* rtw8821ce_efuse ab 0xd0: mac_addr[6], vender_id[2], device_id[2], sub_vender_id[2], sub_device_id[2] */
  LOG("eFuse ids %02x%02x:%02x%02x subsystem %02x%02x:%02x%02x (little endian bytes, expect 10ec:c821, 103c:884d)",
      logm[0xd7], logm[0xd6], logm[0xd9], logm[0xd8], logm[0xdb], logm[0xda], logm[0xdd], logm[0xdc]);
  LOG("eFuse MAC %02x:%02x:%02x:xx:xx:xx", logm[0xd0], logm[0xd1], logm[0xd2]);
}

/* ---- Stufe 2b: aus- und wieder einschalten (rtw88 mac.c: rtw_mac_power_switch, rtw_pwr_seq_parser) -------
 * Nur mit cloke_wifi=2. Der Chip ist beim Start schon an (REG_CR != 0xea); wie Linux schaltet der Treiber ihn
 * dann mit der Ausschaltfolge aus und mit der Einschaltfolge wieder ein. Gelingt das (alle Abfragen kommen
 * rechtzeitig), ist der Weg zu Firmware und DMA frei. */
enum { REG_SYS_PW_CTRL = 0x04, BIT_PFM_WOWL = 1u << 3, REG_MCUFW_CTRL = 0x80, REG_CR = 0x100 };

static bool pwr_poll(volatile uint8_t *base, unsigned off, uint8_t mask, uint8_t target) {
  target &= mask;
  for (unsigned i = 0; i < 20000; i++) {
    if ((base[off] & mask) == target) return true;
    IODelay(50);
  }
  return false;
}

static bool pwr_seq(volatile uint8_t *base, const struct rtw_pwr_seq_cmd *const *seq, uint8_t cut_mask, const char *what) {
  unsigned n = 0;
  for (; *seq; seq++)
    for (const struct rtw_pwr_seq_cmd *c = *seq; c->cmd != RTW_PWR_CMD_END; c++) {
      if (!(c->intf_mask & RTW_PWR_INTF_PCI_MSK) || !(c->cut_mask & cut_mask)) continue;
      if (c->offset >= 0x1000) {
        LOG("%s: offset 0x%x outside the mapped registers, stop", what, c->offset);
        return false;
      }
      switch (c->cmd) {
        case RTW_PWR_CMD_WRITE: base[c->offset] = (uint8_t)((base[c->offset] & ~c->mask) | (c->value & c->mask)); break;
        case RTW_PWR_CMD_POLLING:
          if (!pwr_poll(base, c->offset, c->mask, c->value)) {
            /* wie rtw88 bei PCIe: BIT_PFM_WOWL einmal umschalten und noch einmal warten */
            uint8_t v = base[REG_SYS_PW_CTRL];
            base[REG_SYS_PW_CTRL] = v | BIT_PFM_WOWL;
            base[REG_SYS_PW_CTRL] = v & (uint8_t)~BIT_PFM_WOWL;
            if (!pwr_poll(base, c->offset, c->mask, c->value)) {
              LOG("%s: poll 0x%x mask 0x%02x value 0x%02x failed (reads 0x%02x)", what, c->offset, c->mask, c->value,
                  base[c->offset]);
              return false;
            }
          }
          break;
        case RTW_PWR_CMD_DELAY:
          if (c->value == RTW_PWR_DELAY_US) IODelay(c->offset);
          else IOSleep(c->offset);
          break;
        default: break;
      }
      n++;
    }
  LOG("%s: %u steps done", what, n);
  return true;
}

static void power_cycle(volatile uint8_t *base, unsigned cut) {
  uint8_t cut_mask = (uint8_t)(1u << (cut + 1));
  LOG("power: REG_CR 0x%02x MCUFW_CTRL 0x%04x SYS_PW_CTRL 0x%08x", base[REG_CR], *(volatile uint16_t *)(base + REG_MCUFW_CTRL),
      *(volatile uint32_t *)(base + REG_SYS_PW_CTRL));
  if (base[REG_CR] != 0xea) {
    if (!pwr_seq(base, card_disable_flow_8821c, cut_mask, "power off")) return;
    LOG("power: after off REG_CR 0x%02x SYS_PW_CTRL 0x%08x", base[REG_CR], *(volatile uint32_t *)(base + REG_SYS_PW_CTRL));
  }
  if (!pwr_seq(base, card_enable_flow_8821c, cut_mask, "power on")) return;
  LOG("power: after on REG_CR 0x%02x SYS_FUNC_EN 0x%04x SYS_PW_CTRL 0x%08x", base[REG_CR],
      *(volatile uint16_t *)(base + 2), *(volatile uint32_t *)(base + REG_SYS_PW_CTRL));
}

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
  /* Standardmäßig aus: Stufe 1 schreibt ins PCI-Befehlsregister und in den Energiezustand des Geräts, und
   * auf dem HP fiel mit ihr im Kernel das Trackpad (ClokeI2CHID) aus. Nur mit Boot-Arg cloke_wifi=1. */
  uint32_t on = 0;
  if (!PE_parse_boot_argn("cloke_wifi", &on, sizeof on) || !on) return false;
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
  if (cfg1 == 0xffffffffu) {
    LOG("registers read as all ones: chip powered off or link down");
    return true;
  }
  dump_efuse(base);
  if (on >= 2) power_cycle(base, (cfg1 >> 12) & 0xf);
  return true;
}

void ClokeRtw88::stop(IOService *provider) {
  OSSafeReleaseNULL(map);
  IOService::stop(provider);
}
