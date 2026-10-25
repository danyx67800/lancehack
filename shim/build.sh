#!/bin/bash
# Build the shim and wrap it in a boot.img.
#   BOARD=sweet (default)  boot.img v2 mirroring the stock PixelOS header, 4K pages
#   BOARD=lancelot         boot.img v2 for MediaTek LK, 2048-byte pages, DTB in
#                          the header at 0x4bc80000 (the tags/FDT address)
# Usage: [BOARD=..] [DTB=..] [CROSS=..] [MKBOOTIMG=..] ./build.sh [payload.bin]
set -euo pipefail
cd "$(dirname "$0")"

BOARD=${BOARD:-sweet}
CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -Wall -Wextra"
mkdir -p out

case $BOARD in
sweet)
	CROSS=${CROSS:-aarch64-linux-gnu-}
	SRCS="start.S shim.c console.c mmu.c fdt.c loader.c ufs.c"
	DTB=${DTB:-../dtb/boot_unpacked/dtb}
	MKBOOTIMG=${MKBOOTIMG:-mkbootimg}
	;;
lancelot)
	CROSS=${CROSS:-aarch64-elf-}
	# No UFS probe and no Qualcomm payload loader on this board: lancelot.c
	# provides shim_main, and the storage/display work lands in later patches.
	# loader.c is the iBoot stand-in: it finds a SWEETXNU payload in the ramdisk
	# and jumps to it with boot_args. Without a payload it reports the error and
	# the probe page stays, so the same image is still an instrument.
	SRCS="start.S lancelot.c console.c mmu.c fdt_lance.c loader.c fdt.c"
	# PROBE=geom swaps the instrument for the raw-buffer geometry probe
	# (shim/geom.c): colour quarters and big digits, no coordinate maths.
	[ "${PROBE:-}" = geom ] && SRCS="start.S geom.c mmu.c"
	[ "${PROBE:-}" = geom ] && OUTNAME=shim-boot-lancelot-geom.img
	DTB=${DTB:-../dtb/lancelot-boot/dtb}
	MKBOOTIMG=${MKBOOTIMG:-python3 ../tools/mkbootimg.py}
	CFLAGS="$CFLAGS -DBOARD_LANCELOT"
	;;
*)
	echo "error: unknown BOARD=$BOARD" >&2
	exit 1
	;;
esac

CC=${CROSS}gcc
OBJCOPY=${CROSS}objcopy
READELF=${CROSS}readelf
NM=${CROSS}nm

for f in $SRCS; do
	$CC $CFLAGS -c $f -o out/${f%.*}.o
done

OBJS=$(for f in $SRCS; do printf 'out/%s.o ' "${f%.*}"; done)
C_OBJS=$(for f in $SRCS; do [ "$f" = start.S ] || printf 'out/%s.o ' "${f%.*}"; done)

# The image runs wherever the bootloader puts it with no relocation pass: only
# the header's image_size (start.S's .quad _end, linked at 0) may be an
# absolute address; the C objects must be free of them.
abs=$($READELF -r $C_OBJS | grep -c ABS64 || true)
[ "$abs" = 0 ] || { echo "error: $abs absolute relocations in C objects" >&2; exit 1; }

$CC $CFLAGS -static -T shim.ld -Wl,--build-id=none -Wl,--no-warn-rwx-segments -Wl,-N \
	$OBJS -o out/shim.elf
$OBJCOPY -O binary out/shim.elf out/Image

# The bootloader copies the file; pad to image_size so .bss/stacks exist as
# zeroed bytes.
size=$($NM out/shim.elf | python3 -c 'import sys; print([int(l.split()[0],16) for l in sys.stdin if len(l.split())==3 and l.split()[2]=="_end"][0])')
truncate -s "$size" out/Image

# Ramdisk: an xnu payload (tools/mkpayload.py) if given as $1, else an empty
# newc cpio. LK and ABL both dislike a zero-size ramdisk on some builds.
if [ -n "${1:-}" ]; then
	cp "$1" out/ramdisk.bin
else
	python3 - <<'PY'
import gzip
# Minimal newc archive: header for "." then the trailer record.
def rec(name, data, mode, ino):
    h = ("070701%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x"
         % (ino, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(name) + 1, 0))
    return (h + name + "\0").encode() + b"\0" * ((4 - (len(h) + len(name) + 1) % 4) % 4) + data
blob = rec(".", b"", 0o040755, 1) + rec("TRAILER!!!", b"", 0, 0)
open("out/ramdisk.bin", "wb").write(gzip.compress(blob, 9))
PY
fi

if [ "$BOARD" = lancelot ]; then
	# The stock boot.img carries a gzip kernel and MTK LK is only exercised
	# with that format, so ship the shim the same way (NOGZIP=1 to compare).
	KERNEL=out/Image
	if [ -z "${NOGZIP:-}" ]; then
		gzip -9 -n -c out/Image > out/Image.gz
		KERNEL=out/Image.gz
	fi
	# The cmdline is not cosmetic. MTK LK's platform_parse_bootopt() starts
	# with g_is_64bit_kernel = 0 and only switches the handoff to AArch64 when
	# it finds the final "64" of bootopt=64S3,32N2,64N2; without it LK jumps
	# into our 64-bit payload in AArch32 state (wrong instruction set, instant
	# fault). Copied verbatim from the shipped LineageOS recovery.img.
	CMDLINE="bootopt=64S3,32N2,64N2 androidboot.serialconsole=0"
	# recovery_dtbo: also taken from that image, where LK/recovery expect it.
	# Not in git (dtb/ is ignored); see docs/lancelot-hardware.md for the source.
	RDTBO=${RDTBO:-../dtb/lancelot-boot/recovery_dtbo.img}
	RDTBO_ARG=""
	if [ -f "$RDTBO" ]; then
		RDTBO_ARG="--recovery_dtbo $RDTBO"
	else
		echo "note: no recovery_dtbo at $RDTBO, building without it" >&2
	fi
	$MKBOOTIMG --header_version 2 --kernel "$KERNEL" --ramdisk out/ramdisk.bin \
		--dtb "$DTB" $RDTBO_ARG --base 0x40078000 --kernel_offset 0x8000 \
		--ramdisk_offset 0x7c08000 --tags_offset 0xbc08000 --dtb_offset 0xbc08000 \
		--pagesize 2048 --os_version 11.0.0 --os_patch_level 2026-10 \
		--cmdline "$CMDLINE" -o out/shim-boot.img
	cp out/shim-boot.img "out/${OUTNAME:-shim-boot-lancelot.img}"
else
	$MKBOOTIMG --header_version 2 --kernel out/Image --ramdisk out/ramdisk.bin \
		--dtb "$DTB" --base 0 --kernel_offset 0x8000 --ramdisk_offset 0x1000000 \
		--tags_offset 0x100 --dtb_offset 0x1f00000 --pagesize 4096 \
		--os_version 17.0.0 --os_patch_level 2035-06 \
		--cmdline "console=null" -o out/shim-boot.img
fi

# Host render of the console layout (sweet only; it needs the host font test).
if [ "$BOARD" = sweet ]; then
	gcc -O1 -Wall -o out/host_test console.c host_test.c && ./out/host_test
fi

ls -l out/Image out/shim-boot.img
