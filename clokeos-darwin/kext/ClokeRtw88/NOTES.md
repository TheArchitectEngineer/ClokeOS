# ClokeRtw88: Wi-Fi for the RTL8821CE (HP 15s-fq5655ng; the diagnostics show 10ec:c821)

Status: stage 1 only (identify and read, `ClokeRtw88.cpp`). Not tried on the laptop yet.

## What exists on the machine side
- PureDarwin has no Wi-Fi at all: no IO80211Family, no Wi-Fi-capable `configd` plugins, no supplicant. Apple's own
  Wi-Fi kexts cannot run here.
- Source to port from: Linux `drivers/net/wireless/realtek/rtw88` (`rtw8822c*.c`, `main.c`, `pci.c`, `fw.c`, `coex.c`,
  `tx.c`, `rx.c`), licensed `GPL-2.0 OR BSD-3-Clause`, so it can be used under BSD. Firmware: `rtw88/rtw8822c_fw.bin`
  from linux-firmware (Realtek's redistribution licence). Register tables in `rtw8822c_table.c` are 1.1 MB of data.
- PCI id 10ec:c821 on this laptop (RTL8821CE, 1x1; rtw88 files `rtw8821c*.c`, firmware `rtw88/rtw8821c_fw.bin`); the
  8822CE (c822, `rtw8822c*.c`) is the 2x2 sibling. Registers in BAR2 (`pci.c`: `bar_id = 2`), chip id in `REG_SYS_CFG1` (0xF0).

## Stages
1. Identify, map BAR2, read version registers, log (done; check `dmesg | grep ClokeRtw88` or clokeos-diag on the HP).
2. Power-on sequence (`rtw_mac_power_on`: pwr_on_seq tables), firmware download over the "H2C" DMA ring, TX/RX ring setup,
   MAC/BB/RF init tables. Success = firmware reports ready, RX ring delivers beacons.
3. `IONetworkController` ("en1") that hands raw 802.11 frames to a userland helper through a character device
   (like /dev/clokeos_touchpad); scanning (channel hops via H2C), TX/RX path, interrupt or polled.
4. Userland: scan, open + WPA2-PSK (4-way handshake, CCMP) in a small C daemon (or wpa_supplicant with a custom driver
   backend), key installation through H2C/CAM registers, `clokeos-network` integration (DHCP on the new interface).
5. Power save, regulatory limits, WPA3, roaming: later or never.

Stages 2 to 4 are weeks of work and every step needs the laptop (nothing here emulates this chip), so each test costs the
user a boot. The rtw88 coexistence code (`coex.c`, Bluetooth sharing) can be skipped at first.
Until then: USB tethering or a LAN cable (`clokeos-network`) is the way to the internet.
