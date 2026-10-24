# lancelot hardware facts

Target: Xiaomi Redmi 9 (`lancelot`), MediaTek **MT6769V/CU** (Helio G80), part of the
MT6768 family (mainline names the SoC `mt6768` and the Xiaomi board `mt6769t-xiaomi-*`).

Every value below carries a **source** and a **status**:

| Status | Meaning |
| --- | --- |
| CONFIRMED | Stated explicitly by a primary source (mainline/LineageOS device tree, vendor kernel source, Linux driver whose binding matches the DT node). |
| HYPOTHESIS | Consistent with the family or inferred from a sibling board; must be measured on the phone before use. |
| UNKNOWN | No source found. Do not invent a value. |

The rule of this project: **no address, register or constant goes into code without a
row here.** When a shim measurement confirms or contradicts a row, update it and say
which boot measured it.

Last full review: 2026-10-24 (M0, desk research only; nothing measured on the phone yet).

## 1. SoC and CPU

| Fact | Value | Source | Status |
| --- | --- | --- | --- |
| SoC | MT6769V/CU (Helio G80), 12 nm | GSMArena Redmi 9 page; mainline `mt6768.dtsi` + `mt6769t-xiaomi-common.dtsi` | CONFIRMED |
| Cores | 2× Cortex-A75 + 6× Cortex-A55 | same DTS `cpus` node | CONFIRMED |
| ISA level | ARMv8.2-A, **no pointer authentication**, no Apple PAC | A75/A55 core design; same class as sweet (A76/A55) and beryllium (A75/A55) | CONFIRMED (design) |
| CPU `reg` (MPIDR affinity) | A55: `0x000, 0x100, 0x200, 0x300, 0x400, 0x500`; A75: `0x600, 0x700` | mainline `mt6768.dtsi`, `cpu@N` nodes | CONFIRMED (DT) |
| Clusters | `cpu-map`: cluster0 = cpu0..cpu5 (A55), cluster1 = cpu6..cpu7 (A75) | same | CONFIRMED |
| `enable-method` | `psci` on all eight | same | CONFIRMED |
| Live MPIDR readout | — | — | UNKNOWN (shim prints it) |

Porting note: unlike sweet (6 A55 of 8, A76 at 6-7) the big cores here are **6 and 7**,
so the "big cores at the end" assumption holds, but the split is 6 little + 2 big.

## 2. Interrupts, timer, PSCI

| Fact | Value | Source | Status |
| --- | --- | --- | --- |
| GIC | **GICv3**, `compatible = "arm,gic-v3"` | mainline `mt6768.dtsi`, `interrupt-controller@c000000` | CONFIRMED |
| GIC distributor | `0x0c000000`, size `0x40000` | same | CONFIRMED |
| GIC redistributor | `0x0c040000`, size `0x200000` (8 PEs) | same | CONFIRMED (size); stride HYPOTHESIS `0x40000` |
| ITS | not declared in the SoC DTS | same | UNKNOWN |
| Timer node | `arm,armv8-timer`, level-low | same | CONFIRMED |
| Timer PPIs | secure phys **13**, non-secure phys **14**, virtual **11**, hyp **10** | same | CONFIRMED (DT) |
| `CNTFRQ` | — | — | UNKNOWN. Family convention is 13 MHz (`system_clk` is 13 MHz in the DTS). **Measure `CNTFRQ_EL0` in the shim before using it.** |
| PSCI | `arm,psci-1.0`, `method = "smc"` | same | CONFIRMED (DT) |

Porting note: sweet/beryllium use Qualcomm PPI numbering (virt 19, phys 18). MTK uses
the ARM-standard 11/14 here, so the XNU timer PPI patch must be board-parameterised,
not just re-addressed.

## 3. Memory mapMeasured from the LK FDT of a **3 GiB** unit (hataketsu/mt6768-mainline-notes, see
sources at the bottom). **This phone is the 4 GiB SKU** (user, 2026-10-24), so expect an
additional bank above 4 GiB; the exact split is measured from the live FDT in M1.

| Fact | Value | Status |
| --- | --- | --- |
| Installed RAM | **4 GB** | CONFIRMED by the user (not yet read on the phone) |
| DRAM bank 0 (3 GiB unit) | `0x40000000` + `0xc0000000` (ends exactly at 4 GiB) | CONFIRMED on that unit |
| DRAM above 4 GiB | none on the 3 GiB unit; a 4 GiB unit is expected to report a second bank (typically `0x1_00000000` or `0x1_40000000`) | HYPOTHESIS for this SKU — measure in M1 |
| `BOARD_KERNEL_BASE` (Android image layout) | `0x40078000`, kernel load `0x40080000` | CONFIRMED (LineageOS BoardConfig) |
| LK FDT physical address | `0x4bc80000` (= `BOARD_KERNEL_BASE + BOARD_DTB_OFFSET`, also the boot tags address) | CONFIRMED (port notes) |
| Mainline DTS `memory@40000000` | `reg = <0x0 0x40000000 0x0 0x3e605000>` (~997 MiB) | CONFIRMED as DTS content; **do not** read it as the installed RAM size |

### Reserved / carve-out regions (LK FDT, start + size)

These must never be handed to XNU or DMA-ed into. Copy of the 18 regions reported by
the bootloader on the 3 GiB unit:

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
| `mblock-16-ccci` (modem-shared) | `0xae000000` | `0x10000000` |
| `mblock-5-atf-log-reserved` | `0xbfe00000` | `0x00040000` |
| `mblock-12-atf-ramdump-memory` | `0xbff80000` | `0x00080000` |
| `mblock-2-dramc-rk1` | `0xfffff000` | `0x00001000` |

Consequence for XNU: the usable contiguous window is small. On a 3 GiB unit the only
sane kernel region is **`0x80000000`–`0xae000000`** (768 MiB) minus `0x9f900000`/`0x9ffff000`,
or **`0x50000000`–`0x70000000`** (512 MiB, below TEE). This is *very* different from
sweet's 3.95 GiB: a 3 GiB phone will be RAM-starved and the plan commits to `cpus=1`
plus a small physmap first. Exact placement is decided from the live FDT in M2, not here.

## 3b. On-device read, 2026-10-24 (M1, partial)

Read with `fastboot` on the actual phone. No partition was written.

| Variable | Value | Note |
| --- | --- | --- |
| `product` | `lancelot` | CONFIRMED on device |
| `unlocked` | `yes` | bootloader already unlocked |
| `secure` | `no` | no secure boot enforcement |
| `hw-revision` | `ca00` | — |
| `max-download-size` | `0x8000000` (128 MiB) | sparse chunks must stay below this |
| `logical-block-size` / `erase-block-size` | 512 B / 512 KiB | — |
| `battery-voltage` | 3736 mV, `battery-soc-ok: yes` | phone in a healthy state |
| RAM | 4 GB (user) | second bank still to be measured |

Partition sizes seen before the read was truncated by the host pipeline (see below):
`boot` 64 MiB, `cache` 432 MiB, `cust` 1 GiB, `devinfo` 8 MiB, `dtbo` 16 MiB,
`exaid` 384 MiB, `ffu` 11.5 MiB, `flashinfo` 16 MiB, `gsort` 16 MiB, `gz1`/`gz2`
16 MiB each, `lk`/`lk2` 2 MiB each, `logo` 8 MiB, `md1img` 128 MiB, `minidump`
128 MiB, `nvram` 64 MiB, `otp` 43 MiB, `scp1`/`scp2` 6 MiB, `sgpt` 32 KiB,
`spmfw` 1 MiB, `sspm_1`/`sspm_2` 1 MiB, `super` 7 GiB, `tee1`/`tee2` 5 MiB,
`userdata` ~48.4 GiB.

**The list above is PARTIAL, not a full GPT**: the `getvar all` stream was cut by the
host's `head`, and a second enumeration (`fastboot oem device-info`) printed further
names (`proinfo`, `sec1`) that never appeared in the first one. In particular the
existence of a `recovery` partition is **UNRESOLVED** — it decides the M2 boot path
(see `docs/RECOVERY.md`). Re-read the full table before planning any write.

Operational findings, both reproduced on this unit:

- **`fastboot fetch <partition>` is not supported**: the host waits forever and no
  data arrives. There is no fastboot-side partition backup.
- **`fastboot boot` wedges LK**: the host wraps the file, starts `Sending 'boot.img'`
  and never completes; afterwards the bootloader enumerates on USB but stops
  answering `getvar` until it is power-cycled. Consistent with the two reports that
  the command is not implemented.

## 4. Boot chain

| Fact | Value | Source | Status |
| --- | --- | --- | --- |
| Chain | BROM → preloader → ATF (BL31) → LK (BL33) → `boot.img` | MTK platform; LK FDT shows ATF/TEE carve-outs | CONFIRMED (structure) |
| `fastboot boot` | **NOT supported**: LK answers `Unrecognized command boot` | XDA lancelot thread; hataketsu port notes ("crashes with any image, including the known-good LineageOS boot.img") | CONFIRMED, two independent reports |
| Boot image header | **v2**, page size 2048 | LineageOS `mt6768-common/BoardConfigCommon.mk`, TWRP `a14_lancelot` | CONFIRMED |
| Load addresses | kernel `0x40080000`, ramdisk `0x47c80000`, second `0x100000000`, tags `0x4bc80000`, dtb `0x4bc80000` | same (`BOARD_KERNEL_BASE 0x40078000` + offsets) | CONFIRMED |
| Stock cmdline prefix | `bootopt=64S3,32N2,64N2` | same | CONFIRMED |
| DTB in boot.img | yes (`BOARD_INCLUDE_DTB_IN_BOOTIMG`), plus a separate `dtbo` partition | TWRP BoardConfig | CONFIRMED |
| Kernel compression | LK decompresses the kernel slot; a raw ARM64 `Image` crashed LK in the mainline port, gzip `Image.gz` is what the working trees ship | hataketsu port notes | CONFIRMED (report); we ship our own raw payload format and **must** test whether LK accepts an uncompressed "kernel" |
| A/B slots | **no** (single `boot`, `recovery`) | LineageOS BoardConfig + fstab | CONFIRMED |
| Dynamic partitions | yes (`super` holds system/system_ext/vendor/product) | LineageOS fstab/BoardConfig | CONFIRMED |
| Key combos | fastboot = Vol- + Power, recovery = Vol+ + Power | LineageOS lancelot wiki | CONFIRMED |

### Partitions (from the shared MT6768 fstab)

`boot`, `recovery`, `dtbo`, `vbmeta`, `vbmeta_vendor`, `vbmeta_system`, `super`,
`userdata`, `metadata`, `cache`, `misc`, `para`, `frp`, `protect1`, `protect2`,
`nvdata`, `nvcfg`, `persist`, `nvram`, `proinfo`, `expdb`, `boot_para`, `lk`, `lk2`,
`tee1`, `tee2`, `logo`, `scp1`, `scp2`, `sspm_1`, `sspm_2`, `md1img`, `md1dsp`,
`gz1`, `gz2`, `spmfw`, `otp`, `seccfg`.

`userdata` is reached through `/dev/block/platform/bootdevice/by-name/userdata`
(never a hard-coded `mmcblk0pN`; the numbering differs between firmware revisions).

Build-config sizes (not a verified GPT dump): `boot` 64 MiB, `recovery` 64 MiB,
`dtbo` 16 MiB, `super` 7.0–8.5 GiB depending on the tree.

**Never write:** `preloader`, `lk`/`lk2`, `tee1`/`tee2`, `seccfg`, `nvram`, `nvdata`,
`nvcfg`, `protect1`/`protect2`, `proinfo`, `persist`, `md1img`/`md1dsp`, `frp`,
`metadata`, and anything calibration/identity related. `userdata` is the only
write target by default; `boot`/`recovery` only with a verified backup and an
explicit confirmation (see `docs/RECOVERY.md`).

## 5. Storage

| Fact | Value | Source | Status |
| --- | --- | --- | --- |
| eMMC | 5.1, 8-bit, non-removable, DDR/HS200/HS400 at 1.8 V, max 200 MHz | vendor `cust_mt6768_msdc.dtsi` | CONFIRMED |
| eMMC controller | **MSDC0**, base `0x11230000` | MTK family convention; the Lancelot DT did not print the base | HYPOTHESIS — confirm from the stock DTB |
| eMMC clocks | `CLK_IFR_MSDC0_SRC`, `CLK_IFR_MSDC0`, `CLK_IFR_FAES_FDE` from `infracfg_ao`; source `MSDC0_CLKSRC_400MHZ` | vendor MSDC DTS | CONFIRMED |
| CQE / CQHCI | not established for lancelot | — | UNKNOWN |
| SD card | MSDC1, 4-bit, card-detect GPIO 18, `no-mmc` | vendor MSDC DTS | CONFIRMED |

This replaces the whole UFS driver of sweet/beryllium. It is the largest single block
of new kernel code in the plan (M4).

## 6. Display

| Fact | Value | Source | Status |
| --- | --- | --- | --- |
| Panel | `hx83112b_fhdp_dsi_cmd_auo_rt5081_drv` → Himax HX83112B bridge, AUO panel, RT5081 backlight | vendor `k69v1_64.dts` / lancelot DT overlay | CONFIRMED |
| Resolution | 1080 × 2340 | same | CONFIRMED |
| Mode | **command mode** (`_dsi_cmd_`) | same | CONFIRMED |
| Bootloader framebuffer | `atag,videolfb` under `/chosen`: base **`0x7e605000`**, `vramSize = 0x017e8000` (~25 MiB) | same | CONFIRMED (metadata) |
| Framebuffer reserved region | `0x7dcb0000` + `0x02250000` (the base above sits inside it) | LK FDT carve-outs | CONFIRMED |
| Pixel format / stride | — | — | UNKNOWN — read it from the live FDT/`/chosen` in the shim; do not assume `w*4` |
| Continuous scanout after boot | — | — | UNKNOWN. Command-mode DSI usually needs a refresh kick, so M2 must prove whether the LK framebuffer keeps being scanned out or the panel holds the last frame in GRAM |
| MMSYS | `0x14000000` (mutex `0x14016000`) | vendor `mt6768.dts` | CONFIRMED |
| OVL0 / DSI0 | `0x1400b000` / `0x14014000` | family-level | HYPOTHESIS — confirm in the stock DTB |
| Panel timings | porches, pixel clock, DSI lanes | — | UNKNOWN (vendor `lcm` driver in the kernel source) |

iOS presents an iPhone X canvas (1125×2436). Lancelot is 1080×2340 (19.5:9 vs 19.5:9,
so a straight scale is possible with small letterboxing). Decision: render at
`750×1624 @2x` like beryllium's `--display-scale 2` and scale into the panel, or
scale pixel-for-pixel. Deferred to M5; it is a `mkadt.py` value, not a kernel change.

## 7. Input

| Fact | Value | Source | Status |
| --- | --- | --- | --- |
| Touch bus | **SPI0** node, compatible list covering Novatek NVT and Focaltech FTS | vendor `lancelot.dts` | CONFIRMED (bus) |
| Touch controller | NT36672-class vs FT8719P | node lists both variants | UNKNOWN — resolve from the stock DTB / live FDT in M1 |
| Touch IRQ / reset | IRQ GPIO 1 (falling edge EINT), reset GPIO 92 | vendor `lancelot/cust.dtsi` | CONFIRMED |
| Touch supply | PMIC rail `vldo28` | same | CONFIRMED |
| SPI0 controller base | — | — | UNKNOWN — from the stock DTB |
| Keys | MTK keypad block **KP** `0x10010000` (GIC SPI 73), software power key code 116 / hardware index 8 | vendor `mt6768.dts`, `cust.dtsi` | CONFIRMED (block); physical volume-key routing UNKNOWN |
| PMIC | **MT6358-compatible** behind **PWRAP** `0x1000d000` (GIC SPI 194, level-high), PIO-routed interrupt | vendor `mt6768.dts` | CONFIRMED |
| Charger | external **SMB1351** at I2C7 address `0x55`, interrupt GPIO 20 | vendor `lancelot.dts` | CONFIRMED (node); fuel gauge UNKNOWN |
| Battery gauge | MT6358 BATON/FG or external | — | UNKNOWN |

Battery can start static (100 %, charging icon) exactly as sweet did; a real gauge
is M6+ polish.

## 8. Debug channels

| Channel | Value | Status |
| --- | --- | --- |
| UART0 | `0x11002000`, SPI 112; `serial0` alias enabled in the Xiaomi common DTS | CONFIRMED (DT). Physical access on the lancelot PCB: UNKNOWN |
| UART1 | `0x11003000`, SPI 113, disabled by default | CONFIRMED (DT) |
| USB | MTK MUSB-style controller (not DWC3); `fastboot` enumerates at USB 2.0 | HYPOTHESIS — confirm from the stock DTB |
| Framebuffer console | same approach as sweet: draw into the LK framebuffer | planned |
| Persistent log ring | same approach as sweet: reserved area at the end of `userdata` | planned (needs MSDC write, M4) |
| ramoops / pstore | `0x4d010000` + `0xe0000`, Linux console at `0x4d05f000` | CONFIRMED (LK FDT) — a second log channel worth using before MSDC works |

There is no evidence of an accessible debug UART without opening the case; the
framebuffer remains the primary console (the shim can draw before XNU runs).

## 9. Watchdog

| Fact | Value | Source | Status |
| --- | --- | --- | --- |
| Block | TOPRGU `watchdog@10007000`, size `0x100` | mainline `mt6768.dtsi` | CONFIRMED |
| Registers | `WDT_MODE 0x00`, `WDT_RST 0x08`, `WDT_SWRST 0x14` | `drivers/watchdog/mtk_wdt.c` | CONFIRMED |
| Disable | clear `WDT_MODE` bit 0 (EN) while writing `0x22000000` (key) into the upper bits | same driver `mtk_wdt_stop()` | CONFIRMED |
| `WDT_RST` reload key | `0x1971` | same | CONFIRMED |
| `WDT_SWRST` key | `0x1209` (software reset, **not** a disable) | same | CONFIRMED |

The shim must stop the watchdog before doing anything slow (first action after EL1),
otherwise the port can reset mid-probe and look like a random bootloop.

## 10. Clocks and power blocks (addresses only)

| Block | Base | Size | Source |
| --- | --- | --- | --- |
| topckgen | `0x10000000` | `0x1000` | mainline `mt6768.dtsi` |
| infracfg_ao | `0x10001000` | `0x1000` | same |
| pericfg | `0x10003000` | `0x1000` | same |
| pinctrl | `0x10005000` | — | same |
| scpsys / spm | `0x10006000` | `0x1000` | same |
| apmixedsys | `0x1000c000` | `0x1000` | same |
| pwrap | `0x1000d000` | `0x1000` | same |
| mcucfg | `0x0c530000` | `0x10000` | same |

Mainline lancelot needs `clk_ignore_unused pd_ignore_unused` with the stock
bootloader, i.e. Linux relies on firmware-programmed clocks/power. A bare-metal shim
should therefore **preserve** what LK set up and add votes only where a measurement
proves something is off (display, MSDC, touch rail).

## Sources

1. mainline MT6768 SoC DTS — <https://github.com/mt6768-mainline/linux/blob/6.18/arch/arm64/boot/dts/mediatek/mt6768.dtsi>
2. mainline Xiaomi common DTS — <https://github.com/mt6768-mainline/linux/blob/6.18/arch/arm64/boot/dts/mediatek/mt6769t-xiaomi-common.dtsi>
3. mainline lancelot panel include — <https://github.com/mt6768-mainline/linux/blob/6.18/arch/arm64/boot/dts/mediatek/mt6769t-xiaomi-lancelot-tianma.dtsi>
4. device-tested LK FDT and carve-outs — <https://github.com/hataketsu/mt6768-mainline-notes/blob/master/README.md>
5. U-Boot / LK handoff notes — <https://github.com/hataketsu/mt6768-mainline-notes/blob/master/uboot-patches/README.md>
6. LineageOS shared boot config — <https://github.com/kitw4y/android_device_xiaomi_mt6768-common/blob/lineage-20/BoardConfigCommon.mk>
7. LineageOS MT6768 fstab — <https://github.com/kitw4y/android_device_xiaomi_mt6768-common/blob/lineage-20/rootdir/etc/fstab.mt6768>
8. LineageOS lancelot board config — <https://github.com/LineageOS/android_device_xiaomi_lancelot/blob/lineage-20/BoardConfig.mk>
9. TWRP lancelot tree — <https://github.com/Shas45558/twrp_tree_mt6768/blob/a14_lancelot/BoardConfig.mk>
10. Xiaomi MT6768 vendor kernel (display/touch/keys/PMIC) — <https://github.com/smlinux/kernel_lancelot> and <https://github.com/Redmi-MT6768/android_kernel_xiaomi_mt6768>
11. Linux MTK watchdog driver — <https://github.com/torvalds/linux/blob/master/drivers/watchdog/mtk_wdt.c>
12. LineageOS lancelot wiki (keys, unlock) — <https://wiki.lineageos.org/devices/lancelot/>
13. `fastboot boot` failure reports — <https://xdaforums.com/t/how-do-i-get-a-recovery-on-my-redmi-9-lancelot.4442047/>, <https://github.com/hataketsu/mt6768-mainline-notes>
