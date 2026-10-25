#pragma once
/*
 * Board selection. build.sh passes -DBOARD_<NAME>; the default is sweet.
 * Every value comes from the shipped device tree of that phone; the source
 * and status of each one live in docs/lancelot-hardware.md.
 *
 * FB_W / FB_H are always what the panel shows (the console's coordinate
 * system). FB_STRIDE / FB_ROT describe the buffer the bootloader handed over.
 */

#if defined(BOARD_LANCELOT)

#define BOARD_NAME		"lancelot"

/*
 * /chosen atag,videolfb of the lancelot DTBO: fb_base_l = 0x7e605000,
 * fb_base_h = 0, vramSize = 0x017e8000.
 */
#define FB_BASE			0x7e605000UL
#define FB_W			1080
#define FB_H			2340

/*
 * MEASURED on the phone (TEST #9 + #10 photos, 2026-10-25).
 *
 * Pitch: 4320 bytes (1080 px), no rotation, no transpose. TEST #9 painted four
 * equal BYTE quarters and they came out as four clean horizontal bands with the
 * exact 585-row spacing that a 1080-px pitch predicts; TEST #7, which assumed a
 * 2340-px pitch, produced the dense horizontal aliasing of a 2.17x mismatch.
 * The TEST #6 "rotation" reading was that aliasing seen the other way round.
 *
 * Horizontal: the scanout does not start at the first pixel of the line. TEST
 * #10 drew white rules at buffer columns 0 and 540; they landed 525-540 px
 * apart (scale 1:1) but at panel x ~265 and ~790 instead of 0 and 540, which is
 * a constant shift of ~256 px: the panel shows column (x + 256) of the line for
 * panel column x, i.e. each line is read starting 1024 bytes in and the last
 * 256 px of the panel show the START of the NEXT buffer row. So content must
 * stay left of x = FB_W - FB_XOFF and never wrap into the following row.
 *
 * Vertical: the top ~500 px of the panel are LK's own splash layer (the
 * bootloader-unlocked padlock), not this buffer, so the console starts at
 * TOP_VISIBLE.
 */
#define FB_STRIDE		(FB_W * 4)
#define FB_XOFF			256
/* First screen row that is ours: everything above is LK's layer. */
#define TOP_VISIBLE		520
/* Widest canvas that cannot wrap into the next buffer row. */
#define CANVAS_W		(FB_W - FB_XOFF - 8)

/* LK leaves the merged FDT at BOARD_KERNEL_BASE + BOARD_DTB_OFFSET. */
#define LK_FDT_ADDR		0x4bc80000UL

/* TOPRGU: wdt@10007000, WDT_MODE 0x00 (key in the upper bits), WDT_SWRST 0x14. */
#define TOPRGU_BASE		0x10007000UL
#define TOPRGU_WDT_MODE		0x00
#define TOPRGU_WDT_SWRST	0x14
#define TOPRGU_MODE_KEY		0x22000000UL
#define TOPRGU_SWRST_KEY	0x1209UL

#define UART0_BASE		0x11002000UL

#else	/* sweet */

#define BOARD_NAME		"sweet"
/* reserved-memory cont_splash_region@9c000000 */
#define FB_BASE			0x9c000000UL
#define FB_W			1080
#define FB_H			2400
#define FB_STRIDE		(FB_W * 4)	/* a8r8g8b8, matches mainline simplefb */
#define FB_XOFF			0
#define TOP_VISIBLE		0
#define CANVAS_W		FB_W

#define LK_FDT_ADDR		0
#define TOPRGU_BASE		0
#define UART0_BASE		0

#endif
