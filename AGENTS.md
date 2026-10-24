# Working on lancehack

iOS 16 (XNU xnu-8796 + the unmodified iPhone X 16.7.x userland) on the Xiaomi Redmi 9
`lancelot` (MediaTek MT6769V/CU, Helio G80, MT6768 family). Fork of
[dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet), porting pattern
from [ry-diffusion/bergamota](https://github.com/ry-diffusion/bergamota).

## Read first

1. `NOTES.md` — the active handoff is the **first** section; below it is upstream
   sweet's log, which documents the inherited machinery.
2. `docs/lancelot-hardware.md` — every hardware fact with its source and status.
3. `docs/PORTING.md` — what changes vs sweet/beryllium and why.
4. `docs/RECOVERY.md` — the restore plan, and the precondition for any write.

## Hard rules

- **`fastboot boot` does not work on lancelot** (LK: `Unrecognized command boot`).
  The RAM-only workflow of the parent projects is unavailable. The boot path is a
  user decision, documented in `docs/RECOVERY.md` §4; until the user confirms one,
  do not write any partition.
- Never write `preloader`, `lk`/`lk2`, `tee1`/`tee2`, `seccfg`, `nvram`, `nvdata`,
  `nvcfg`, `protect1`/`protect2`, `proinfo`, `persist`, `md1img`/`md1dsp`, `frp`,
  `metadata` or any calibration/identity partition.
- `userdata` is the only default write target. `boot`/`recovery` require a verified
  backup and an explicit confirmation **in that moment**. Flashing `userdata`
  destroys Android's user data: warn and ask first.
- Never invent an address, register or constant. Every hardware value used in code
  needs a row in `docs/lancelot-hardware.md` with a source; if it is measured on the
  phone, say which boot measured it.
- Keep Apple artifacts (IPSW, root images, real device trees, dyld cache) and device
  dumps (partitions, serials, calibration) out of git. `.gitignore` already covers
  `backup/`, `dtb/`, `recovery/`, `research/`, `shim/out/`, `xnu/out*/`; add `*.ipsw`
  and root images if they ever land in the tree.
- Linux/Xiaomi GPL sources are documentation for register sequences only. Write our
  own code; never paste GPL code into the kernel.
- Commit messages in English, small commits, one idea each. Credit ios-sweet and
  Bergamota in `README.md`; do not add a `Co-Authored-By` trailer for upstream.
- Kernel patches continue our series from `0082` (base is `0081`). Bergamota's
  patches are 0082+ too: lift hunks and renumber, never merge its series.

## Workflow

- Kernel builds in GitHub Actions (`.github/workflows/xnu.yml`, macos-14 runner,
  xnu-8796). Validate a patch locally with `tools/xnu-cc.py` before spending a run.
- Shim: `shim/build.sh` (boot.img v2 for lancelot, 2048-byte pages, DTB in header).
- ADT: `tools/mkadt.py --board lancelot` (add the board entry before using it).
- QEMU: `tools/qemu-sweet.py` for kernel-only changes; it does not model MTK
  peripherals, the LK handoff or PSCI conduit details.
- On-device tests: write a **RICHIESTA DI TEST #N** with commit, SHA-256 of the
  artifacts, exact commands, key combinations, what should appear at each stage and
  what to report back. The user presses the buttons; do not assume they are watching
  a screen you cannot see.
- Update the handoff in `NOTES.md` after each milestone and before stopping: commits,
  CI run IDs, exact boot results, what is observed vs guessed.
- If two consecutive tests fail the same way, stop and re-analyse (a research agent,
  not another random attempt).
