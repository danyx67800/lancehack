# Recovering lancelot back to Android

Written **before** the first write to the phone, as required by the project rules.
It is filled in progressively: the tables marked *TODO(M1)* need values that only the
phone (or the exact stock ROM) can provide.

## 0. The assumption that makes all of this recoverable

The chain is **BROM → preloader → ATF → LK → boot.img**. Everything this project
writes lives at the *end* of that chain:

- the shim image goes into `recovery` (option A) or `boot` (option B);
- the iOS root goes into `userdata`.

`preloader`, `lk`/`lk2`, `tee1`/`tee2`, `seccfg`, `nvram`, `nvdata`, `nvcfg`,
`protect1`/`protect2`, `proinfo`, `persist`, `md1img`/`md1dsp`, `frp`, `metadata`
are **never written**. As long as LK and the preloader are untouched, fastboot keeps
working and a stock ROM can restore every partition. A brick in this project's sense
means "fastboot still works but Android does not boot", which is recoverable.

The two ways to actually lose the phone are: writing the preloader/LK, and a BROM
operation with the wrong DA/scatter. Both are out of scope and require a separate,
explicit decision.

## 1. Materials to have before the first write

| Material | Why | Status |
| --- | --- | --- |
| Exact MIUI version installed (Settings → About, or `fastboot getvar`/`getprop ro.build.version.incremental`) | A fastboot ROM from a different region/version can fail or trigger anti-rollback | TODO(M1) |
| Official Xiaomi **fastboot** ROM (`.tgz`) for that exact version | Contains `boot.img`, `recovery.img`, `super`, firmware images and `flash_all` scripts | TODO(M1) |
| SHA-256 of the downloaded `.tgz`, recorded in `backup/SHA256SUMS` | Detect corrupt downloads before they are needed | TODO(M1) |
| Backup of the device-specific partitions (table below) | `persist`/`nvram`/`nvdata`/`proinfo` hold calibration, IMEI-adjacent data and cannot be regenerated | TODO(M1) |
| Confirmation that the bootloader is unlocked (`fastboot oem device-info`) | Nothing in this project works on a locked bootloader | TODO(M1) |

Known reference point (verify, do not trust): lancelot fastboot ROM packages exist
for MIUI `V13.0.4.0.SJCMIXM` (Android 12, ~4.3 GB `.tgz`), model codes
M2004J19G / M2004J19AG. Match the phone's own version string, not the model code.

## 2. Partition backup (M1)

There is **no `fastboot boot`** on this phone (verified on the unit: the transfer stalls
and LK stops answering until power-cycled), and **`fastboot fetch` is not supported**
(the host waits forever, no data). So a backup cannot be taken from fastboot. The
realistic paths, in order of preference:

1. **BROM read-only with mtkclient** (no partition writes, no flashing):
   `mtkclient` can read `by-name` partitions through the Boot ROM. Access may be
   refused on units with SBC/SLA/DAA enabled; a refusal is a normal outcome, not a
   failure of the phone.
2. **A recovery flashed into the slot LK actually boots, then `adb pull`**: this is
   itself a write, so it must be preceded by having the matching stock image from the
   ROM `.tgz` (which is device-independent for a given firmware build, so it is
   available without reading the phone).

   Which slot that is, is **not yet settled** on this phone: the first `fastboot getvar
   all` did not list a `recovery` partition, but that read was truncated, and on MTK
   phones with dynamic partitions the recovery ramdisk often lives inside `boot`
   (recovery-as-boot). The full table decides it, and with it whether the M2 plan
   ("flash the shim into `recovery`") is even possible.

Partitions to back up (all sizes are read from the phone, not assumed):

| Partition | Why | Size | SHA-256 |
| --- | --- | --- | --- |
| `boot` | Restore material if option B is used | TODO | TODO |
| `recovery` | Restore material for option A | TODO | TODO |
| `dtbo` | Panel/touch/board DTB used by Android | TODO | TODO |
| `vbmeta`, `vbmeta_system`, `vbmeta_vendor` | AVB state; needed to explain/undo verification errors | TODO | TODO |
| `persist` | Sensors/Wi-Fi calibration, per-device, not regenerable | TODO | TODO |
| `nvram`, `nvdata`, `nvcfg` | Radio/IMEI-adjacent data, not regenerable | TODO | TODO |
| `proinfo` | Product info | TODO | TODO |
| `protect1`, `protect2` | Keystore/DRM material | TODO | TODO |
| `seccfg` | Bootloader lock state | TODO | TODO |
| `frp` | Factory reset protection state | TODO | TODO |
| `md1img`, `md1dsp` | Modem firmware | TODO | TODO |
| `misc`, `para`, `expdb`, `boot_para` | Boot control and logs | TODO | TODO |

`userdata` is explicitly **not** backed up by this project: it holds the Android user
data that will be replaced by HFS+ (see §4).

Store dumps in `backup/` (git-ignored), with a `SHA256SUMS` file, and keep a second
copy off the machine that runs the experiments.

## 3. Undo the shim only (the common case)

Fastboot is reachable (Vol- + Power), Android's `boot` was never touched, and the
shim lives in `recovery`:

```sh
fastboot flash recovery backup/recovery.img     # or recovery.img from the stock ROM
fastboot reboot
```

If the shim is in `boot` instead:

```sh
fastboot flash boot backup/boot.img             # or boot.img from the stock ROM
fastboot reboot
```

Both restore the previous state **without** touching user data.

## 4. Full restore of Android

This **erases `userdata`** — that is, the iOS installation and anything Android stored
there.

1. Extract the stock ROM `.tgz` (contains `images/` and `flash_all.sh`/`.bat`).
2. Verify the images against the ROM's own checksums.
3. Run the ROM's own `flash_all.sh` (Linux/macOS) or `flash_all.bat` (Windows), or
   flash the same images by hand in the order the script uses. Do not skip
   `vbmeta*`; a stale vbmeta is the usual cause of "flashes fine, does not boot".
4. First boot can take several minutes. If it bootloops, re-flash and let it finish
   before intervening.
5. Do **not** relock the bootloader (`fastboot oem lock`) while any custom image is
   present. Relocking with a modified boot chain is the one reliable way to need a
   BROM/EDL recovery.

## 5. When only BROM is left

If the screen stays black and neither fastboot nor recovery responds:

1. Do not keep pressing buttons; disconnect and let the battery settle.
2. Try Vol- + Power with the USB cable connected for 10 s; check
   `lsusb`/System Information for the MediaTek BROM VID/PID.
3. BROM access on MT6768 may require authentication (SBC/SLA/DAA); if `mtkclient`
   refuses, stop — do not attempt a different DA blindly.
4. A BROM/EDL operation is a **separate decision** with its own risk review; it can
   write the preloader and is the only step in this document that can make things
   worse.

## 6. Rules

- Backups first, checksums always; a backup without a verified checksum is not a backup.
- No write outside `userdata` without: verified backup, a written restore step, and
  an explicit confirmation in the conversation at that moment.
- No `fastboot oem lock`, ever, while this project is installed.
- No downgrade flash across MIUI versions without checking anti-rollback first
  (current lancelot ARB status: **UNKNOWN**, treat as active).
