/*
 * Minimal text console on the bootloader's splash framebuffer.
 *
 * The console draws in SCREEN coordinates: (0,0) is the top-left pixel the user
 * sees. How that maps into the buffer the bootloader handed over is per-board
 * (FB_ROT). lancelot is the interesting one: its panel is scanned from a
 * landscape buffer, rotated 90 degrees clockwise (measured, TEST #6).
 *
 * With the MMU off every access is Device memory, so stay aligned
 * (-mstrict-align) and keep the stores 32-bit.
 */
#include "console.h"
#include "font.h"

#define SCALE		3
#define CH_W		(8 * SCALE)
#define CH_H		(16 * SCALE)
#define MARGIN		16
#define LABEL_COLS	8

/* Set at runtime: a statically initialised absolute pointer would leave an
 * ABS64 relocation in .data, and the image is loaded at an arbitrary address. */
volatile uint32_t *con_fb;
static uint32_t con_bg;
static int row;
/* First text row, in screen pixels. Boards where part of the panel is covered
 * by another layer raise it above MARGIN with con_set_top(). */
static int con_top = MARGIN;

void con_set_top(int y)
{
	con_top = y;
}

/* Screen pixel -> framebuffer word. FB_STRIDE is the buffer's line pitch in
 * bytes; FB_W/FB_H are always what the panel shows. FB_XOFF is the measured
 * shift of the scanout inside each line (see board.h): panel column x shows
 * buffer column x + FB_XOFF. Callers stay below FB_W - FB_XOFF so nothing
 * wraps into the next row. */
static inline volatile uint32_t *px(int x, int y)
{
	return con_fb + (uint32_t)y * (FB_STRIDE / 4) +
	       (uint32_t)(x + FB_XOFF);
}

void con_clear(uint32_t bg)
{
	uint32_t n = (uint32_t)FB_W * FB_H;

	if (!con_fb)
		con_fb = (volatile uint32_t *)(uintptr_t)FB_BASE;
	/* The buffer is the exact frame with no padding on both boards, so this is
	 * a linear fill whatever the rotation. */
	for (uint32_t i = 0; i < n; i++)
		con_fb[i] = bg;
	con_bg = bg;
	row = 0;
}

void con_rect(int x0, int y0, int x1, int y1, uint32_t colour)
{
	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > FB_W)
		x1 = FB_W;
	if (y1 > FB_H)
		y1 = FB_H;
	for (int y = y0; y < y1; y++)
		for (int x = x0; x < x1; x++)
			*px(x, y) = colour;
}

static void put_char(int col, char ch, uint32_t fg)
{
	if (ch < 0x20 || ch > 0x7e)
		ch = '?';
	const unsigned char *g = font8x16[ch - 0x20];
	int x0 = MARGIN + col * CH_W, y0 = con_top + row * CH_H;
	if (x0 + CH_W > FB_W || y0 + CH_H > FB_H)
		return;
	for (int gy = 0; gy < 16; gy++)
		for (int gx = 0; gx < 8; gx++) {
			uint32_t c = (g[gy] & (0x80 >> gx)) ? fg : con_bg;
			for (int sy = 0; sy < SCALE; sy++)
				for (int sx = 0; sx < SCALE; sx++)
					*px(x0 + gx * SCALE + sx, y0 + gy * SCALE + sy) = c;
		}
}

static int put_str(int col, const char *s, uint32_t fg)
{
	while (*s)
		put_char(col++, *s++, fg);
	return col;
}

static void fmt_hex(char *buf, uint64_t v, int digits)
{
	static const char hex[] = "0123456789ABCDEF";
	for (int i = 0; i < digits; i++)
		buf[i] = hex[(v >> (4 * (digits - 1 - i))) & 0xf];
	buf[digits] = 0;
}

void con_skip(void)
{
	row++;
}

void con_str(const char *label, const char *s)
{
	put_str(0, label, YELLOW);
	put_str(LABEL_COLS, s, WHITE);
	row++;
}

void con_hex(const char *label, uint64_t v)
{
	char buf[17];
	fmt_hex(buf, v, 16);
	con_str(label, buf);
}

void con_hex2(const char *label, const char *a, uint32_t va, const char *b, uint32_t vb)
{
	char buf[9];
	int col = put_str(0, label, YELLOW);
	col = col < LABEL_COLS ? LABEL_COLS : col + 1;
	col = put_str(col, a, YELLOW);
	fmt_hex(buf, va, 8);
	col = put_str(col, buf, WHITE);
	if (b) {
		col = put_str(col + 1, b, YELLOW);
		fmt_hex(buf, vb, 8);
		put_str(col, buf, WHITE);
	}
	row++;
}
