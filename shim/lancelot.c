/*
 * lancelot (MT6769V/CU, Redmi 9) milestone-2 probe.
 *
 * Booted from the recovery partition, entered by LK at EL1 with the MMU off
 * and x0 = the FDT LK built for us (base DTB + dtbo overlays + its own
 * /chosen atags). One boot answers:
 *   - did our code run at all (colour bars, and a reset after ~90 s)
 *   - the live DRAM map (the shipped DTB only carries a 997 MiB placeholder)
 *   - the framebuffer LK actually reports, versus the one in board.h
 *   - GICv3 identification, CNTFRQ, MPIDR, CurrentEL
 * The MediaTek watchdog is stopped first, or the phone resets mid-probe.
 *
 * Nothing here writes a partition or touches a peripheral beyond TOPRGU, the
 * GIC and the framebuffer.
 */
#include <stdint.h>
#include "board.h"
#include "console.h"
#include "fdt_lance.h"

/* arm,gic-v3: dist 0x0c000000 +0x40000, redist 0x0c040000 +0x200000 (8 PEs). */
#define GICD_BASE		0x0c000000UL
#define GICR_BASE		0x0c040000UL
#define GICR_STRIDE		0x40000UL
#define GICR_PE_MAX		8
#define GICD_CTLR		0x00
#define GICD_TYPER		0x04
#define GICR_TYPER		0x08
#define GICR_WAKER		0x14

#define FDT_MAGIC		0xd00dfeedU
#define PSCI_SYSTEM_RESET	0x84000009UL

#define rsr(r) ({ uint64_t _v; __asm__ volatile("mrs %0, " r : "=r"(_v)); _v; })
#define wsr(r, v) __asm__ volatile("msr " r ", %0" : : "r"((uint64_t)(v)))

static inline uint32_t rd32(uintptr_t a) { return *(volatile uint32_t *)a; }
static inline void wr32(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

static uint32_t be32(uintptr_t a) { return __builtin_bswap32(rd32(a)); }

extern volatile uint32_t *con_fb;

static void sleep_ms(unsigned ms)
{
	uint64_t f = rsr("cntfrq_el0");
	uint64_t end = rsr("cntpct_el0") + f / 1000 * ms;

	while (rsr("cntpct_el0") < end)
		;
}

/* ---- watchdog ---- */

static uint32_t wdt_mode;

static void wdt_stop(void)
{
	if (!TOPRGU_BASE)
		return;
	wdt_mode = rd32(TOPRGU_BASE + TOPRGU_WDT_MODE);
	wr32(TOPRGU_BASE + TOPRGU_WDT_MODE, (wdt_mode & ~1UL) | TOPRGU_MODE_KEY);
	__asm__ volatile("dsb sy" ::: "memory");
}

/* ---- stage breadcrumbs ---- */

static void bar(uint32_t colour, int stage)
{
	char label[] = "S0";

	label[1] = '0' + stage;
	for (int y = FB_H - 90; y < FB_H - 10; y++) {
		volatile uint32_t *line = con_fb + (uint32_t)y * (FB_STRIDE / 4);
		for (int x = 10; x < FB_W - 10; x++)
			line[x] = colour;
	}
	con_str("STAGE", label);
}

/* ---- reset ---- */

static void smc_reset(void)
{
	register uint64_t x0 __asm__("x0") = PSCI_SYSTEM_RESET;

	__asm__ volatile("smc #0" : "+r"(x0) : : "x1", "x2", "x3", "memory");
}

static void __attribute__((noreturn)) reset_now(void)
{
	if (TOPRGU_BASE) {
		wr32(TOPRGU_BASE + TOPRGU_WDT_SWRST, TOPRGU_SWRST_KEY);
		__asm__ volatile("dsb sy" ::: "memory");
	}
	smc_reset();
	for (;;)
		__asm__ volatile("wfe");
}

/* ---- entry points required by start.S ---- */

/* No SMP in M2: PSCI CPU_ON is never issued, so this is never entered. */
void secondary_main(uint64_t idx)
{
	(void)idx;
	for (;;)
		__asm__ volatile("wfe");
}

void exc_handler(uint64_t slot, uint64_t *regs)
{
	(void)regs;
	con_clear(0xff0000a0U);
	con_str("PANIC", "lancelot shim");
	con_hex2("SLOT", "n=", (uint32_t)slot, "mpidr=", (uint32_t)rsr("mpidr_el1"));
	con_hex("ESR", rsr("esr_el1"));
	con_hex("ELR", rsr("elr_el1"));
	con_hex("FAR", rsr("far_el1"));
	sleep_ms(60000);
	reset_now();
}

/* ---- main ---- */

void shim_main(uint64_t dtb)
{
	struct lance_probe p;

	/* The framebuffer base is known from the DTBO; prove execution now. */
	con_clear(0xff101010U);
	con_str("SHIM", "lancelot M2 probe");
	con_str("STAGE", "1 running");
	bar(RED, 1);

	wdt_stop();
	con_str("STAGE", "2 watchdog stopped");
	bar(YELLOW, 2);

	/* LK passes the merged FDT in x0; fall back to the tags address. */
	if (!dtb || be32(dtb) != FDT_MAGIC)
		dtb = LK_FDT_ADDR;
	lance_fdt_probe(dtb, &p);

	/* If LK reports a different framebuffer, believe it - but only inside the
	 * framebuffer carve-out (0x7dcb0000 + 34 MiB), never over modem/SCP RAM. */
	uint64_t fbb = p.fb_base_l | (p.fb_base_h << 32);
	if (p.valid && fbb >= 0x7dc00000UL && fbb < 0x7ff00000UL && !(fbb & 3) &&
	    fbb != (uint64_t)(uintptr_t)con_fb) {
		con_fb = (volatile uint32_t *)(uintptr_t)fbb;
		con_clear(0xff101010U);
	} else if (!p.valid) {
		/* Not even an FDT header: say so on the screen we already own. */
		con_str("WARN", "no FDT at x0 or tags addr");
	}
	bar(GREEN, 3);

	con_str("SHIM", "lancelot M2 probe");
	con_hex("EL", rsr("CurrentEL") >> 2);
	con_hex("MPIDR", rsr("mpidr_el1"));
	con_hex("CNTFRQ", rsr("cntfrq_el0"));
	con_hex("MMFR0", rsr("id_aa64mmfr0_el1"));
	con_hex("SCTLR", rsr("sctlr_el1"));
	con_skip();
	con_hex("FDT", dtb);
	con_hex2("FDT", "magic=", p.valid ? FDT_MAGIC : 0, "size=", p.totalsize);
	con_hex("WD_MODE", wdt_mode);
	con_skip();

	con_hex("FB_ARG", fbb);
	con_hex2("FB", "w=", FB_W, "h=", FB_H);
	con_hex("VRAM", p.vram);
	con_hex2("LCM", "found=", (uint32_t)p.lcmfound,
		 "stride=", (uint32_t)FB_STRIDE);
	con_str("LCMNAME", p.lcmname[0] ? p.lcmname : "(none)");
	con_skip();

	/* The point of M2: the real memory map, which no public DTB has. */
	for (int i = 0; i < p.nbanks; i++) {
		char label[] = "MEM0";

		label[3] = '0' + i;
		con_hex(label, p.mb_base[i]);
		con_hex("   size", p.mb_size[i]);
	}
	if (!p.nbanks)
		con_str("MEM", "no /memory reg found");
	con_skip();

	/* GICv3 identification, read-only. */
	con_hex2("GICD", "ctlr=", rd32(GICD_BASE + GICD_CTLR),
		 "typer=", rd32(GICD_BASE + GICD_TYPER));
	for (int i = 0; i < GICR_PE_MAX; i++) {
		uintptr_t r = GICR_BASE + (uintptr_t)i * GICR_STRIDE;
		uint64_t typer = (uint64_t)rd32(r + GICR_TYPER + 4) << 32 |
				 rd32(r + GICR_TYPER);
		char label[] = "GICR0";

		label[4] = '0' + i;
		con_hex2(label, "aff=", (uint32_t)(typer >> 32), "waker=", rd32(r + GICR_WAKER));
		if (typer & (1ULL << 4))
			break;	/* Last */
	}

	con_str("STAGE", "4 probe done, reset in 90s");
	bar(BLUE, 4);
	sleep_ms(90000);
	con_str("RESET", "now");
	sleep_ms(2000);
	reset_now();
}
