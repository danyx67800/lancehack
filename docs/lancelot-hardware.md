# lancelot hardware facts

Target: Xiaomi Redmi 9 (`lancelot`), MediaTek **MT6769V/CU** (Helio G80), part of the
MT6768 family. The phone currently runs **AfterlifeOS (Android 16, user build)**.

Every value carries a **source** and a **status**:

| Status | Meaning |
| --- | --- |
| CONFIRMED | Read from the device or from a primary artifact (the shipped DTB/DTBO, the boot image header, `fastboot getvar`, mainline/vendor DT source). |
| HYPOTHESIS | Consistent with the family or a sibling board; must be measured before use. |
| UNKNOWN | No source. Do not invent a value. |

Rule: **no address, register or constant goes into code without a row here.** When a
shim measurement confirms or contradicts a row, update it and name the boot that
measured it.

Last review: 2026-10-24 (M1: values below marked CONFIRMED come from the LineageOS
24.0 lancelot `boot.img`/`dtbo.img` — which carry the same board DTB/overlay as the
installed ROM — and from the phone itself).

## 1. SoC and CPU

| Fact | Value | Source | Status |
| --- | --- | --- | --- |
| SoC | MT6769V/CU (Helio G80), 12 nm | DTB root `compatible = "mediatek,MT6768"`; GSMArena | CONFIRMED |
| Cores | 2× Cortex-A75 + 6× Cortex-A55 | DTB `cpus` | CONFIRMED |
| ISA | ARMv8.2-A, **no pointer authentication** | A75/A55 cores; same class as sweet/beryllium | CONFIRMED (design) |
| CPU `reg` | A55 `0x000,0x100,0x200,0x300,0x400,0x500`; A75 `0x600,0x700` | mainline `mt6768.dtsi`; DTB `cpus` nodes | CONFIRMED |
| `enable-method` | `psci` (all eight) | DTB | CONFIRMED |
| Live MPIDR | — | — | UNKNOWN (shim prints it in M2) |

## 2. Interrupts, timer, PSCI

| Fact | Value | Source | Status |
| --- | --- | --- | --- |
| GIC | **GICv3**, `compatible = "arm,gic-v3"` | DTB `interrupt-controller` | CONFIRMED |
| GIC distributor | `0x0c000000`, size `0x40000` | DTB `reg` | CONFIRMED |
| GIC redistributor | `0x0c040000`, size `0x200000` (8 PEs, 1 region) | DTB `reg`, `#redistributor-regions = 1` | CONFIRMED |
| MediaTek sysirq | `intpol-controller@0`, `mediatek,mt6577-sysirq`, registers at `0x0c53a650` (size `0x50`) | DTB | CONFIRMED |
| Timer PPIs | secure phys **13**, non-secure phys **14**, virtual **11**, hyp **10**, all level-low | DTB `timer` interrupts | CONFIRMED |
| **`CNTFRQ`** | **13 000 000 Hz** (`0xc65d40`) | DTB `timer/clock-frequency` | **CONFIRMED** |
| PSCI | `arm,psci-1.0`, `method = "smc"` | DTB | CONFIRMED |

The sysirq block is a MediaTek quirk: Linux programs it to set trigger types for a few
peripheral interrupts. The peripheral numbers in the DTB (`interrupts = <0 N type>`,
N is a raw GIC SPI) are already the real INTIDs; a bare-metal/GICv3 path can use them
directly and ignore sysirq unless an interrupt turns out to be mis-typed.

## 3. Memory

| Fact | Value | Source | Status |
| --- | --- | --- | --- |
| Installed RAM | **4 GB** | user (2026-10-24) | CONFIRMED by owner |
| `memory` node in the shipped DTB | `reg = <0x0 0x40000000 0x0 0x3e605000>` (~997 MiB) | `boot.img` DTB | CONFIRMED as DTB content — **LK patches this at boot**, so it is not the installed size |
| Real DRAM map (live FDT) | **one bank, base `0x40000000`, size `0x1_00000000` (4 GiB)**, i.e. the range crosses the 4 GiB boundary up to `0x1_40000000` - the normal MTK 4 GB layout, since the first GiB of address space belongs to peripherals | MEASURED on the phone, TEST #11 (`MEM0`/`size` on screen). Re-read `size` on the next instrument boot: the two `hi` digits are the only ones the photo leaves ambiguous |
| DRAM SKU | 4 GB (one bank, no second bank above 4 GiB) | MEASURED, TEST #11 |
| LK FDT address | `0x4bc80000` (= `BOARD_KERNEL_BASE + BOARD_DTB_OFFSET`, same as the tags address) | `boot.img` header: `tags_addr`/`dtb_addr` = `0x4bc80000` | CONFIRMED |
| Android kernel load | `0x40080000` | header (`--base 0x40078000`, `--kernel_offset 0x8000`) | CONFIRMED |
| Android ramdisk load | `0x47c80000` | header | CONFIRMED |

### Carve-outs, from the LK FDT of a 3 GiB unit (start + size)

| Node | Start | Size |
| --- | --- | --- |
| `mblock-4-atf-reserved` | `0x4ce00000` | `0x00060000` |
| `mblock-8-ram_console` | `0x4d000000` | `0x00010000` |
| `mblock-9-pstore` (ramoops at `0x4d05f000`) | `0x4d010000` | `0x000e0000` |
| `mblock-10-minirdump` | `0x4d0f0000` | `0x00010000` |
| `mblock-7-tee-reserved` | `0x70000000` | `0x04200000` |
| `mblock-13-SPM-reserved` | `0x77ff0000` | `0x00010000` |
| `mblock-17-md_smem_ncache` | `0x78000000` | `0x01000000` |
| `mblock-18-ccci` | `0x7c000000` | `0x00c00000` |
| `mblock-11-framebuffer` | `0x7dcb0000` | `0x02250000` |
| `mblock-6-SSPM-reserved` | `0x7ff00000` | `0x000c0000` |
| `mblock-3-log_store` | `0x7ffc0000` | `0x00040000` |
| `mblock-14-SCP-reserved` | `0x9f900000` | `0x00600000` |
| `mblock-15-ccci_tag_mem` | `0x9ffef000` | `0x00010000` |
| `mblock-1-dramc-rk0` | `0x9ffff000` | `0x00001000` |
| `mblock-16-ccci` (modem) | `0xae000000` | `0x10000000` |
| `mblock-5-atf-log-reserved` | `0xbfe00000` | `0x00040000` |
| `mblock-12-atf-ramdump-memory` | `0xbff80000` | `0x00080000` |
| `mblock-2-dramc-rk1` | `0xfffff000` | `0x00001000` |

Consequence: on a 3 GiB unit the only sane kernel window is `0x80000000`–`0xae000000`
(768 MiB, minus `0x9f900000`/`0x9ffff000`). A 4 GiB phone presumably has more above
4 GiB, but the shim must read the live FDT before choosing a window. Plan for
`cpus=1` and a small physmap first.

## 4. Boot chain

| Fact | Value | Source | Status |
| --- | --- | --- | --- |
| Chain | BROM → preloader → ATF (BL31) → LK (BL33) → `boot.img` | MTK platform; ATF/TEE carve-outs in the LK FDT | CONFIRMED |
| `fastboot boot` | **NOT implemented**: the transfer stalls at `Sending 'boot.img'` and LK stops answering `getvar` until a power cycle | reproduced on this phone 2026-10-24; XDA thread; hataketsu notes | CONFIRMED |
| `fastboot fetch` | **not implemented**: the host waits forever, no data | reproduced on this phone | CONFIRMED |
| Boot image header | **v2**, page size **2048**, header_size 1660 | `boot.img` header (unpacked) | CONFIRMED |
| Load addresses | kernel `0x40080000`, ramdisk `0x47c80000`, second `0x100000000`, tags `0x4bc80000`, dtb `0x4bc80000` | header | CONFIRMED |
| Kernel payload | **gzip**, 13 505 892 bytes (LK decompresses; a raw `Image` crashed LK in the mainline port) | `boot.img` + port notes | CONFIRMED |
| Ramdisk | 1 511 850 bytes (LineageOS recovery ramdisk) | `boot.img` | CONFIRMED |
| DTB in boot.img | 118 867 bytes, at image offset 15 022 080 | `boot.img` | CONFIRMED |
| Stock cmdline | `bootopt=64S3,32N2,64N2 androidboot.serialconsole=0` | header | CONFIRMED |
| A/B slots | none | `ro.boot.slot_suffix` empty; `getvar all` | CONFIRMED |
| Dynamic partitions | yes (`super` 7 GiB) | `getvar all`; fstab | CONFIRMED |
| Key combos | fastboot = Vol- + Power, recovery = Vol+ + Power | LineageOS wiki; reproduced | CONFIRMED |

### Partitions (full `getvar all`, 52 entries)

| Partition | Size | Partition | Size |
| --- | --- | --- | --- |
| `boot` | `0x4000000` = 64 MiB | `boot_para` | 26 MiB |
| `cache` | 432 MiB | `cust` | 1 GiB |
| `devinfo` | 8 MiB | `dtbo` | 16 MiB |
| `efuse` | 512 KiB | `exaid` | 384 MiB |
| `expdb` | 20 MiB | `ffu` | 11.5 MiB |
| `flashinfo` | 16 MiB | `frp` | 64 KiB |
| `gsort` | 16 MiB | `gz1`, `gz2` | 16 MiB each |
| `lk`, `lk2` | 2 MiB each | `logo` | 8 MiB |
| `md1img` | 128 MiB | `md_udc` | 22.6 MiB |
| `metadata` | 32 MiB | `minidump` | 128 MiB |
| `misc` | 512 KiB | `nvcfg` | 32 MiB |
| `nvdata` | 64 MiB | `nvram` | 64 MiB |
| `otp` | 43 MiB | `para` | 512 KiB |
| `persist` | 48 MiB | `persistbak` | 48 MiB |
| `pgpt` | 32 KiB | `preloader` | 512 KiB |
| `proinfo` | 3 MiB | `protect1` | 8 MiB |
| `protect2` | 11.4 MiB | **`recovery`** | **64 MiB** |
| `scp1`, `scp2` | 6 MiB each | `sec1` | 2 MiB |
| `seccfg` | 8 MiB | `sgpt` | 32 KiB |
| `spmfw` | 1 MiB | `sspm_1`, `sspm_2` | 1 MiB each |
| `super` | 7 GiB | `tee1`, `tee2` | 5 MiB each |
| `userdata` | ≈48.35 GiB | `vbmeta`, `vbmeta_system`, `vbmeta_vendor` | 8 MiB each |

**`recovery` exists**, so the chosen M2 boot path is viable: the shim goes into
`recovery`, Android's `boot` is never touched.

`userdata` is reached through `/dev/block/platform/bootdevice/by-name/userdata`, never
a hard-coded `mmcblk0pN`.

**Never write:** `preloader`, `lk`/`lk2`, `tee1`/`tee2`, `seccfg`, `nvram`, `nvdata`,
`nvcfg`, `protect1`/`protect2`, `proinfo`, `persist`/`persistbak`, `md1img`/`md1dsp`,
`frp`, `metadata`, `efuse`, `sec1`, `otp` and anything calibration/identity related.
`userdata` is the only default write target; `boot`/`recovery` need a verified backup
and an explicit confirmation in the moment (see `docs/RECOVERY.md`).

## 5. Storage (eMMC)

| Fact | Value | Source | Status |
| --- | --- | --- | --- |
| eMMC controller | **MSDC0** `0x11230000`, size `0x10000`, IRQ **SPI 100** | DTB `msdc@11230000`, `mmc@11230000` | CONFIRMED |
| MSDC0 top block | `0x11cd0000`, size `0x1000` | DTB `msdc0_top@11cd0000` | CONFIRMED |
| SD card | MSDC1 `0x11240000`, IRQ SPI 101 (not the root) | DTB | CONFIRMED |
| Bus | 8-bit, non-removable, `mmc-ddr-1_8v`, HS200, **HS400**, `max-frequency = 200 MHz` | DTB `mmc0` | CONFIRMED |
| CQE | **`mediatek,cqhci`** present | DTB `mmc0` | CONFIRMED |
| Clocks | `clocks = <topckgen …>, <infracfg_ao …>` named `source`, `hclk`, `source_cg`, `crypto_clk` | DTB `mmc0` | CONFIRMED (phandles unresolved; resolve before programming) |

This replaces the whole UFS driver of sweet/beryllium and is the largest block of new
kernel code (M4).

## 6. Display

| Fact | Value | Source | Status |
| --- | --- | --- | --- |
| Panel | `hx83112b_fhdp_dsi_cmd_auo_rt5081_drv` (Himax HX83112B, AUO, RT5081 backlight) | DTBO `/chosen atag,videolfb-lcmname` | CONFIRMED |
| Resolution / mode | 1080×2340, **command mode** (`_dsi_cmd_`) | DTBO; vendor DTS | CONFIRMED |
| Bootloader framebuffer | base **`0x7e605000`** (`fb_base_l` = 0x7e605000, `fb_base_h` = 0), inside the `0x7dcb0000`+34 MiB carve-out | DTBO atag values; LK FDT | CONFIRMED |
| VRAM size | `0x017e8000` (25.0 MiB) | DTBO `atag,videolfb-vramSize` | CONFIRMED |
| Panel fps tag | `0x1770` = 6000 (60.00 Hz) | DTBO | CONFIRMED |
| LCM inited at LK | `islcmfound = 1`, `islcm_inited = 0` - yet the panel scans continuously after LK hands over | DTBO; MEASURED, TEST #11 |
| Live fb base from LK | `atag,videolfb-fb_base_l = 0x7e605000` at runtime, `hi = 0` - identical to the DTBO value, so the DTBO placeholder is authoritative here | Live FDT via the shim, TEST #11 |
| Watchdog register | `TOPRGU_WDT_MODE` reads `0x5D` before the shim touches it and `0x5C` after clearing bit 0: the shim's disable **works on this SoC** | MEASURED, TEST #11 |
| System counter | `CNTFRQ_EL0 = 0x00C65D40` = **13 MHz** at runtime (the DT value was right); 200k loop iterations advanced `CNTPCT` by ~1.0M ticks (~78 ms) | MEASURED, TEST #11 |
| GIC registers as found | `GICD_CTLR = 0x12` (enable + affinity routing), `GICD_TYPER = 0x0079040F` (512 SPIs), `GICR_WAKER = 0` (core awake) | MEASURED, TEST #11 |
| Pixel format | 32 bpp, `0xAARRGGBB`, byte-identical to the shim's colour constants | MEASURED on the phone, TEST #6 (white and yellow came out as white and yellow) |
| Buffer pitch (stride) | **4320 bytes (1080 px)**, no padding. The four equal byte quarters of the geometry probe came out as clean horizontal bands with the exact 585-row spacing a 1080-px pitch predicts | MEASURED on the phone, TEST #9 (photo); TEST #10 confirmed the 1:1 scale with two rules 540 px apart |
| Scanout rotation | **none**: the frame is scanned as contiguous lines. The TEST #6 "text on the right edge" reading was the horizontal offset below, seen through a wrong pitch | DISPROVED, TEST #7 (photo) and TEST #11 (text upright and aligned) |
| Scanout horizontal offset | the panel shows **column `x + 256`** of each line for panel column `x`: the scanout starts 1024 B into the line, so the last 256 px of the panel show the *start of the next buffer row*. Rules at buffer columns 0 and 540 landed at panel x ~265 and ~790. Confirmed by the console: a frame drawn at `x = 8` is flush with the panel's left edge and text does not wrap | MEASURED on the phone, TEST #10 (photo) + TEST #11 |
| Top strip of the panel | the first ~500 px show LK's own splash layer (the bootloader-unlocked padlock), not our buffer; the console starts at buffer row 540 and is fully visible | MEASURED on the phone, TEST #7 and #11 (photos) |
| Bottom of the buffer | the panel seems to stop showing the buffer around row ~1700: on TEST #12 the last of 23 text rows (48 px pitch, from row 620) sat at the bottom of the screen and the seven colour bands at rows 2276-2340 were absent from the photo. Either the scanout window is shorter than the 2340-row buffer, or the photo was cropped | MEASURED on the phone, TEST #12 (photo) | OPEN - the shim keeps its output above row ~1650 until this is settled |
| Pixel format | 32 bpp `0xAARRGGBB` | CONFIRMED, TEST #6/#9 (every colour constant renders as intended) |
| Continuous scanout after LK | **yes**, the panel keeps refreshing our writes without any kick | MEASURED on the phone, TEST #5/#6: buffers written after LK took effect immediately |
| MMSYS mutex | `0x14016000`, size `0x1000` | DTB | CONFIRMED |
| DSI0 | `0x14014000`, size `0x1000`, IRQ SPI 238 | DTB | CONFIRMED |
| OVL0_2L | `0x1400c000`, size `0x1000`, IRQ SPI 242 | DTB | CONFIRMED |
| Display clocks | `dispsys` node lists MMSYS/SMI/OVL/RDMA/WDMA/COLOR/CCORR/AAL/GAMMA/DITHER/DSI0 clocks | DTB `dispsys` | CONFIRMED (names; phandles unresolved) |

iOS presents an iPhone X canvas (1125×2436). Lancelot is 1080×2340; the mapping
(scale vs letterbox, @2x vs @3x) is a `mkadt.py` value decided in M5.

## 7. Input

| Fact | Value | Source | Status |
| --- | --- | --- | --- |
| Touch controller | **Novatek NT36672** over SPI (`novatek36672,NVT-ts-spi`; the DTBO also lists a Focaltech compatible, but the Novatek criteria tables are the fitted one) | DTBO `novatek36672@0` | CONFIRMED |
| Bus | **SPI0** `0x1100a000`, size `0x1000`, IRQ **SPI 138**, max 8 MHz (`0x7a1200`) | DTB `spi0`; DTBO | CONFIRMED |
| Reset / IRQ | reset **GPIO 92** (`0x5c`), IRQ **GPIO 1** (`0x2001` = active-low + edge) | DTBO | CONFIRMED |
| Protocol extras | `swrst-n8-addr 0x3f0fe`, `spi-rd-fast-addr 0x3f310`, 10 touch points | DTBO | CONFIRMED |
| Touch rail | PMIC `vldo28` | vendor DTS | CONFIRMED |
| Keypad | MTK **KP** `0x10010000`, size `0x1000`, IRQ **SPI 73**, falling edge; `mediatek,boot_mode = 1` | DTB `keypad` | CONFIRMED |
| PMIC | **MT6358** behind **PWRAP** `0x1000d000`, IRQ SPI 194, PIO-routed interrupt | DTB `pwrap`/`mt6358-pmic` | CONFIRMED |
| Charger | external **SMB1351** at I2C7 address `0x55`, interrupt GPIO 20 | vendor DTS | CONFIRMED (node); fuel gauge UNKNOWN |

Battery can start static (100 %, charging icon) as on sweet; a real gauge is M6+.

## 8. Debug channels

| Channel | Value | Status |
| --- | --- | --- |
| UART0 | `0x11002000`, size `0x1000`, IRQ SPI 112; `apuart0` alias present | CONFIRMED (DT). Physical access on the PCB: UNKNOWN |
| UART1 | `0x11003000`, IRQ SPI 113 | CONFIRMED (DT) |
| USB | MTK MUSB-style controller (`usb1p_sif@11210000` is a sibling block) | HYPOTHESIS — confirm in the stock DTB before writing a driver |
| Framebuffer console | draw into `0x7e605000` from the shim and XNU | planned |
| Persistent log ring | end of `userdata` (needs MSDC write, M4) | planned |
| ramoops / pstore | `0x4d010000` + `0xe0000`, Linux console at `0x4d05f000` | CONFIRMED (LK FDT) — usable before MSDC works |

No evidence of an accessible debug UART without opening the case. Framebuffer remains
the primary console.

## 9. Watchdog and resets

| Fact | Value | Source | Status |
| --- | --- | --- | --- |
| Block | `toprgu@10007000`, size **`0x1000`**, IRQ SPI 156 | DTB | CONFIRMED |
| `mediatek,rg_dfd_timeout` | `0xa0` | DTB | CONFIRMED |
| Registers | `WDT_MODE 0x00`, `WDT_RST 0x08`, `WDT_SWRST 0x14` | Linux `mtk_wdt.c` | CONFIRMED |
| Disable | clear `WDT_MODE` bit 0 (EN) while writing key `0x22000000` | `mtk_wdt_stop()` | CONFIRMED |
| `WDT_RST` reload key | `0x1971` | same | CONFIRMED |
| `WDT_SWRST` key | `0x1209` (reset, not disable) | same | CONFIRMED |
| Reboot mode | `syscon-reboot-mode` at TOPRGU offset `0x24`, mask `0x0f` (1 charger, 2 recovery, 3 bootloader, 4 dm-verity corrupt) | DTB | CONFIRMED |

The shim stops the watchdog before anything slow, or later measurements are unreliable.

## 10. Clocks and power blocks

| Block | Base | Size | Source |
| --- | --- | --- | --- |
| topckgen | `0x10000000` | `0x1000` | DTB |
| infracfg_ao | `0x10001000` | `0x1000` | DTB |
| scpsys (SPM) | `0x10001000`, `0x10006000`, … | `0x1000` each | DTB |
| pericfg | `0x10003000` | `0x1000` | DTB |
| pinctrl (`pio`) | `0x10005000` | — | DTB |
| apmixedsys | `0x1000c000` | `0x1000` | DTB |
| pwrap | `0x1000d000` | `0x1000` | DTB |
| mcucfg | `0x0c530000` | `0x10000` | DTB |

Mainline lancelot needs `clk_ignore_unused pd_ignore_unused` with the stock bootloader:
Linux relies on firmware-programmed clocks/power. A bare-metal shim preserves what LK
set up and adds votes only where a measurement proves something is off.

## 11. On-device read, 2026-10-24 (M1)

| Variable | Value |
| --- | --- |
| `product` | `lancelot` |
| `unlocked` | `yes` (bootloader already unlocked) |
| `secure` | `no` |
| `hw-revision` | `ca00` |
| `max-download-size` | `0x8000000` (128 MiB) |
| `logical-block-size` / `erase-block-size` | 512 B / 512 KiB |
| `battery-voltage` | 3736 mV, `battery-soc-ok: yes` |
| ROM | AfterlifeOS, Android 16, `BP2A.250605.031.A2`, build type **user** (no `adb root`) |
| Base firmware fingerprint | `Redmi/lancelot_global/lancelot:11/RP1A.200720.011/V12.5.6.0.RJCMIXM` |
| Screen (Android) | 1080×2340, density 440 |
| `/proc/cmdline`, `/proc/device-tree` | permission denied for the `shell` user (SELinux) |

Operational findings reproduced on this unit: `fastboot fetch` unsupported; a
`fastboot boot` attempt wedges LK until the phone is power-cycled.

## Sources

1. Shipped board DTB — `boot.img` of LineageOS 24.0 lancelot (20261024), unpacked with `tools/unpack_bootv2.py`, decompiled with `dtc`
2. Shipped board overlay — `dtbo.img` (same build), entry 0, id 0x0
3. mainline MT6768 SoC DTS — <https://github.com/mt6768-mainline/linux/blob/6.18/arch/arm64/boot/dts/mediatek/mt6768.dtsi>
4. mainline Xiaomi common DTS — <https://github.com/mt6768-mainline/linux/blob/6.18/arch/arm64/boot/dts/mediatek/mt6769t-xiaomi-common.dtsi>
5. device-tested LK FDT / carve-outs — <https://github.com/hataketsu/mt6768-mainline-notes/blob/master/README.md>
6. U-Boot / LK handoff notes — <https://github.com/hataketsu/mt6768-mainline-notes/blob/master/uboot-patches/README.md>
7. LineageOS shared boot config — <https://github.com/kitw4y/android_device_xiaomi_mt6768-common/blob/lineage-20/BoardConfigCommon.mk>
8. LineageOS MT6768 fstab — <https://github.com/kitw4y/android_device_xiaomi_mt6768-common/blob/lineage-20/rootdir/etc/fstab.mt6768>
9. LineageOS lancelot board config — <https://github.com/LineageOS/android_device_xiaomi_lancelot/blob/lineage-20/BoardConfig.mk>
10. Xiaomi MT6768 vendor kernel (display/touch/keys/PMIC) — <https://github.com/smlinux/kernel_lancelot>, <https://github.com/Redmi-MT6768/android_kernel_xiaomi_mt6768>
11. Linux MTK watchdog driver — <https://github.com/torvalds/linux/blob/master/drivers/watchdog/mtk_wdt.c>
12. LineageOS lancelot wiki (keys, specs) — <https://wiki.lineageos.org/devices/lancelot/>
13. `fastboot boot` reports — <https://xdaforums.com/t/how-do-i-get-a-recovery-on-my-redmi-9-lancelot.4442047/>, <https://github.com/hataketsu/mt6768-mainline-notes>
