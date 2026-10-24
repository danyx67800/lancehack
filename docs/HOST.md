# Host environment

Detected 2026-10-24 on the machine running this checkout.

| Item | Value |
| --- | --- |
| OS | macOS 26 (Darwin 27.0.0), **Apple Silicon** (arm64, M4) |
| RAM | 16 GiB class (not the constraint) |
| Free disk | 36 GiB on the data volume |
| Git remote protocol | https, `gh` authenticated as `danyx67800` (scopes: repo, workflow, gist) |
| Docker | 29.5.2, daemon in a Linux VM (Ubuntu 24.04) |

## Tools

| Tool | State | Used for |
| --- | --- | --- |
| `gh` | present | fork, CI runs, artifact download |
| `git` | present | — |
| `fastboot`, `adb` | installed 2026-10-24 (`brew install --cask android-platform-tools`, v37.0.1) | M1 device access, flashing |
| `dtc`, `fdtdump` | present (homebrew) | stock DTB / live FDT decoding |
| `qemu-system-aarch64` | present | kernel-only smoke tests (`tools/qemu-sweet.py`) |
| `python3` | present | all builders |
| `docker` | present, Linux VM | fallback for `apfs-fuse`/`hfsprogs` if the macOS root builder is not enough |
| `mkbootimg` | **missing** | boot.img packing; two options: AOSP `mkbootimg.py` (pure Python, no install) or `brew install mkbootimg` if available |
| `aarch64-linux-gnu-gcc` / `aarch64-elf-gcc` | **missing** | shim build; Bergamota uses `brew install aarch64-elf-gcc`. Do **not** cross-build the shim for arm64 macOS: it is a bare-metal ELF |
| `apfs-fuse` | missing (Linux-only) | not needed: `tools/mkiosroot-macos.sh` (from Bergamota) uses `hdiutil` + `newfs_hfs` |
| `hfsprogs` (`fsck.hfsplus`, `mkfs.hfsplus`) | missing on macOS | not needed: use `fsck_hfs`/`newfs_hfs` from the base system |

## What this means per milestone

- **M1 (phone data)**: fully doable here. `fastboot`/`adb` installed; USB access to
  the phone is the only requirement.
- **M2 (shim)**: needs an aarch64 bare-metal compiler (`aarch64-elf-gcc`) — one
  `brew install` away. The resulting boot.img is packed with `mkbootimg`
  (`--header_version 2`, `--pagesize 2048`).
- **M3 (XNU)**: kernel compiles in CI on macOS runners, not locally. QEMU smoke
  tests work here.
- **M4 (root image)**: `tools/mkiosroot-macos.sh` avoids the Linux-only
  `apfs-fuse`/`hfsprogs` path at all. `tools/img2simg.py` handles the sparse
  conversion that macOS `fastboot` needs. Disk: a 20 GiB root image plus a sparse
  copy needs ~40 GiB; the current 36 GiB free is **tight** — free space or build the
  image on an external volume.
- **M5/M6**: no special host tooling.

## Not installed on purpose

- `ipsw` (blacktop): only needed to extract the iPhone X `DeviceTree.d22ap.im4p`
  from the IPSW at M3; install it then (`brew install blacktop/tap/ipsw`).
- MTK BROM tooling (`mtkclient`): only if the BROM path in `docs/RECOVERY.md` is
  chosen; it is a Python package and needs a separate decision.
