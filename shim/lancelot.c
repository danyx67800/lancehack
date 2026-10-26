/*
 * lancelot (MT6769V/CU, Redmi 9) - milestone-2 instrument.
 *
 * LK enters us with the MMU off and x0 = the FDT it built (base DTB +
 * dtbo overlays + its own /chosen atags). On this SoC that handoff happens at
 * **EL2**, not EL1 (see start.S, which drops to EL1 and reports the level), so
 * the first line of the page states which level we were given: a page that
 * reads S0 EL 2 with the kernel now getting further is the fix that unblocked
 * M3. There is no UART and no USB, so the
 * screen is the only output channel: this build has to answer everything it
 * can in a single boot, and it must be readable without a working timer.
 *
 * Display geometry (measured, TEST #6)
 * ------------------------------------
 * The buffer LK hands over is the panel's image rotated 90 degrees clockwise:
 * 2340 px per line, 1080 lines, base 0x7e605000. shim/console.c owns that
 * mapping, so everything below is written in SCREEN pixels - (0,0) is the
 * top-left pixel the user sees. A white frame is drawn around the screen: if
 * all four sides are visible, the geometry is right.
 *
 * How it reports
 * --------------
 * A full-width colour BAND is drawn after each stage completes, stacked from
 * the bottom of the panel upwards, in this order:
 *
 *   S1 red     framebuffer writable at FB_BASE
 *   S2 orange  TOPRGU watchdog stopped, and verified by reading it back
 *   S3 yellow  system counter (CNTPCT) sampled twice
 *   S4 green   FDT parsed from x0 (/chosen atags + /memory banks)
 *   S5 cyan    GICD identification read
 *   S6 blue    GICR redistributor 0 read
 *   S7 white   end of the probe
 *
 * The first missing band is the stage that failed, and the bands are persistent
 * state, so no delay is needed to read the result: the timer is one of the
 * things under test, not a tool. A synchronous exception turns the whole
 * screen magenta and prints ESR/ELR/FAR instead.
 *
 * Nothing here resets the phone. A failed boot used to reset back into the same
 * boot target, and on 2026-10-24 that produced a recovery loop that cost the
 * user fastboot. The shim now always hangs: hold Power ~15 s to get out.
 *
 * Nothing here writes a partition or touches a peripheral beyond TOPRGU, the
 * GIC and the framebuffer.
 */
#include <stdint.h>
#include "board.h"
#include "console.h"
#include "fdt_lance.h"
#include "loader.h"

/* arm,gic-v3: dist 0x0c000000 +0x40000, redist 0x0c040000 +0x200000 (8 PEs). */
#define GICD_BASE		0x0c000000UL
#define GICD_CTLR		0x00
#define GICD_TYPER		0x04
#define GICR_BASE		0x0c040000UL
#define GICR_TYPER		0x08
#define GICR_WAKER		0x14

#define FDT_MAGIC		0xd00dfeedU

/* TOPRGU WDT_MODE bit 0 = ENABLE (docs/lancelot-hardware.md §9). */
#define TOPRGU_MODE_EN		0x1UL

/* ---- screen layout ---- */

#define BAND_H			64
#define BAND_GAP		6
#define BORDER			6

#define ORANGE			0xffff8000U
#define CYAN			0xff00ffffU
#define MAGENTA			0xffff00ffU
#define BLACK			0xff000000U

#define rsr(r) ({ uint64_t _v; __asm__ volatile("mrs %0, " r : "=r"(_v)); _v; })

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

static uint32_t be32(uintptr_t a) { return __builtin_bswap32(rd32(a)); }

static void __attribute__((noreturn)) hang(void)
{
	for (;;)
		__asm__ volatile("wfe");
}

/* The breadcrumb. Band 0 (red) sits at the very bottom of the screen and the
 * stack grows upwards, so the topmost coloured band is the last stage reached. */
static void band(int i, uint32_t colour)
{
	int y0 = FB_H - (i + 1) * BAND_H;

	/* Only as wide as the canvas: past FB_W - FB_XOFF the writes would wrap
	 * into the next buffer row. */
	con_rect(0, y0, CANVAS_W, y0 + BAND_H - BAND_GAP, colour);
}

/* ---- watchdog ---- */

static uint32_t wdt_before, wdt_after;

/* The watchdog has to die early, or it eventually resets the phone mid-probe.
 * The write is verified: on some SoCs the key is silently dropped, and a
 * reset a couple of seconds in is exactly what that looks like. */
static int wdt_stop(void)
{
	if (!TOPRGU_BASE)
		return 0;
	wdt_before = rd32(TOPRGU_BASE + TOPRGU_WDT_MODE);
	wr32(TOPRGU_BASE + TOPRGU_WDT_MODE,
	     (wdt_before & ~TOPRGU_MODE_EN) | TOPRGU_MODE_KEY);
	__asm__ volatile("dsb sy" ::: "memory");
	wdt_after = rd32(TOPRGU_BASE + TOPRGU_WDT_MODE);
	return !(wdt_after & TOPRGU_MODE_EN);
}

/* ---- exception path ---- */

static volatile int in_panic;

void secondary_main(uint64_t idx)
{
	(void)idx;
	hang();
}

void exc_handler(uint64_t slot, uint64_t *regs)
{
	(void)regs;
	/* A fault raised while painting the panic screen must not re-enter here:
	 * the stack would run into .bss and the report would be lost. */
	if (in_panic)
		hang();
	in_panic = 1;
	con_clear(MAGENTA);
	con_str("PANIC", "lancelot shim");
	con_hex2("SLOT", "n=", (uint32_t)slot, "mp=", (uint32_t)rsr("mpidr_el1"));
	con_hex("ESR", rsr("esr_el1"));
	con_hex("ELR", rsr("elr_el1"));
	con_hex("FAR", rsr("far_el1"));
	con_hex("SCTLR", rsr("sctlr_el1"));
	con_str("HOLD", "Power 15s");
	hang();
}

/* ---- main ---- */

static uint64_t cntfrq_raw;
static uint64_t t0, t1;

/* Sample the system counter, without waiting on it. If it does not advance the
 * old sleep_ms() waits and the hold-loops are meaningless, which is worth
 * knowing before trusting any timed behaviour. */
static void sample_timer(void)
{
	cntfrq_raw = rsr("cntfrq_el0");
	t0 = rsr("cntpct_el0");
	/* ~200k iterations of an empty loop: long enough for a 13 MHz counter to
	 * move, short enough to be invisible. */
	for (volatile uint32_t i = 0; i < 200000; i++)
		;
	t1 = rsr("cntpct_el0");
}

void shim_main(uint64_t dtb, uint64_t el)
{
	struct lance_probe p;
	uint64_t fbb;
	int wdt_ok;

	/* S1: the framebuffer, before anything that could fault. The base is the
	 * DTBO value on purpose - this stage must not depend on the FDT parse. */
	con_fb = (volatile uint32_t *)(uintptr_t)FB_BASE;
	con_clear(BLACK);
	/* The canvas is the part of the panel that is both ours (LK's splash layer
	 * owns the top) and free of the scanout shift (see FB_XOFF in board.h).
	 * The first 48 rows stay empty: the loader paints them magenta right
	 * before it jumps, so a photo taken afterwards says whether the jump ever
	 * happened even when the kernel dies before it owns the screen. */
	con_set_top(TOP_VISIBLE + 56);
	band(0, RED);
	/* The exception level the bootloader handed us over at, before start.S
	 * dropped to EL1: 1 = EL1 (nothing to do), 2 = EL2 (the drop above is what
	 * makes SCTLR_EL1 - and therefore the kernel's MMU - take effect). */
	con_hex("S0 EL", el);
	con_str("S1 FB", "writable");
	con_skip();

	/* S2: the watchdog, verified. */
	wdt_ok = wdt_stop();
	band(1, ORANGE);
	con_str("S2 WD", wdt_ok ? "stopped" : "STILL ARMED");
	con_hex2("WDT", "b=", wdt_before, "a=", wdt_after);
	con_skip();

	/* S3: the counter. */
	sample_timer();
	band(2, YELLOW);
	con_hex2("S3 TMR", "f=", (uint32_t)cntfrq_raw, "d=", (uint32_t)(t1 - t0));

	/* S4: the live FDT. */
	if (!dtb || (dtb & 3) || be32(dtb) != FDT_MAGIC)
		dtb = LK_FDT_ADDR;
	lance_fdt_probe(dtb, &p);
	band(3, GREEN);
	con_str("S4 FDT", p.valid ? "parsed" : "NOT PARSED");
	con_hex("FDT", dtb);
	/* LK's own value, versus the placeholder in the DTBO. */
	fbb = p.fb_base_l | (p.fb_base_h << 32);
	con_hex2("FBARG", "lo=", (uint32_t)fbb, "hi=", (uint32_t)(fbb >> 32));

	/* The real memory map: the point of M2, and no public DTB has it. Two banks
	 * are enough for the 4 GB SKU and keep the report inside the screen. */
	for (int i = 0; i < p.nbanks && i < 2; i++) {
		char label[] = "MEM0";

		label[3] = '0' + i;
		con_hex2(label, "lo=", (uint32_t)p.mb_base[i], "hi=", (uint32_t)(p.mb_base[i] >> 32));
		con_hex2("size", "lo=", (uint32_t)p.mb_size[i], "hi=", (uint32_t)(p.mb_size[i] >> 32));
	}
	if (!p.nbanks)
		con_str("MEM", "no /memory reg");

	/* S5: the distributor. */
	band(4, CYAN);
	con_hex2("GICD", "c=", rd32(GICD_BASE + GICD_CTLR), "t=", rd32(GICD_BASE + GICD_TYPER));

	/* S6: only our own redistributor. Reading one that is powered down can
	 * wedge the interconnect on MTK, so the other seven wait for a later
	 * milestone where there is a way to recover from it. */
	{
		uint64_t typer = (uint64_t)rd32(GICR_BASE + GICR_TYPER + 4) << 32 |
				 rd32(GICR_BASE + GICR_TYPER);

		band(5, BLUE);
		con_hex2("GICR", "a=", (uint32_t)(typer >> 32), "w=", rd32(GICR_BASE + GICR_WAKER));
	}

	/* S7: done. If LK handed us a ramdisk with a kernel payload, become its
	 * iBoot: loader_boot() copies the segments, builds boot_args and jumps, and
	 * only returns on error. With an empty ramdisk this is still the end of the
	 * probe page, which is what makes the same image an instrument. */
	band(6, WHITE);
	con_str("S7 DONE", "probe complete");
	con_str("LOADER", loader_boot(dtb));
	hang();
}
