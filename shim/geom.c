/*
 * Raw-buffer geometry probe for lancelot - measurement, not an instrument.
 *
 * TEST #6 and #7 gave contradictory pictures of the LK framebuffer: the first
 * suggested a 90-degree rotated landscape buffer, the second (written for that
 * geometry) came out as a jumble. So this build makes no assumption at all: it
 * paints the memory it is allowed to paint in a pattern that is a function of
 * the RAW buffer offset, and the user reports which colour is where.
 *
 * The pattern
 * -----------
 * The first 10,108,800 bytes - exactly one panel image, 1080x2340 at 4 bytes
 * per pixel, whichever way the buffer is laid out - are split into four equal
 * quarters, painted with a colour and a huge digit:
 *
 *     quarter 0  RED     digit 1
 *     quarter 1  GREEN   digit 2
 *     quarter 2  BLUE    digit 3
 *     quarter 3  YELLOW  digit 4
 *
 * Everything after that, up to the end of vramSize, is MAGENTA: if the screen
 * shows magenta the displayed frame does not start where FB_BASE says.
 *
 * A white 6-pixel edge runs along the first buffer row and TWO vertical white
 * rules run through every quarter at columns 0 and 540. The rules make the
 * horizontal part measurable instead of guessable: 540 px is the distance
 * between them if the scale is 1:1, and where they land on the panel is the
 * scanout's horizontal offset.
 *
 * Reading the result
 * ------------------
 * Four horizontal bands, digits upright, white edges on the left and at the
 * top: the old portrait geometry was right after all.
 * Four vertical bands: the buffer is landscape.
 * Digits rotated or sheared: the pitch is not what any of our guesses said.
 * A magenta band: the frame starts at a different offset.
 *
 * Safety: everything written is inside the display carve-out
 * (0x7dcb0000 + 34 MiB, FB_BASE 0x7e605000, vram 25,067,520 bytes ends at
 * 0x7fdf3000). Nothing else is touched, no partition is written, and the probe
 * never resets: hold Power ~15 s to leave.
 */
#include <stdint.h>
#include "board.h"
#include "font.h"

#define PAINT_W		1080
#define PAINT_H		2340
#define QUARTER		(PAINT_H / 4)
#define VRAM_BYTES	0x017e8000UL
#define VRAM_ROWS	((uint32_t)((VRAM_BYTES / 4) / PAINT_W))	/* 5802 */

#define RED		0xffff0000U
#define GREEN		0xff00ff00U
#define BLUE		0xff0000ffU
#define YELLOW		0xffffff00U
#define MAGENTA		0xffff00ffU
#define WHITE		0xffffffffU
#define BLACK		0xff000000U

/* Set at runtime: a statically initialised absolute pointer would leave an
 * ABS64 relocation in .data and the image is loaded at an arbitrary address. */
static volatile uint32_t *fb;

static void __attribute__((noreturn)) hang(void)
{
	for (;;)
		__asm__ volatile("wfe");
}

static void fill_rows(uint32_t y0, uint32_t y1, uint32_t colour)
{
	for (uint32_t y = y0; y < y1; y++) {
		volatile uint32_t *l = fb + (uint32_t)y * PAINT_W;

		for (int x = 0; x < PAINT_W; x++)
			l[x] = colour;
	}
}

/* One digit, centred in its quarter, 160x320 px: readable from arm's length
 * even if the scanout is sheared. */
static void digit(int d, uint32_t qy, uint32_t fg)
{
	const unsigned char *g = font8x16[('0' + d) - 0x20];
	const int S = 20;
	const uint32_t x0 = (PAINT_W - 8 * S) / 2;
	const uint32_t y0 = qy + (QUARTER - 16u * S) / 2;

	for (int gy = 0; gy < 16; gy++)
		for (int gx = 0; gx < 8; gx++) {
			if (!(g[gy] & (0x80 >> gx)))
				continue;
			for (int sy = 0; sy < S; sy++)
				for (int sx = 0; sx < S; sx++)
					fb[(uint32_t)(y0 + gy * S + sy) * PAINT_W +
					   x0 + gx * S + sx] = fg;
		}
}

void secondary_main(uint64_t idx)
{
	(void)idx;
	hang();
}

void exc_handler(uint64_t slot, uint64_t *regs)
{
	(void)slot;
	(void)regs;
	/* No text: the whole screen goes magenta, which no other state produces. */
	if (fb)
		fill_rows(0, PAINT_H, MAGENTA);
	hang();
}

void shim_main(uint64_t dtb)
{
	(void)dtb;
	fb = (volatile uint32_t *)(uintptr_t)FB_BASE;

	fill_rows(0, QUARTER, RED);
	fill_rows(QUARTER, 2 * QUARTER, GREEN);
	fill_rows(2 * QUARTER, 3 * QUARTER, BLUE);
	fill_rows(3 * QUARTER, PAINT_H, YELLOW);
	/* The rest of vram: magenta means "the frame is not where we think". */
	fill_rows(PAINT_H, VRAM_ROWS, MAGENTA);

	digit(1, 0, BLACK);
	digit(2, QUARTER, BLACK);
	digit(3, 2 * QUARTER, WHITE);
	digit(4, 3 * QUARTER, BLACK);

	/* Orientation markers: first buffer row, then TWO vertical rules at column 0
	 * and column 540. The distance between them on the panel is the scale
	 * directly (it must read 540 px), and their absolute positions give the
	 * horizontal offset of the scanout without any arithmetic guess. */
	fill_rows(0, 6, WHITE);
	for (uint32_t i = 0; i < 4; i++) {
		uint32_t y0 = i * QUARTER;

		for (uint32_t y = y0; y < y0 + QUARTER; y++) {
			volatile uint32_t *l = fb + (uint32_t)y * PAINT_W;

			for (int x = 0; x < 6; x++) {
				l[x] = WHITE;
				l[540 + x] = WHITE;
			}
		}
	}

	hang();
}
