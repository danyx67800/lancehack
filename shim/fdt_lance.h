#pragma once
#include <stdint.h>

#define LANCE_MAX_BANKS 8

/*
 * What the shim needs from the live FDT that LK hands over: the real DRAM map
 * (the shipped DTB carries a 997 MiB placeholder) and the framebuffer the
 * bootloader actually programmed. Pure reads, no MMIO, no console: the same
 * code runs on the phone and in shim/host_fdt.c against a real .dtb file.
 */
struct lance_probe {
	uint32_t totalsize;
	int valid;
	int nbanks;
	uint64_t mb_base[LANCE_MAX_BANKS], mb_size[LANCE_MAX_BANKS];
	uint64_t fb_base_l, fb_base_h, vram;
	int lcmfound;
	char lcmname[48];
};

void lance_fdt_probe(uintptr_t fdt, struct lance_probe *p);
