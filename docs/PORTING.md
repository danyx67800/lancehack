# Porting delta: ios-sweet → Bergamota → lancehack

This is the M0 analysis of what has to change to move the project from a Qualcomm
SM7150 phone (`sweet`) / SDM845 phone (`beryllium`) to a MediaTek MT6768-family
phone (`lancelot`). It is based on the actual git history, not on the READMEs.

Base: `upstream/main` at `45263be` (85 patches, through `0081-sweet-usb-link-state`).
Reference fork: `bergamota/main` at the `bergamota` remote. Merge base of the two:
`11da58b`.

## 1. Size of the delta

`git diff --stat 11da58b bergamota/main` (Poco F1 work, ~5 days):

| Area | Files | + | − | What |
| --- | --- | --- | --- | --- |
| `xnu/` (patches) | 54 | 15 606 | 0 | 0082–0141: beryllium board, UFS traces, USB, touch, BT, GPU, battery, reboot |
| `apps/` | 45 | 4 975 | 0 | a macOS SwiftUI control app (not needed for lancelot) |
| `tools/` | 46 | 3 799 | 26 | `mkadt --board`, macOS root-image builder, sparse tool, SSH/dropbear, log pullers |
| `docs/` | 19 | 3 755 | 0 | static review, display HW, SSH over USB, GPU/WiFi plans |
| `shim/` | 10 | 244 | 48 | `board.h`, boot.img v1 path, FDT/console tweaks |
| `NOTES.md` / `README.md` / `AGENTS.md` | 3 | 151 | 5 | beryllium handoff and status |

So: **~15k lines of kernel patch and ~4k lines of tooling per new SoC**, but only a
small part of it is Qualcomm-independent. Two facts matter more than the line count:

1. The **architecture of the fork is reusable** — one kernel, board selected at
   runtime from the device tree; one shim, board selected at compile time.
2. Everything below `bsd/dev/` is **per-SoC hardware** and has to be written fresh.

## 2. Machinery worth taking from Bergamota (in this order)

| Bergamota path | What it gives us | Take now? |
| --- | --- | --- |
| `shim/board.h` | One `#if defined(BOARD_X)` block per phone with FB base/geometry, IMEM/restart reason, GIC, storage base | **Yes** — extend with a `BOARD_LANCELOT` block |
| `shim/build.sh` `BOARD=` case | Per-board boot.img header version (v1 for SDM845 ABL, v2 for sweet) and DTB handling | **Yes** — add `lancelot` → v2 + DTB + MTK base addresses |
| `tools/mkadt.py` `BOARDS = {...}` + `--board` | Per-board framebuffer, compatibles, CPU layout, frequencies, `/chosen` extras | **Yes** — add a `lancelot` entry |
| `bsd/dev/sweet_board.h` (patch 0082) + runtime board check | Kernel picks MMIO from the ADT root `compatible` instead of at compile time, so one kernel can serve several phones | **Yes**, but only once a second board is actually booting; M3 can hard-code lancelot first |
| keeping `xiaomi,sweet` in the ADT `compatible` list | `SweetPlatformExpert` matches by name; a new board does not need a new platform expert on day one | **Yes** (cheap, big win) |
| `tools/mkiosroot-macos.sh` | Builds the HFS+ iOS root **on macOS** with `hdiutil`/`newfs_hfs` instead of Linux + `apfs-fuse` | **Yes** — this is the single most useful tool for our host |
| `tools/img2simg.py` | Pure-Python raw→sparse (and back) converter; macOS `fastboot` fails on a 20 GiB raw image ("Failed reading from userdata") | **Yes** |
| `tools/sweet-log-pull.sh`, `sweet-ios-pull.sh`, `smp-sym.py` | Kernel-console ring and iOS unified-log pullers, SMP sample symbolicator | Later (after M4) |
| `docs/STATIC_REVIEW.md` process | A second agent reviews each patch before a CI run | **Yes** — adopt the process, not the file |
| USB CDC-ECM + live log (`tools/sweet-usbnet.sh`, patches 0074–0081) | Live kernel log over USB | Later (M7): lancelot has an MTK MUSB-style controller, the DWC3 code does not apply |
| MDP page flip / QSEED3 scaler / GPU / BT / WiFi patches | Qualcomm blocks | **No** |

Numbers to respect when cherry-picking: our base is at patch `0081`, so **our own
series starts at `0082`**. Bergamota also uses `0082`+. Do not merge its series; lift
individual hunks and renumber.

## 3. What must be written for lancelot

| Component | sweet / beryllium | lancelot | New work |
| --- | --- | --- | --- |
| Boot loader | ABL accepts `fastboot boot` | **LK rejects `fastboot boot`** (`Unrecognized command boot`) | Boot path decision, see §4 |
| Image format | boot.img v2 (sweet) / v1 + appended DTB (beryllium) | boot.img **v2**, 2048-byte pages, DTB in header, separate `dtbo` | `shim/build.sh` case + `mkbootimg` flags |
| Storage | own UFS host driver (~4k lines) | **eMMC over MSDC0** | New driver: controller init, clocks via `infracfg_ao`, ADMA, HS400 → then HFS+ root in `userdata` |
| Interrupts | GICv3, timer PPIs 19/18 (Qualcomm numbering) | GICv3, timer PPIs **13/14/11/10** | Parameterise the PPI numbers in the timer patch instead of hard-coding |
| Timer | 19.2 MHz | `CNTFRQ` unknown (13 MHz family convention) | Measure in the shim; pass to XNU |
| PSCI | `smc` | `smc` | Reuse |
| CPU topology | 6×A55 (+2×A76 unused) / 4+4 | 6×A55 (`0x0..0x500`) + 2×A75 (`0x600`,`0x700`) | `mkadt.py` CPU nodes; SMP patch unchanged apart from MPIDR |
| Memory | 3.95 GiB above 4 GiB | 3 GiB ends at 4 GiB on the base SKU, 18 carve-outs | New physmap plan; likely `cpus=1` and a much smaller kernel heap |
| Watchdog | none seen / PMIC | **TOPRGU** can reset the SoC in seconds | Shim: stop it before anything slow |
| Console | FB at `0x9c000000`, persistent ring in `userdata` | FB at `0x7e605000` (inside `0x7dcb0000`+34 MiB), command-mode panel | Shim/FB code with the MTK base; prove scanout in M2 |
| Display | Qualcomm MDP / ABL continuous splash | MTK DISP/DSI, **command mode**, needs a refresh kick | New `AppleCLCD` backend writing/poking the MTK panel |
| Touch | Goodix GT9896 on GENI I²C | Novatek/Focaltech on **SPI0**, IRQ GPIO 1, reset GPIO 92, `vldo28` | New bus driver + protocol; chip variant unknown until M1 |
| Keys | SPMI PMIC (Qualcomm) | MTK **KP** `0x10010000` + MT6358 through **PWRAP** `0x1000d000` | New key driver |
| Battery | PMIC / fuel gauge | SMB1351 charger + unknown gauge | Static battery first |

## 4. The boot path problem (the one real blocker)

`fastboot boot` is the mechanism both existing ports rely on: the shim runs from RAM
and **nothing is written**. Two independent lancelot reports say LK does not implement
it:

- XDA lancelot thread: `Booting FAILED (remote: 'Unrecognized command boot')`
- `hataketsu/mt6768-mainline-notes`: "fastboot boot is broken on this device. It
  crashes with any image, including the known-good LineageOS boot.img."

So one partition has to be written to get our code executed. Options, with risk:

| Option | What it writes | Android still boots? | Risk | Recovery |
| --- | --- | --- | --- | --- |
| **A. Flash the shim into `recovery`** (recommended) | `recovery` (64 MiB) only | Yes — normal boot untouched | Recovery is lost until restored; a bad shim just fails to run | `fastboot flash recovery stock_recovery.img`, or restore from backup |
| B. Flash the shim into `boot` | `boot` (64 MiB) | No — Android does not boot while ours is there | Phone only boots the shim; a bad shim means fastboot only | `fastboot flash boot stock_boot.img` |
| C. BROM / mtkclient | DA in RAM, no partition write | Yes | BROM may be locked (SBC/SLA/DAA on this SoC); no documented "boot this image" command | Depends on DA; can be destructive if misused |
| D. Do nothing | nothing | Yes | No progress | — |

Option A keeps the Android installation intact and limits the blast radius to a
partition that is not needed for normal boot. It still needs: a verified backup of
`recovery` **and** `boot`, the stock ROM for the exact firmware version, and the user's
explicit go-ahead on the day. Option B is the classic Magisk-style route and is
strictly more invasive.

Whatever is chosen, M1 produces the backups and `docs/RECOVERY.md` before any write.

## 5. Milestone mapping

| Milestone | Concrete first step in this fork |
| --- | --- |
| M1 (data from the phone) | `fastboot getvar all`, `fastboot oem device-info`, dump `boot`/`recovery`/`dtbo`/`vbmeta`, read the live FDT → fill the UNKNOWN rows of `docs/lancelot-hardware.md` |
| M2 (minimum boot) | `BOARD_LANCELOT` in `shim/board.h`, v2 image in `shim/build.sh`, TOPRGU stop, staged colour bars, FDT dump to the screen |
| M3 (XNU console) | `mkadt.py --board lancelot`, GIC/timer PPI parameterisation, framebuffer console, `rd=md0` RAM-disk first (no storage needed) |
| M4 (storage/root) | MSDC0 read, then write; HFS+ root in `userdata`; persistent log ring at the end of `userdata` |
| M5 (userland/display) | SMP on the A55s, `AppleCLCD` backend for MTK DISP **with the command-mode refresh kick**, backboardd/SpringBoard |
| M6 (input) | SPI touch (chip TBD), KP/PWRAP keys, static battery |
| M7 (stretch) | USB (MUSB) debug, A75 cores, GPU, then iOS 18 |

## 6. Rules that come out of this analysis

- One kernel, board chosen from the ADT: keep the bergamota pattern, but do not add
  a second board to the same kernel until the first one boots.
- Never hard-code a Qualcomm number (PPI 19, 19.2 MHz, UFS base) in a shared file.
  Every board-dependent constant goes into `shim/board.h`, `mkadt.py`'s `BOARDS`, or
  `sweet_board.h`.
- The MTK watchdog is disabled in the first 100 instructions of the shim, or every
  later measurement is unreliable.
- Nothing is written to the phone until `docs/RECOVERY.md` exists with a checksummed
  backup list.
