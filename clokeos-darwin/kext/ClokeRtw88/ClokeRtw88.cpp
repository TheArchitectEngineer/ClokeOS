/*
 * ClokeRtw88: WLAN-Chips von Realtek (RTL8822CE im HP 15s-fq5655ng), Stufe 1: erkennen und lesen.
 *
 * Der Treiber hängt sich an die PCI-Kennung, schaltet Speicher und Busmaster ein, bringt das Gerät in D0,
 * bildet BAR2 (die Register, wie rtw88 in Linux: pci.c, bar_id = 2) ab und schreibt Kennung, Versionsregister
 * und den PCIe-Verbindungsstatus ins Kernel-Log (dmesg, "ClokeRtw88:"), damit es das Diagnosefenster
 * (clokeos-diag) zeigt. Geschrieben wird nur das PCI-Befehlsregister und der Energiezustand, an den Chip-
 * Registern nichts. Nur mit Boot-Arg cloke_wifi=1 aktiv (2: zusätzlich aus- und einschalten, Stufe 2b; 3: dazu die Firmware laden, Stufe 3; 4: dazu MAC einrichten und ein Befehl an die Firmware, Stufe 4). Weiter (Stufe 2: Einschalten, Firmware, Ringe) steht in NOTES.md.
 */
#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOService.h>
#include <pexpert/pexpert.h>
#include "rtw8821c_pwr.h"
#include "rtw8821c_fw.h"
#include "rtw88_regs.h"
#include "rtw8821c_table.h"

#define LOG(fmt, ...) IOLog("ClokeRtw88: " fmt "\n", ##__VA_ARGS__)

/* wie in ClokeI2CHID: nur die Konfigurationszugriffe, die kc-builder gegen den Kernel bindet */
class IOPCIDevice : public IOService {
public:
  UInt8 extendedConfigRead8(IOByteCount offset);
  UInt16 extendedConfigRead16(IOByteCount offset);
  UInt32 extendedConfigRead32(IOByteCount offset);
  void extendedConfigWrite16(IOByteCount offset, UInt16 data);
};

enum { REG_SYS_ISO_CTRL = 0x00, REG_SYS_CFG1 = 0xF0, REG_SYS_CFG2 = 0xFC };

/* ---- Stufe 2a: eFuse lesen (rtw88 efuse.c, rtw8821c: kein efuse_grant) ---------------------------------
 * Das eFuse enthält die Kennungen und die MAC-Adresse. Die Zahlen müssen zu denen der PCI-Konfiguration
 * passen (10ec:c821, Subsystem 103c:884d); das beweist, dass Schreiben und Warten auf Register klappt, bevor
 * es an Einschalten und Firmware geht. Geschrieben werden nur REG_LDO_EFUSE_CTRL (Bank, 2,5-V-LDO) und
 * REG_EFUSE_CTRL; der Chip lief schon (die Firmware des Notebooks hat ihn eingeschaltet). */
enum { REG_EFUSE_CTRL = 0x30, REG_LDO_EFUSE_CTRL = 0x34, PHY_EFUSE_SIZE = 512, PROTECT_SIZE = 96 };

static uint8_t g_logm[512]; /* logische eFuse-Karte, von dump_efuse gefüllt */

static void dump_efuse(volatile uint8_t *base) {
  auto rd32 = [&](unsigned o) { return *(volatile uint32_t *)(base + o); };
  auto wr32 = [&](unsigned o, uint32_t v) { *(volatile uint32_t *)(base + o) = v; };
  static uint8_t phy[PHY_EFUSE_SIZE];
  uint8_t *logm = g_logm;
  base[0xCF] = 0x69; /* EFUSE_ACCESS_ON (rtw_chip_efuse_grant_on) */
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
  base[0xCF] = 0;
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
enum { REG_WINDOW = 0x10000 }; /* BAR2: 64 KB Register */

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

static bool power_cycle(volatile uint8_t *base, unsigned cut) {
  uint8_t cut_mask = (uint8_t)(1u << (cut + 1));
  LOG("power: REG_CR 0x%02x MCUFW_CTRL 0x%04x SYS_PW_CTRL 0x%08x", base[REG_CR], *(volatile uint16_t *)(base + REG_MCUFW_CTRL),
      *(volatile uint32_t *)(base + REG_SYS_PW_CTRL));
  if (base[REG_CR] != 0xea) {
    if (!pwr_seq(base, card_disable_flow_8821c, cut_mask, "power off")) return false;
    LOG("power: after off REG_CR 0x%02x SYS_PW_CTRL 0x%08x", base[REG_CR], *(volatile uint32_t *)(base + REG_SYS_PW_CTRL));
  }
  /* rtw_mac_pre_system_cfg (PCIe): REG_RSV_CTRL = 0, BIT_USB_SUS_DIS in REG_HCI_OPT_CTRL */
  base[0x1c] = 0;
  *(volatile uint32_t *)(base + 0x74) |= 1u << 8;
  if (!pwr_seq(base, card_enable_flow_8821c, cut_mask, "power on")) return false;
  LOG("power: after on REG_CR 0x%02x SYS_FUNC_EN 0x%04x SYS_PW_CTRL 0x%08x", base[REG_CR],
      *(volatile uint16_t *)(base + 2), *(volatile uint32_t *)(base + REG_SYS_PW_CTRL));
  return true;
}

/* ---- Stufe 3: Firmware laden (rtw88 mac.c: __rtw_mac_init_system_cfg, __rtw_download_firmware; fw.c:
 * rtw_fw_write_data_rsvd_page; pci.c: rtw_pci_write_data_rsvd_page). Nur mit cloke_wifi=3.
 * Jedes Stück (höchstens 4 KB) geht über die Beacon-Warteschlange per DMA in die reservierte Seite 0 des
 * TX-FIFO des Chips, von dort kopiert der DDMA-Kanal 0 es an seine Stelle im Speicher des WLAN-Prozessors.
 * Der Chip liest dabei nur aus dem Hauptspeicher (Senderichtung), schreibt nicht hinein. */
struct Dma {
  IOBufferMemoryDescriptor *md = nullptr;
  uint8_t *va = nullptr;
  uint32_t pa = 0;
  size_t contig = 0; /* über IOMallocContiguous geholt: Größe zum Freigeben */
};

/* PureDarwins IOKit hat nicht jeden Weg zu physisch zusammenhängendem Speicher; der erste HP-Test scheiterte
 * hier. Deshalb drei Wege nacheinander, jeder mit Meldung, woran er scheitert. */
static bool dma_try_md(Dma *d, IOBufferMemoryDescriptor *md, size_t size, const char *how) {
  if (!md) {
    LOG("dma %s: no buffer", how);
    return false;
  }
  IOReturn r = md->prepare();
  if (r != kIOReturnSuccess) {
    LOG("dma %s: prepare 0x%x", how, r);
    md->release();
    return false;
  }
  IOByteCount len = 0;
  addr64_t pa = md->getPhysicalSegment(0, &len, kIOMemoryMapperNone);
  if (!pa) pa = md->getPhysicalSegment(0, &len, 0);
  uint8_t *va = (uint8_t *)md->getBytesNoCopy();
  if (!va || !pa || (pa >> 32) || len < size) {
    LOG("dma %s: va %p pa 0x%llx len %llu", how, va, (unsigned long long)pa, (unsigned long long)len);
    md->complete();
    md->release();
    return false;
  }
  d->md = md;
  d->va = va;
  d->pa = (uint32_t)pa;
  memset(va, 0, size);
  LOG("dma %s: %zu bytes at 0x%08x", how, size, d->pa);
  return true;
}

#pragma clang diagnostic ignored "-Wdeprecated-declarations" /* IOMallocContiguous: der dritte Weg */
static bool dma_alloc(Dma *d, size_t size) {
  *d = Dma();
  if (dma_try_md(d, IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task, kIODirectionInOut | kIOMemoryPhysicallyContiguous,
                                                                    size, 0x00000000fffff000ull), size, "physmask"))
    return true;
  if (dma_try_md(d, IOBufferMemoryDescriptor::withOptions(kIODirectionInOut | kIOMemoryPhysicallyContiguous, size, 4096), size,
                 "contiguous"))
    return true;
  IOPhysicalAddress pa = 0;
  void *va = IOMallocContiguous(size, 4096, &pa);
  if (va && pa && !((uint64_t)pa >> 32)) {
    d->va = (uint8_t *)va;
    d->pa = (uint32_t)pa;
    d->contig = size;
    memset(va, 0, size);
    LOG("dma IOMallocContiguous: %zu bytes at 0x%08x", size, d->pa);
    return true;
  }
  LOG("dma IOMallocContiguous: va %p pa 0x%llx", va, (unsigned long long)pa);
  if (va) IOFreeContiguous(va, size);
  return false;
}

static void dma_free(Dma *d) {
  if (d->md) {
    d->md->complete();
    d->md->release();
  } else if (d->contig && d->va) {
    IOFreeContiguous(d->va, d->contig);
  }
  *d = Dma();
}

static inline uint8_t r8(volatile uint8_t *b, unsigned o) { return b[o]; }
static inline uint16_t r16(volatile uint8_t *b, unsigned o) { return *(volatile uint16_t *)(b + o); }
static inline uint32_t r32(volatile uint8_t *b, unsigned o) { return *(volatile uint32_t *)(b + o); }
static inline void w8(volatile uint8_t *b, unsigned o, uint8_t v) { b[o] = v; }
static inline void w16(volatile uint8_t *b, unsigned o, uint16_t v) { *(volatile uint16_t *)(b + o) = v; }
static inline void w32(volatile uint8_t *b, unsigned o, uint32_t v) { *(volatile uint32_t *)(b + o) = v; }

/* check_hw_ready (rtw88 util.c): (Wert & Maske) >> Lage der Maske == Ziel, höchstens 1000 x 10 µs */
static bool hw_ready(volatile uint8_t *b, unsigned addr, uint32_t mask, uint32_t target) {
  unsigned shift = (unsigned)__builtin_ctz(mask);
  for (unsigned i = 0; i < 1000; i++) {
    if (((r32(b, addr) & mask) >> shift) == target) return true;
    IODelay(10);
  }
  return false;
}

enum {
  PCI_CTRL = 0x300, PCI_TXBD_DESA_BCNQ = 0x308, PCI_TXBD_BCN_WORK = 0x383, LTE_CTRL = 0x1700, LTE_WDATA = 0x1704,
  LTE_RDATA = 0x1708, TX_DESC = 48, FW_HDR = 64, FW_CHKSUM = 8,
};

static bool lte_read(volatile uint8_t *b, unsigned off, uint32_t *v) {
  if (!hw_ready(b, LTE_CTRL, 1u << 29, 1)) return false;
  w32(b, LTE_CTRL, 0x800F0000u | off);
  *v = r32(b, LTE_RDATA);
  return true;
}

static bool lte_write(volatile uint8_t *b, unsigned off, uint32_t v) {
  if (!hw_ready(b, LTE_CTRL, 1u << 29, 1)) return false;
  w32(b, LTE_WDATA, v);
  w32(b, LTE_CTRL, 0xC00F0000u | off);
  return true;
}

/* ein Stück in die reservierte Seite 0 (rtw_fw_write_data_rsvd_page + rtw_pci_write_data_rsvd_page) */
static bool write_rsvd_page(volatile uint8_t *b, Dma *ring, Dma *buf, const uint8_t *data, uint32_t size) {
  uint8_t bcn_ctrl = r8(b, REG_BCN_CTRL);
  w16(b, REG_FIFOPAGE_CTRL_2, (uint16_t)(0 | (1u << 15)));     /* Seite 0, BIT_BCN_VALID_V1 löschen */
  uint8_t cr1 = r8(b, REG_CR + 1);
  w8(b, REG_CR + 1, cr1 | 1);                                    /* BIT_ENSWBCN >> 8 */
  w8(b, REG_BCN_CTRL, (uint8_t)((bcn_ctrl & ~(1u << 3)) | (1u << 4)));
  uint8_t txq = r8(b, REG_FWHW_TXQ_CTRL + 2);
  w8(b, REG_FWHW_TXQ_CTRL + 2, txq & (uint8_t)~(1u << 6));      /* ~BIT_EN_BCNQ_DL >> 16 */

  /* TX-Beschreibung (48 Byte) in der Seite des Rings ab 0x100, die Daten in einer eigenen Seite: PureDarwin gibt
   * zusammenhängenden Speicher unter 4 GB nur seitenweise her (4 KB + 48 Byte scheiterten auf dem HP), und die
   * Pufferbeschreibung hat ohnehin zwei Teile mit eigener Adresse */
  uint8_t *desc = ring->va + 0x100;
  memset(desc, 0, TX_DESC);
  const uint8_t *a1 = data + 4; /* rtw88 deutet die Daten als 802.11-Kopf: BMC aus addr1 */
  bool bmc = (a1[0] & 1) != 0;
  uint32_t w0 = (size & 0xffff) | ((uint32_t)TX_DESC << 16) | ((bmc ? 1u : 0u) << 24) | (1u << 26);
  uint32_t w1 = 16u << 8; /* QSEL Beacon */
  memcpy(desc, &w0, 4);
  memcpy(desc + 4, &w1, 4);
  memcpy(buf->va, data, size);
  /* Pufferbeschreibung: zwei Teile, Beacon-Warteschlange mit OWN */
  uint16_t *bd16 = (uint16_t *)ring->va;
  uint32_t *bd32 = (uint32_t *)ring->va;
  memset(ring->va, 0, 16);
  bd16[0] = TX_DESC;                                                       /* buf_size */
  bd16[1] = (uint16_t)((((TX_DESC + size) - 1) / 128 + 1) | (1u << 15));   /* psb_len | OWN */
  bd32[1] = ring->pa + 0x100;
  bd16[4] = (uint16_t)size;
  bd32[3] = buf->pa;
  __asm__ volatile("mfence" ::: "memory");
  w8(b, PCI_TXBD_BCN_WORK, r8(b, PCI_TXBD_BCN_WORK) | (1u << 4));
  bool ok = hw_ready(b, REG_FIFOPAGE_CTRL_2, 1u << 15, 1);

  w16(b, REG_FIFOPAGE_CTRL_2, (uint16_t)(0 | (1u << 15)));     /* rsvd_boundary (noch 0) */
  w8(b, REG_BCN_CTRL, bcn_ctrl);
  w8(b, REG_FWHW_TXQ_CTRL + 2, txq);
  w8(b, REG_CR + 1, cr1);
  return ok;
}

static bool to_mem(volatile uint8_t *b, Dma *ring, Dma *buf, const uint8_t *data, uint32_t dst, uint32_t size, const char *what) {
  w32(b, REG_DDMA_CH0CTRL, r32(b, REG_DDMA_CH0CTRL) | BIT_DDMACH0_RESET_CHKSUM_STS);
  uint32_t off = 0;
  bool first = true;
  while (off < size) {
    uint32_t n = size - off < 0x1000 ? size - off : 0x1000;
    if (!write_rsvd_page(b, ring, buf, data + off, n)) {
      LOG("fw %s: reserved page at +0x%x did not become valid (FIFOPAGE_CTRL_2 0x%04x)", what, off, r16(b, REG_FIFOPAGE_CTRL_2));
      return false;
    }
    if (!hw_ready(b, REG_DDMA_CH0CTRL, BIT_DDMACH0_OWN, 0)) {
      LOG("fw %s: DDMA busy before +0x%x", what, off);
      return false;
    }
    uint32_t ctrl = BIT_DDMACH0_CHKSUM_EN | BIT_DDMACH0_OWN | (n & 0x3ffff) | (first ? 0 : BIT_DDMACH0_CHKSUM_CONT);
    w32(b, REG_DDMA_CH0SA, OCPBASE_TXBUF_88XX + TX_DESC);
    w32(b, REG_DDMA_CH0DA, dst + off);
    w32(b, REG_DDMA_CH0CTRL, ctrl);
    if (!hw_ready(b, REG_DDMA_CH0CTRL, BIT_DDMACH0_OWN, 0)) {
      LOG("fw %s: DDMA did not finish at +0x%x (CTRL 0x%08x)", what, off, r32(b, REG_DDMA_CH0CTRL));
      return false;
    }
    first = false;
    off += n;
  }
  /* check_fw_checksum */
  uint8_t fw = r8(b, REG_MCUFW_CTRL);
  bool bad = (r32(b, REG_DDMA_CH0CTRL) & BIT_DDMACH0_CHKSUM_STS) != 0;
  bool imem = dst < OCPBASE_DMEM_88XX;
  if (bad) fw = (uint8_t)((fw | (imem ? 1u << 3 : 1u << 5)) & ~(imem ? 1u << 4 : 1u << 6));
  else fw |= imem ? (1u << 3) | (1u << 4) : (1u << 5) | (1u << 6);
  w8(b, REG_MCUFW_CTRL, fw);
  LOG("fw %s: %u bytes to 0x%08x, checksum %s", what, size, dst, bad ? "WRONG" : "ok");
  return !bad;
}

static bool download_fw(volatile uint8_t *b) {
  const uint8_t *fw = rtw8821c_fw;
  uint32_t dmem_addr, dmem, imem, emem, emem_addr, imem_addr;
  memcpy(&dmem_addr, fw + 0x20, 4);
  memcpy(&dmem, fw + 0x24, 4);
  memcpy(&imem, fw + 0x30, 4);
  memcpy(&emem, fw + 0x34, 4);
  memcpy(&emem_addr, fw + 0x38, 4);
  memcpy(&imem_addr, fw + 0x3c, 4);
  if (!(fw[0x18] & (1u << 4))) emem = 0;
  dmem += FW_CHKSUM;
  imem += FW_CHKSUM;
  if (emem) emem += FW_CHKSUM;
  if (FW_HDR + dmem + imem + emem != sizeof rtw8821c_fw) {
    LOG("fw: size check failed");
    return false;
  }
  LOG("fw: version %u.%u, dmem %u imem %u emem %u", fw[4] | fw[5] << 8, fw[6], dmem, imem, emem);

  Dma ring, buf;
  if (!dma_alloc(&ring, 4096) || !dma_alloc(&buf, 4096)) {
    LOG("fw: no DMA memory below 4 GB");
    dma_free(&ring);
    dma_free(&buf);
    return false;
  }
  /* rtw_pci_setup (nur die Beacon-Warteschlange) und DMA-Reset */
  w8(b, PCI_CTRL + 3, r8(b, PCI_CTRL + 3) | 0xf7);
  w32(b, PCI_TXBD_DESA_BCNQ, ring.pa);
  w32(b, PCI_CTRL, r32(b, PCI_CTRL) | (1u << 20) | (1u << 15));

  /* __rtw_mac_init_system_cfg */
  w32(b, REG_CPU_DMEM_CON, r32(b, REG_CPU_DMEM_CON) | (1u << 16) | (1u << 8));
  w8(b, REG_SYS_FUNC_EN + 1, r8(b, REG_SYS_FUNC_EN + 1) | 0xD8);
  w8(b, REG_CR_EXT + 3, (uint8_t)((r8(b, REG_CR_EXT + 3) & 0xF0) | 0x0C));
  uint32_t t = r32(b, REG_MCUFW_CTRL);
  if (t & (1u << 20)) {
    w32(b, REG_MCUFW_CTRL, t & ~(1u << 20));
    w32(b, REG_GPIO_MUXCFG, r32(b, REG_GPIO_MUXCFG) & ~(1u << 19));
  }

  uint32_t lte = 0;
  bool have_lte = lte_read(b, 0x38, &lte);
  /* wlan_cpu_enable(false) */
  w8(b, REG_SYS_FUNC_EN + 1, r8(b, REG_SYS_FUNC_EN + 1) & (uint8_t)~(1u << 2));
  w8(b, REG_RSV_CTRL + 1, r8(b, REG_RSV_CTRL + 1) & (uint8_t)~1u);
  /* download_firmware_reg_backup */
  uint8_t s_pq = r8(b, REG_TXDMA_PQ_MAP + 1), s_cr = r8(b, REG_CR), s_bcn = r8(b, REG_BCN_CTRL);
  uint16_t s_fifo = r16(b, REG_FIFOPAGE_INFO_1);
  uint32_t s_rqpn = r32(b, REG_RQPN_CTRL_2) | (1u << 31);
  w8(b, REG_TXDMA_PQ_MAP + 1, 3u << 6);
  w8(b, REG_CR, (1u << 0) | (1u << 2));
  w32(b, REG_H2CQ_CSR, 1u << 31);
  w16(b, REG_FIFOPAGE_INFO_1, 0x200);
  w32(b, REG_RQPN_CTRL_2, s_rqpn);
  w8(b, REG_BCN_CTRL, (uint8_t)((s_bcn & ~(1u << 3)) | (1u << 4)));
  /* download_firmware_reset_platform */
  w8(b, REG_CPU_DMEM_CON + 2, r8(b, REG_CPU_DMEM_CON + 2) & (uint8_t)~1u);
  w8(b, 0x08 + 1, r8(b, 0x08 + 1) & (uint8_t)~(1u << 6));
  w8(b, REG_CPU_DMEM_CON + 2, r8(b, REG_CPU_DMEM_CON + 2) | 1u);
  w8(b, 0x08 + 1, r8(b, 0x08 + 1) | (1u << 6));

  /* start_download_firmware */
  w16(b, REG_MCUFW_CTRL, (uint16_t)((r16(b, REG_MCUFW_CTRL) & 0x3800) | 1u));
  bool running = false;
  bool ok = to_mem(b, &ring, &buf, fw + FW_HDR, dmem_addr & ~(1u << 31), dmem, "dmem") &&
            to_mem(b, &ring, &buf, fw + FW_HDR + dmem, imem_addr & ~(1u << 31), imem, "imem") &&
            (!emem || to_mem(b, &ring, &buf, fw + FW_HDR + dmem + imem, emem_addr & ~(1u << 31), emem, "emem"));
  if (ok) {
    /* reg_restore */
    w8(b, REG_TXDMA_PQ_MAP + 1, s_pq);
    w8(b, REG_CR, s_cr);
    w32(b, REG_H2CQ_CSR, 1u << 31);
    w16(b, REG_FIFOPAGE_INFO_1, s_fifo);
    w32(b, REG_RQPN_CTRL_2, s_rqpn);
    w8(b, REG_BCN_CTRL, s_bcn);
    /* download_firmware_end_flow */
    w32(b, REG_TXDMA_STATUS, 1u << 2);
    uint16_t c = r16(b, REG_MCUFW_CTRL);
    if ((c & ((1u << 4) | (1u << 6))) == ((1u << 4) | (1u << 6))) w16(b, REG_MCUFW_CTRL, (uint16_t)((c | (1u << 14)) & ~1u));
    /* wlan_cpu_enable(true) */
    w8(b, REG_RSV_CTRL + 1, r8(b, REG_RSV_CTRL + 1) | 1u);
    w8(b, REG_SYS_FUNC_EN + 1, r8(b, REG_SYS_FUNC_EN + 1) | (1u << 2));
    if (have_lte) lte_write(b, 0x38, lte);
    bool ready = (r32(b, REG_MCUFW_CTRL) & FW_READY_MASK) == FW_READY;
    for (int i = 0; !ready && i < 50; i++) { /* großzügiger als rtw88: bis 0,5 s */
      IOSleep(10);
      ready = (r32(b, REG_MCUFW_CTRL) & FW_READY_MASK) == FW_READY;
    }
    LOG("fw: %s (MCUFW_CTRL 0x%04x, FW_DBG7 0x%08x)", ready ? "FIRMWARE RUNNING" : "firmware not ready", r16(b, REG_MCUFW_CTRL),
        r32(b, REG_FW_DBG7));
    running = ready;
  } else {
    w8(b, REG_MCUFW_CTRL, r8(b, REG_MCUFW_CTRL) & (uint8_t)~1u);
    w8(b, REG_SYS_FUNC_EN + 1, r8(b, REG_SYS_FUNC_EN + 1) | (1u << 2));
    LOG("fw: download failed (MCUFW_CTRL 0x%04x)", r16(b, REG_MCUFW_CTRL));
  }
  /* Beacon-Warteschlange wieder ohne Speicher, bevor er freigegeben wird */
  w32(b, PCI_TXBD_DESA_BCNQ, 0);
  dma_free(&ring);
  dma_free(&buf);
  return running;
}

/* ---- Stufe 4: MAC einrichten (rtw88 mac.c: rtw_mac_init) und ein erster Befehl an die Firmware -----------------
 * Nur mit cloke_wifi=4, nach geladener Firmware. Nur Register: txdma_queue_mapping, priority_queue_cfg (Seiten
 * des TX-FIFO verteilen, Reserve bei 460), init_h2c, die 8821C-Voreinstellungen der Sende- und Empfangswege
 * (rtw8821c_mac_init) und die Treiberinfo. Danach der H2C-Befehl "BT-Info abfragen" über die Mailbox: Quittiert
 * die Firmware ihn (REG_HMETFR Bit 0 wird wieder 0), hört sie auf Befehle. */
enum { RTW_DMA_MAPPING_EXTRA = 0, RTW_DMA_MAPPING_LOW = 1, RTW_DMA_MAPPING_NORMAL = 2, RTW_DMA_MAPPING_HIGH = 3 };

static bool mac_init(volatile uint8_t *b) {
  /* txdma_queue_mapping, PCIe: vo/vi normal, be/bk low, mg extra, hi high */
  uint16_t map = 0;
  map |= BIT_TXDMA_VOQ_MAP(RTW_DMA_MAPPING_NORMAL);
  map |= BIT_TXDMA_VIQ_MAP(RTW_DMA_MAPPING_NORMAL);
  map |= BIT_TXDMA_BEQ_MAP(RTW_DMA_MAPPING_LOW);
  map |= BIT_TXDMA_BKQ_MAP(RTW_DMA_MAPPING_LOW);
  map |= BIT_TXDMA_MGQ_MAP(RTW_DMA_MAPPING_EXTRA);
  map |= BIT_TXDMA_HIQ_MAP(RTW_DMA_MAPPING_HIGH);
  w16(b, REG_TXDMA_PQ_MAP, map);
  w8(b, REG_CR, 0);
  w8(b, REG_CR, MAC_TRX_ENABLE);
  w32(b, REG_H2CQ_CSR, BIT_H2CQ_FULL);

  /* rtw_set_trx_fifo_info: 64 KB TX-FIFO in 128-Byte-Seiten, Reserve für Treiber, H2C, Firmware */
  const unsigned txff_pg = 65536 / TX_PAGE_SIZE, drv_pg = 8, csi = 0;
  const unsigned rsvd = drv_pg + RSVD_PG_H2C_EXTRAINFO_NUM + RSVD_PG_H2C_STATICINFO_NUM + RSVD_PG_H2CQ_NUM +
                        RSVD_PG_CPU_INSTRUCTION_NUM + RSVD_PG_FW_TXBUF_NUM + csi;
  const unsigned acq = txff_pg - rsvd, boundary = txff_pg - rsvd;
  unsigned cur = txff_pg - csi;
  cur -= RSVD_PG_FW_TXBUF_NUM;
  cur -= RSVD_PG_CPU_INSTRUCTION_NUM;
  cur -= RSVD_PG_H2CQ_NUM;
  const unsigned h2cq_pg = cur;
  cur -= RSVD_PG_H2C_STATICINFO_NUM;
  cur -= RSVD_PG_H2C_EXTRAINFO_NUM;
  cur -= drv_pg;
  if (cur != boundary) {
    LOG("mac: reserve boundary mismatch %u vs %u", cur, boundary);
    return false;
  }
  /* priority_queue_cfg: page_table_8821c[1] = {hq 16, lq 16, nq 16, exq 14, gapq 1} */
  const unsigned hq = 16, lq = 16, nq = 16, exq = 14, gapq = 1;
  const unsigned pubq = acq - hq - lq - nq - exq - gapq;
  w16(b, REG_FIFOPAGE_INFO_1, hq);
  w16(b, REG_FIFOPAGE_INFO_2, lq);
  w16(b, REG_FIFOPAGE_INFO_3, nq);
  w16(b, REG_FIFOPAGE_INFO_4, exq);
  w16(b, REG_FIFOPAGE_INFO_5, pubq);
  w32(b, REG_RQPN_CTRL_2, r32(b, REG_RQPN_CTRL_2) | BIT_LD_RQPN);
  w16(b, REG_FIFOPAGE_CTRL_2, boundary);
  w8(b, REG_FWHW_TXQ_CTRL + 2, r8(b, REG_FWHW_TXQ_CTRL + 2) | (BIT_EN_WR_FREE_TAIL >> 16));
  w16(b, REG_BCNQ_BDNY_V1, boundary);
  w16(b, REG_FIFOPAGE_CTRL_2 + 2, boundary);
  w16(b, REG_BCNQ1_BDNY_V1, boundary);
  w32(b, REG_RXFF_BNDY, 16384 - C2H_PKT_BUF - 1);
  w8(b, REG_AUTO_LLT_V1, r8(b, REG_AUTO_LLT_V1) | BIT_AUTO_INIT_LLT_V1);
  if (!hw_ready(b, REG_AUTO_LLT_V1, BIT_AUTO_INIT_LLT_V1, 0)) {
    LOG("mac: auto LLT init did not finish");
    return false;
  }
  w8(b, REG_CR + 3, 0);
  LOG("mac: tx fifo %u pages, reserved from %u, public queue %u", txff_pg, boundary, pubq);

  /* init_h2c */
  const uint32_t h2cq_addr = h2cq_pg << TX_PAGE_SIZE_SHIFT, h2cq_size = RSVD_PG_H2CQ_NUM << TX_PAGE_SIZE_SHIFT;
  uint32_t v = r32(b, REG_H2C_HEAD);
  w32(b, REG_H2C_HEAD, (v & 0xFFFC0000) | h2cq_addr);
  v = r32(b, REG_H2C_READ_ADDR);
  w32(b, REG_H2C_READ_ADDR, (v & 0xFFFC0000) | h2cq_addr);
  v = r32(b, REG_H2C_TAIL);
  w32(b, REG_H2C_TAIL, (v & 0xFFFC0000) | (h2cq_addr + h2cq_size));
  uint8_t v8 = r8(b, REG_H2C_INFO);
  w8(b, REG_H2C_INFO, (uint8_t)((v8 & 0xFC) | 0x01));
  v8 = r8(b, REG_H2C_INFO);
  w8(b, REG_H2C_INFO, (uint8_t)((v8 & 0xFB) | 0x04));
  v8 = r8(b, REG_TXDMA_OFFSET_CHK + 1);
  w8(b, REG_TXDMA_OFFSET_CHK + 1, (uint8_t)((v8 & 0x7f) | 0x80));
  uint32_t wp = r32(b, REG_H2C_PKT_WRITEADDR) & 0x3FFFF, rp = r32(b, REG_H2C_PKT_READADDR) & 0x3FFFF;
  uint32_t free_ = wp >= rp ? h2cq_size - (wp - rp) : rp - wp;
  if (free_ != h2cq_size) {
    LOG("mac: H2C queue mismatch (wp 0x%x rp 0x%x)", wp, rp);
    return false;
  }

  /* rtw8821c_mac_init */
  w8(b, REG_AMPDU_MAX_TIME_V1, WLAN_AMPDU_MAX_TIME);
  w8(b, REG_TX_HANG_CTRL, r8(b, REG_TX_HANG_CTRL) | BIT_EN_EOF_V1);
  uint16_t pre = WLAN_PRE_TXCNT_TIME_TH | BIT_EN_PRECNT;
  w8(b, REG_PRECNT_CTRL, (uint8_t)(pre & 0xff));
  w8(b, REG_PRECNT_CTRL + 1, (uint8_t)(pre >> 8));
  w32(b, REG_PROT_MODE_CTRL, WLAN_RTS_LEN_TH | (WLAN_RTS_TX_TIME_TH << 8) | (WLAN_MAX_AGG_PKT_LIMIT << 16) |
                                 ((uint32_t)WLAN_RTS_MAX_AGG_PKT_LIMIT << 24));
  w16(b, REG_BAR_MODE_CTRL + 2, WLAN_BAR_RETRY_LIMIT | WLAN_RA_TRY_RATE_AGG_LIMIT << 8);
  w8(b, REG_FAST_EDCA_VOVI_SETTING, FAST_EDCA_VO_TH);
  w8(b, REG_FAST_EDCA_VOVI_SETTING + 2, FAST_EDCA_VI_TH);
  w8(b, REG_FAST_EDCA_BEBK_SETTING, FAST_EDCA_BE_TH);
  w8(b, REG_FAST_EDCA_BEBK_SETTING + 2, FAST_EDCA_BK_TH);
  w8(b, REG_INIRTS_RATE_SEL, r8(b, REG_INIRTS_RATE_SEL) | BIT(5));
  w8(b, REG_TIMER0_SRC_SEL, r8(b, REG_TIMER0_SRC_SEL) & (uint8_t)~BIT_TSFT_SEL_TIMER0);
  w16(b, REG_TXPAUSE, 0);
  w8(b, REG_SLOT, WLAN_SLOT_TIME);
  w8(b, REG_PIFS, WLAN_PIFS_TIME);
  w32(b, REG_SIFS, WLAN_SIFS_CFG);
  w16(b, REG_EDCA_VO_PARAM + 2, WLAN_VO_TXOP_LIMIT);
  w16(b, REG_EDCA_VI_PARAM + 2, WLAN_VI_TXOP_LIMIT);
  w32(b, REG_RD_NAV_NXT, WLAN_NAV_CFG);
  w16(b, REG_RXTSF_OFFSET_CCK, WLAN_RX_TSF_CFG);
  w8(b, REG_BCN_CTRL, r8(b, REG_BCN_CTRL) | BIT_EN_BCN_FUNCTION);
  w32(b, REG_TBTT_PROHIBIT, WLAN_TBTT_TIME);
  w8(b, REG_DRVERLYINT, WLAN_DRV_EARLY_INT);
  w8(b, REG_BCNDMATIM, WLAN_BCN_DMA_TIME);
  w8(b, REG_TX_PTCL_CTRL + 1, r8(b, REG_TX_PTCL_CTRL + 1) & (uint8_t)~(BIT_SIFS_BK_EN >> 8));
  w16(b, REG_RXFLTMAP0, WLAN_RX_FILTER0);
  w16(b, REG_RXFLTMAP1, WLAN_RX_FILTER1);
  w16(b, REG_RXFLTMAP2, WLAN_RX_FILTER2);
  w32(b, REG_RCR, WLAN_RCR_CFG);
  w8(b, REG_RX_PKT_LIMIT, WLAN_RXPKT_MAX_SZ_512);
  w8(b, REG_TCR + 2, WLAN_TX_FUNC_CFG2);
  w8(b, REG_TCR + 1, WLAN_TX_FUNC_CFG1);
  w8(b, REG_ACKTO_CCK, 0x40);
  w8(b, REG_WMAC_TRXPTCL_CTL_H, r8(b, REG_WMAC_TRXPTCL_CTL_H) | BIT(1));
  w8(b, REG_SND_PTCL_CTRL, r8(b, REG_SND_PTCL_CTRL) | BIT_DIS_CHK_VHTSIGB_CRC);
  w32(b, REG_WMAC_OPTION_FUNCTION + 8, WLAN_MAC_OPT_FUNC2);
  w8(b, REG_WMAC_OPTION_FUNCTION + 4, WLAN_MAC_OPT_NORM_FUNC1);

  /* rtw_drv_info_cfg */
  w8(b, REG_RX_DRVINFO_SZ, PHY_STATUS_SIZE);
  v8 = r8(b, REG_TRXFF_BNDY + 1);
  w8(b, REG_TRXFF_BNDY + 1, (uint8_t)((v8 & 0xF0) | 0xF));
  w32(b, REG_RCR, r32(b, REG_RCR) | BIT_APP_PHYSTS);
  w32(b, REG_WMAC_OPTION_FUNCTION + 4, r32(b, REG_WMAC_OPTION_FUNCTION + 4) & ~(BIT(8) | BIT(9)));
  LOG("mac: init done (CR 0x%02x RCR 0x%08x TXPAUSE 0x%04x)", r8(b, REG_CR), r32(b, REG_RCR), r16(b, REG_TXPAUSE));
  return true;
}

static void h2c_probe(volatile uint8_t *b) {
  /* H2C_CMD_QUERY_BT_INFO (0x61) mit Bit 8, wie rtw_fw_query_bt_info: Mailbox 0 */
  if (r8(b, REG_HMETFR) & 1) {
    LOG("h2c: mailbox 0 still busy before sending (HMETFR 0x%02x)", r8(b, REG_HMETFR));
    return;
  }
  w32(b, REG_HMEBOX0_EX, 0);
  w32(b, REG_HMEBOX0, 0x61u | (1u << 8));
  unsigned i = 0;
  for (; i < 3000 && (r8(b, REG_HMETFR) & 1); i++) IODelay(100);
  if (r8(b, REG_HMETFR) & 1) LOG("h2c: firmware did not take the command (HMETFR 0x%02x)", r8(b, REG_HMETFR));
  else LOG("h2c: firmware took the command after %u us (HMETFR 0x%02x), FW_DBG7 0x%08x", i * 100, r8(b, REG_HMETFR),
           r32(b, REG_FW_DBG7));
}

/* ---- Stufe 5: PHY einrichten (rtw8821c_phy_set_param): Basisband, AGC und Funkteil aus den rtw88-Tabellen ---- */
enum { REG_RF_CTRL = 0x1F, REG_WLRF1 = 0xEC, REG_AFE_XTAL_CTRL = 0x24, REG_AFE_PLL_CTRL = 0x28,
       REG_CCK0_FAREPORT = 0xA2C, REG_RXPSEL = 0x808, RF_A_BASE = 0x2800, RF_A_SIPI = 0xC90, RFREG_MASK = 0xFFFFF };

static void rf_write(volatile uint8_t *b, unsigned addr, uint32_t data) {
  addr &= 0xff;
  if (addr == 0) {
    w32(b, RF_A_SIPI, ((addr << 20) | (data & RFREG_MASK)) & 0x0fffffff);
    IODelay(13);
  } else {
    unsigned a = RF_A_BASE + (addr << 2);
    w32(b, a, (r32(b, a) & ~(uint32_t)RFREG_MASK) | (data & RFREG_MASK));
    IODelay(1);
  }
}

static uint32_t rf_read(volatile uint8_t *b, unsigned addr) { return r32(b, RF_A_BASE + ((addr & 0xff) << 2)) & RFREG_MASK; }

/* Tabellen mit Bedingungen (if/elif/else/endif) wie rtw_parse_tbl_phy_cond; kind 0 mac (8 Bit), 1 bb/agc (32 Bit), 2 rf */
struct PhyCond { unsigned rfe, intf, pkg, cut; };

static bool cond_positive(uint32_t c, const PhyCond &d) {
  unsigned rfe = c & 0xff, intf = (c >> 8) & 0xf, pkg = (c >> 12) & 0xf, cut = (c >> 24) & 0xf;
  if (cut && cut != d.cut) return false;
  if (pkg && pkg != d.pkg) return false;
  if (intf && intf != d.intf) return false;
  return rfe == d.rfe;
}

static unsigned load_table(volatile uint8_t *b, const uint32_t *t, unsigned n, int kind, const PhyCond &d) {
  uint32_t pos = 0;
  bool matched = true, skipped = false;
  unsigned written = 0;
  for (unsigned i = 0; i + 1 < n; i += 2) {
    uint32_t a = t[i], v = t[i + 1];
    if (a & 0x80000000u) {
      switch ((a >> 28) & 3) {
        case 3: matched = true; skipped = false; break;
        case 2: matched = skipped ? false : true; break;
        default: pos = a; break;
      }
    } else if (a & 0x40000000u) {
      if (!skipped) {
        if (cond_positive(pos, d)) { matched = true; skipped = true; }
        else { matched = false; skipped = false; }
      } else matched = false;
    } else if (matched) {
      written++;
      if (kind == 0) w8(b, a, (uint8_t)v);
      else if (kind == 1) {
        if (a == 0xfe) IOSleep(50);
        else if (a == 0xfd) IOSleep(5);
        else if (a == 0xfc) IOSleep(1);
        else if (a == 0xfb || a == 0xfa || a == 0xf9) IODelay(a == 0xfb ? 50 : a == 0xfa ? 5 : 1);
        else w32(b, a, v);
      } else {
        if (a == 0xffe) IOSleep(50);
        else if (a == 0xfe) IODelay(100);
        else rf_write(b, a, v);
      }
    }
  }
  return written;
}

static bool phy_init(volatile uint8_t *b, unsigned cut) {
  const uint8_t *m = g_logm;
  unsigned rfe = m[0xCA] & 0x1f, xtal = m[0xB9] & 0x3f;
  unsigned pkg_type = (m[0xCA] & 0x20) ? 1 : 0;
  PhyCond d = {rfe, 1u /* PCIe */, pkg_type ? pkg_type : 15u, cut ? cut : 15u};
  bool btg = rfe == 2 || rfe == 4 || rfe == 7 || rfe == 10 || rfe == 12 || rfe == 15;
  LOG("phy: rfe option %u, xtal cap %u, pkg %u, cut %u, btg %d", rfe, xtal, pkg_type, cut, (int)btg);

  uint8_t v = r8(b, REG_SYS_FUNC_EN) | (1 << 6); /* BIT_FEN_PCIEA */
  w8(b, REG_SYS_FUNC_EN, v);
  v |= 3; /* BB_RSTB | BB_GLB_RST */
  w8(b, REG_SYS_FUNC_EN, v);
  v &= (uint8_t)~3;
  w8(b, REG_SYS_FUNC_EN, v);
  v |= 3;
  w8(b, REG_SYS_FUNC_EN, v);
  w8(b, REG_RF_CTRL, 7); /* RF_EN | RF_RSTB | RF_SDM_RSTB */
  IODelay(10);
  w8(b, REG_WLRF1 + 3, 7);
  IODelay(10);
  w32(b, REG_RXPSEL, r32(b, REG_RXPSEL) & ~(3u << 28));

  unsigned nm = load_table(b, rtw8821c_mac, rtw8821c_mac_n, 0, d);
  unsigned nb = load_table(b, rtw8821c_bb, rtw8821c_bb_n, 1, d);
  unsigned na = load_table(b, rtw8821c_agc, rtw8821c_agc_n, 1, d);
  unsigned ng = btg ? load_table(b, rtw8821c_agc_btg_type2, rtw8821c_agc_btg_type2_n, 1, d) : 0;
  unsigned nr = load_table(b, rtw8821c_rf_a, rtw8821c_rf_a_n, 2, d);
  LOG("phy: tables written: mac %u, bb %u, agc %u, agc-btg %u, rf %u", nm, nb, na, ng, nr);

  w32(b, REG_AFE_XTAL_CTRL, (r32(b, REG_AFE_XTAL_CTRL) & ~0x7e000000u) | (xtal << 25));
  w32(b, REG_AFE_PLL_CTRL, (r32(b, REG_AFE_PLL_CTRL) & ~0x7eu) | (xtal << 1));
  w32(b, REG_CCK0_FAREPORT, r32(b, REG_CCK0_FAREPORT) & ~((1u << 18) | (1u << 22)));
  w32(b, REG_RXPSEL, r32(b, REG_RXPSEL) | (3u << 28));
  LOG("phy: RF A reg 0x00 = 0x%05x, reg 0x18 = 0x%05x, reg 0x08 = 0x%05x; BB 0x808 = 0x%08x", rf_read(b, 0), rf_read(b, 0x18),
      rf_read(b, 8), r32(b, 0x808));
  return rf_read(b, 0) != RFREG_MASK && rf_read(b, 0) != 0;
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
  IOMemoryDescriptor *md = IOMemoryDescriptor::withPhysicalAddress(phys, REG_WINDOW, kIODirectionNone);
  map = md ? md->map(kIOMapInhibitCache) : nullptr;
  OSSafeReleaseNULL(md);
  if (!map || map->getLength() < REG_WINDOW) {
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
  if (on >= 2 && power_cycle(base, (cfg1 >> 12) & 0xf) && (dump_efuse(base), true) && on >= 3 && download_fw(base) && on >= 4 && mac_init(base)) {
    h2c_probe(base);
    if (on >= 5) phy_init(base, (cfg1 >> 12) & 0xf);
  }
  return true;
}

void ClokeRtw88::stop(IOService *provider) {
  OSSafeReleaseNULL(map);
  IOService::stop(provider);
}
