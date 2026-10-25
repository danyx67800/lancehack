#pragma once
#include <stdint.h>
#include "board.h"	/* FB_BASE / FB_W / FB_H / FB_STRIDE / FB_ROT per board */

#define RED	0xffff0000U
#define BLUE	0xff0000a0U
#define WHITE	0xffffffffU
#define YELLOW	0xffffff00U
#define GREEN	0xff00ff00U

extern volatile uint32_t *con_fb;

void con_clear(uint32_t bg);
/* Filled rectangle in SCREEN coordinates, half-open on x1/y1. */
void con_rect(int x0, int y0, int x1, int y1, uint32_t colour);
/* First text row in screen pixels (default: the top margin). */
void con_set_top(int y);
void con_skip(void);
void con_str(const char *label, const char *s);
void con_hex(const char *label, uint64_t v);
/* label, then "a=xxxxxxxx b=xxxxxxxx" style pairs of 32-bit values */
void con_hex2(const char *label, const char *a, uint32_t va, const char *b, uint32_t vb);
