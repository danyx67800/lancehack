# lancehack

**Apple's iOS 16 on a Xiaomi Redmi 9 (`lancelot`, MediaTek Helio G80 / MT6768).**

`lancehack` is a fork of [ios-sweet](https://github.com/dipa-studio/ios-sweet), which
runs the open-source XNU kernel (xnu-8796, the iOS 16 generation) under the
**unmodified iPhone X iOS 16.7.x userland** on the Redmi Note 10 Pro (Qualcomm SM7150).
The porting structure follows [Bergamota](https://github.com/ry-diffusion/bergamota)
(`ios-beryllium`), which did the same for the Poco F1 (SDM845).

The target here is a MediaTek phone: different boot chain, eMMC instead of UFS,
GICv3 with ARM-standard timer PPIs, a command-mode DSI panel and an MTK watchdog.
It is **not** a Qualcomm port with the addresses changed.

> Status: **M0 (research and setup) complete — nothing boots yet, and nothing has
> been read from or written to a phone.** This README describes the intended build
> and flash path; commands marked *(planned)* do not exist in the tree yet.

## Project status

| Milestone | State |
| --- | --- |
| M0 — fork, environment, upstream/delta analysis, hardware matrix | **done** (2026-10-24) |
| M1 — data from the phone: bootloader state, backups, stock `boot`/`recovery`/`dtbo`/`vbmeta`, live FDT | next — needs the phone |
| M2 — shim executes on lancelot and draws staged colour bars | not started |
| M3 — XNU console (GIC/timer, ADT, framebuffer console, QEMU first) | not started |
| M4 — eMMC/MSDC driver, HFS+ root in `userdata`, launchd | not started |
| M5 — SMP, display backend, backboardd + SpringBoard home screen | not started |
| M6 — touch, keys, static battery | not started |
| M7 — USB debug, A75 cores, GPU, iOS 18 | not started |

What "iOS booted" means here: the phone reaches the iOS home screen, unlocks, and
touch and the hardware buttons work.

## What works

On the parent project (Redmi Note 10 Pro), the same userland reaches the iOS home
screen, unlocks, opens Settings, Control Center and the notification centre, with
touch, power and volume keys, at a software-rendered 20–30 fps.

**On lancelot: nothing yet.** No line of this fork has run on the phone.

## What does not work

Inherited limitations of the userland approach (true for any board): no Wi-Fi,
Bluetooth, cellular, audio, camera, sensors, GPS, SEP/Face ID; no GPU acceleration;
permissive code signing and sandbox; no App Store or Apple ID. iOS installs into
`userdata` and **erases Android's user data**.

Lancelot-specific unknowns that block milestones are tracked in
[`docs/lancelot-hardware.md`](docs/lancelot-hardware.md).

## The three things that make lancelot different

1. **`fastboot boot` does not exist.** MediaTek's LK answers
   `Booting FAILED (remote: 'Unrecognized command boot')`, so the "run from RAM,
   write nothing" workflow of ios-sweet and Bergamota is unavailable. Booting our code
   requires writing one partition. The recommended route is to flash the shim into
   `recovery` (64 MiB, not needed for a normal Android boot), which keeps Android
   bootable on the untouched `boot` partition. It still needs a verified backup, the
   stock ROM for the exact firmware, and the user's explicit go-ahead. See
   [`docs/RECOVERY.md`](docs/RECOVERY.md).
2. **Memory is tight and fragmented.** A 3 GiB unit reports `0x40000000` +
   `0xc0000000` and no bank above 4 GiB, with 18 fixed carve-outs (ATF, TEE, modem,
   framebuffer, SCP, ccci…). There is no 3.95 GiB window like sweet's: the largest
   sane kernel region is about 768 MiB, so bring-up starts at `cpus=1`.
3. **Storage is eMMC, not UFS.** The whole UFS driver is replaced by an MSDC0 driver
   (M4) — the largest single block of new code.

## How the boot will work

```
BROM -> preloader -> ATF (BL31) -> LK (BL33, Xiaomi) -> boot.img slot
   (M2) shim: stop TOPRGU, own MMU, read the live FDT at 0x4bc80000, draw stages,
        load the payload
   (M3) XNU Mach-O + generated Apple Device Tree + boot-args, console on the LK
        framebuffer at 0x7e605000
   (M4) MSDC0 -> HFS+ root in userdata -> dyld + shared cache -> launchd
   (M5) backboardd + SpringBoard on the MTK command-mode panel
```

There is no confirmed accessible debug UART. The framebuffer is the console (the
shim can draw before XNU exists), plus a persistent log ring at the end of `userdata`
once eMMC writes work, and ramoops/pstore (`0x4d010000`) as a second channel.

## Building

### Kernel

The kernel builds in GitHub Actions on macOS runners with
[blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build), exactly as
in the parent project:

```sh
gh workflow run xnu.yml -R YOU/lancehack
gh run download RUN_ID -R YOU/lancehack -n xnu-kernel -D xnu/out
```

Our patches continue the series from `0082`; `tools/xnu-cc.py` replays a single
compile command from a CI log so a patch can be checked before spending a run.

### iOS root image

The iPhone X (iPhone10,3) iOS 16.7.16 (20H392) IPSW is downloaded from Apple and
stays local. `tools/mkiosroot-macos.sh` (taken from Bergamota) builds the case-sensitive
journaled HFS+ image with `hdiutil`/`newfs_hfs` on macOS; `tools/img2simg.py` converts
it to sparse, because fastboot on macOS fails on a 20 GiB raw image.

```sh
sudo tools/mkiosroot-macos.sh OS.dmg SystemOS.dmg AppOS.dmg ios16-root.img 20g
python3 tools/img2simg.py ios16-root.img ios16-root.simg
```

### Boot image *(planned — the `lancelot` board entry does not exist yet)*

```sh
python3 tools/mkadt.py shim/out/lancelot.adt --board lancelot --highmem
python3 tools/mkpayload.py xnu/out/kernel.development.sweet shim/out/lancelot.adt \
    shim/out/payload.bin --cmdline 'debug=0xc serial=3 cpus=1 keepsyms=1 ...'
BOARD=lancelot shim/build.sh ../shim/out/payload.bin   # boot.img v2, pages 2048,
                                                       # DTB in header
```

### Flashing *(not authorised yet)*

```sh
fastboot -S 512M flash userdata ios16-root.simg   # erases Android user data
fastboot flash recovery shim/out/shim-boot-lancelot.img   # the boot path, once chosen
```

Keys: fastboot = **Volume down + Power**, recovery = **Volume up + Power**.

## Risks

- Writing any partition can leave the phone without Android. The preloader, LK, TEE
  and all calibration/identity partitions are never written; as long as they are
  intact, fastboot keeps working and a stock fastboot ROM restores everything.
- Unlocking the bootloader wipes data and may have a waiting period.
- Anti-rollback status for lancelot is unknown: treat downgrades as dangerous.
- The shim runs before any OS: a bad shim means a phone that only reaches fastboot.
- Security is off by design (AMFI, sandbox, keybag stubs). Do not put personal data
  on the iOS installation.

## Repository layout

| Path | What |
| --- | --- |
| `docs/lancelot-hardware.md` | Every hardware fact with source and status (confirmed / hypothesis / unknown) |
| `docs/PORTING.md` | Delta ios-sweet → Bergamota → lancehack, and what has to be written |
| `docs/RECOVERY.md` | Backups, restore path, and the preconditions for any write |
| `docs/QEMU_TESTING.md` | QEMU smoke tests for kernel-only changes |
| `shim/` | Bare-metal boot shim (C + asm) packed as an Android boot.img |
| `xnu/patches/` | XNU patch series, applied in order by CI |
| `tools/` | ADT/payload builders, root-image builders, log pullers, QEMU harness |
| `NOTES.md` | Engineering log: every boot, crash and decision |

Not in git: partition backups, live device trees, recovery images, Apple firmware,
root images, downloaded or generated build trees.

## Kernel patches

Upstream sweet's series (`0001`–`0081`) is inherited unchanged and documented in
[`NOTES.md`](NOTES.md) and the [upstream README](https://github.com/dipa-studio/ios-sweet).
Our own series starts at `0082`:

| # | What |
| --- | --- |
| 0082+ | Reserved for the lancelot board: shim/ADT plumbing first, then timer/GIC parameterisation, MSDC, display, input |

## Credits

`lancehack` is a fork of [ios-sweet](https://github.com/dipa-studio/ios-sweet) by
dipa-studio (MIT), and follows the porting work of
[Bergamota / ios-beryllium](https://github.com/ry-diffusion/bergamota) by
ry-diffusion. Both projects' documentation and tooling are the foundation of this one.

Built on Apple's open-source releases (xnu, IOHIDFamily, libpthread, hfs),
[BearSSL](https://bearssl.org), [blacktop/darwin-xnu-build](https://github.com/blacktop/darwin-xnu-build),
[ipsw](https://github.com/blacktop/ipsw), and the MT6768 mainline port
([mt6768-mainline/linux](https://github.com/mt6768-mainline/linux),
[hataketsu/mt6768-mainline-notes](https://github.com/hataketsu/mt6768-mainline-notes)),
LineageOS device trees and MediaTek/Xiaomi GPL kernel sources (used as register
documentation only). iOS itself is Apple's and is not distributed here.

MIT licensed — see [`LICENSE`](LICENSE).
