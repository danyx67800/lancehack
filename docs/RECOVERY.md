# Recovering lancelot back to Android

Written **before** the first write to the phone, as required by the project rules.

## 0. The assumption that makes all of this recoverable

The chain is **BROM → preloader → ATF → LK → boot.img**. Everything this project
writes lives at the *end* of that chain:

- the shim goes into **`recovery`** (64 MiB); Android's `boot` is never touched;
- the iOS root goes into **`userdata`**.

`preloader`, `lk`/`lk2`, `tee1`/`tee2`, `seccfg`, `nvram`, `nvdata`, `nvcfg`,
`protect1`/`protect2`, `proinfo`, `persist`, `persistbak`, `md1img`, `frp`,
`metadata`, `efuse`, `sec1`, `otp` are **never written**. As long as LK and the
preloader are untouched, fastboot keeps working and a stock/custom ROM restores every
partition. Here "brick" means "fastboot still works but Android does not boot", which
is recoverable.

Two ways to actually lose the phone: writing the preloader/LK, and a BROM operation
with the wrong DA/scatter. Both are out of scope and require a separate decision.

## 1. Materials already in place (2026-10-24)

The phone runs **AfterlifeOS (Android 16, user build)** — not MIUI — so no Xiaomi
fastboot ROM is involved. AfterlifeOS distributions go through the project's Telegram
bot (`t.me/AfterlifeOS_hrdbot`), which cannot be driven from here; the restore set is
therefore taken from **LineageOS 24.0 for lancelot (build 20261024)**, which is the
same board and a normal, flashable Android.

Downloaded to `backup/lancelot-lineage/` (git-ignored) and checksummed:

| File | SHA-256 |
| --- | --- |
| `boot.img` (64 MiB) | `4daa83bf3f57709c0386d6517ef16e2377a5ab2a0ac65ec423d4da2165653219` |
| `dtbo.img` (16 MiB) | `3c39c4ec1a0526f4dedfc193c128b30aa1de8bce7fd33877093aa885935678c3` |
| `recovery.img` (64 MiB) | `61b4b257624cd0db2626c8e4fedf9467cf40c4b8a629d72443115b5682550355` |
| `vbmeta.img` (4 KiB) | `d2466db462336e585981ceb0175ce931fee2ca4609cec1b4d973a427173fcda0` |

`boot.img`/`dtbo.img` also gave us the definitive DTB and overlay now recorded in
`docs/lancelot-hardware.md`.

**Gap:** this is LineageOS recovery, not the AfterlifeOS one that is on the phone now.
Restoring `recovery` with it leaves Android fully working (recovery is independent of
`boot`), but it is not byte-identical to the shipped one. If an exact restore is
wanted, the user can fetch the AfterlifeOS package via the official bot and drop it in
`backup/`.

## 2. Device-specific partition backup: not possible before the first write

Three independent blockers, all verified on 2026-10-24:

1. `fastboot boot` does not work on this phone (LK stalls and wedges), so no temporary
   recovery can be run.
2. `fastboot fetch` is not implemented, so fastboot cannot read partitions at all.
3. The ROM is a **`user` build**: `adb root` is refused and `/proc/cmdline`,
   `/proc/device-tree` are denied to the `shell` user by SELinux.

The only remaining read path would be BROM/mtkclient (access may be refused on units
with SBC/SLA/DAA) or flashing a recovery first.

Decision recorded here: because the project **never writes** any device-specific
partition (`persist`, `nvram`, `nvdata`, `proinfo`, `seccfg`, `frp`, `md1img`, …), a
device-specific dump is not a prerequisite for the planned first write (`recovery`).
What is required — and is in place — is a known-good image to put back into the
partition being written. If the project ever needs to touch a device-specific
partition, the backup requirement comes back and that step stops until it is solved.

## 3. Undo the shim only (the common case)

Fastboot is reachable (Vol- + Power), Android's `boot` was never touched, and the
shim lives in `recovery`:

```sh
fastboot flash recovery backup/lancelot-lineage/recovery.img
fastboot reboot
```

This restores a working recovery **without** touching user data. If the user wants
the AfterlifeOS recovery back, flash the `recovery.img` from that package instead
(same partition, same procedure).

## 4. Full restore of Android

This **erases `userdata`** — that is, the iOS installation and anything Android stored
there.

1. Get a flashable package for lancelot (AfterlifeOS from its bot, or LineageOS whose
   `boot.img`/`dtbo.img`/`recovery.img`/`vbmeta.img` are already in `backup/`).
2. Flash in the order the package documents; for LineageOS that is `recovery`, then
   boot into it, then the ROM zip (which writes `boot`, `dtbo`, `super`, `vbmeta`).
3. First boot can take several minutes. If it bootloops, let it finish once before
   intervening.
4. Do **not** relock the bootloader (`fastboot oem lock`) while any custom image is
   present.

## 5. When only BROM is left

If the screen stays black and neither fastboot nor recovery responds:

1. Do not keep pressing buttons; disconnect and let the battery settle.
2. Try Vol- + Power with the cable connected for 10 s; check the USB device list for
   the MediaTek BROM VID/PID.
3. BROM access on MT6768 may require authentication (SBC/SLA/DAA); if `mtkclient`
   refuses, stop — do not try a different DA blindly.
4. A BROM/EDL operation is a **separate decision** with its own risk review; it can
   write the preloader and is the only step here that can make things worse.

## 6. Rules

- Backups or a known-good replacement image first; checksums always.
- No write outside `userdata` without: the replacement image available, a written
  restore step, and an explicit confirmation in the moment.
- No `fastboot oem lock`, ever, while this project is installed.
- Never flash `lk.img`, `preloader` or `seccfg`.
