#pragma once
/*
 * Board selection. build.sh passes -DBOARD_<NAME>; the default is sweet.
 * Every value comes from the shipped device tree of that phone; the source
 * and status of each one live in docs/lancelot-hardware.md.
 */

#if defined(BOARD_LANCELOT)

#define BOARD_NAME		"lancelot"

/*
 * /chosen atag,videolfb of the lancelot DTBO: fb_base_l = 0x7e605000,
 * fb_base_h = 0, vramSize = 0x017e8000. The live FDT is parsed at runtime and
 * overrides this if LK reports a different base.
 */
#define FB_BASE			0x7e605000UL
#define FB_W			1080
#define FB_H			2340
/* ASSUMPTION (docs/lancelot-hardware.md §6): 4 bytes/pixel, no padding.
 * M2 prints what it sees; if the image is sheared this is the first suspect. */
#define FB_STRIDE		(FB_W * 4)

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

#define LK_FDT_ADDR		0
#define TOPRGU_BASE		0
#define UART0_BASE		0

#endif
