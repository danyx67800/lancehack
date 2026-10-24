/*
 * Flattened-device-tree reader for the lancelot shim. Big-endian tokens,
 * 4-byte aligned, no libc: it also runs on the host (shim/host_fdt.c) against
 * the real lancelot DTB so the parser can be tested before a phone boot.
 */
#include "fdt_lance.h"

#define FDT_MAGIC	0xd00dfeedU
#define FDT_BEGIN_NODE	1
#define FDT_END_NODE	2
#define FDT_PROP	3
#define FDT_NOP		4
#define FDT_END		9

static uint32_t be32(uintptr_t a)
{
	return __builtin_bswap32(*(volatile uint32_t *)a);
}

static uintptr_t align4(uintptr_t a) { return (a + 3) & ~3UL; }

static const char *skip_str(uintptr_t p)
{
	while (*(volatile char *)p)
		p++;
	return (const char *)(p + 1);
}

static int str_eq(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return *a == *b;
}

static int starts_with(const char *s, const char *p)
{
	while (*p)
		if (*s++ != *p++)
			return 0;
	return 1;
}

static void copy_str(char *dst, const char *src, int max)
{
	int i = 0;

	while (src[i] && i < max - 1) {
		dst[i] = src[i];
		i++;
	}
	dst[i] = 0;
}

/*
 * MTK stores the framebuffer base as a 4-byte string holding the value in
 * big-endian order ("~`P\0" = 0x7e605000); dtc may drop the trailing NUL, so
 * a 3-byte property is left-padded with zero.
 */
static uint32_t prop_be32(uintptr_t val, uint32_t len)
{
	uint32_t v = 0;

	for (uint32_t i = 0; i < 4; i++)
		v = (v << 8) | (i < len ? *(volatile uint8_t *)(val + i) : 0);
	return v;
}

void lance_fdt_probe(uintptr_t fdt, struct lance_probe *p)
{
	uintptr_t s, strings;
	int depth = 0, in_chosen = 0, in_memory = 0;
	int ac = 2, sc = 2;	/* this SoC uses 2+2; read the root's own values */

	p->valid = 0;
	p->totalsize = 0;
	p->nbanks = 0;
	p->fb_base_l = p->fb_base_h = p->vram = 0;
	p->lcmfound = 0;
	p->lcmname[0] = 0;

	if (!fdt || (fdt & 3) || be32(fdt) != FDT_MAGIC)
		return;

	p->valid = 1;
	p->totalsize = be32(fdt + 4);
	s = fdt + be32(fdt + 8);
	strings = fdt + be32(fdt + 12);

	for (;;) {
		uint32_t tok;

		/* Never walk past the blob: a broken FDT must not turn into an
		 * arbitrary read on Device memory. */
		if (s + 4 > fdt + p->totalsize)
			break;
		tok = be32(s);
		s += 4;
		if (tok == FDT_BEGIN_NODE) {
			const char *name = (const char *)s;

			depth++;
			in_chosen = depth == 2 && str_eq(name, "chosen");
			in_memory = depth == 2 && starts_with(name, "memory");
			s = align4((uintptr_t)skip_str(s));
		} else if (tok == FDT_END_NODE) {
			if (depth == 2)
				in_chosen = in_memory = 0;
			if (--depth <= 0)
				break;
		} else if (tok == FDT_PROP) {
			uint32_t len = be32(s), nameoff = be32(s + 4);
			uintptr_t val = s + 8;
			const char *pn = (const char *)(strings + nameoff);

			if (val + len > fdt + p->totalsize)
				break;
			if (depth == 1 && str_eq(pn, "#address-cells")) {
				ac = (int)be32(val);
			} else if (depth == 1 && str_eq(pn, "#size-cells")) {
				sc = (int)be32(val);
			} else if (in_chosen) {
				if (str_eq(pn, "atag,videolfb-fb_base_l"))
					p->fb_base_l = prop_be32(val, len);
				else if (str_eq(pn, "atag,videolfb-fb_base_h"))
					p->fb_base_h = be32(val);
				else if (str_eq(pn, "atag,videolfb-vramSize"))
					p->vram = be32(val);
				else if (str_eq(pn, "atag,videolfb-islcmfound"))
					p->lcmfound = (int)be32(val);
				else if (str_eq(pn, "atag,videolfb-lcmname"))
					copy_str(p->lcmname, (const char *)val, sizeof(p->lcmname));
			} else if (in_memory && str_eq(pn, "reg") && ac == 2 && sc == 2) {
				for (uint32_t o = 0; o + 16 <= len && p->nbanks < LANCE_MAX_BANKS;
				     o += 16) {
					p->mb_base[p->nbanks] =
						(uint64_t)be32(val + o) << 32 | be32(val + o + 4);
					p->mb_size[p->nbanks] =
						(uint64_t)be32(val + o + 8) << 32 | be32(val + o + 12);
					p->nbanks++;
				}
			}
			s = align4(val + len);
		} else if (tok == FDT_NOP) {
			continue;
		} else {
			break;	/* FDT_END or garbage */
		}
	}
}
