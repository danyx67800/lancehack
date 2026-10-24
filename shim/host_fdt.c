/*
 * Host test for shim/fdt_lance.c: parse a real lancelot .dtb (base DTB,
 * optionally merged with the dtbo overlay by fdtoverlay) and print what the
 * shim would show on the phone.
 *
 *   gcc -O1 -Wall -o out/host_fdt host_fdt.c fdt_lance.c && ./out/host_fdt FILE.dtb
 */
#include <stdio.h>
#include <stdlib.h>
#include "fdt_lance.h"

int main(int argc, char **argv)
{
	FILE *f = fopen(argv[1], "rb");
	long n;
	struct lance_probe p;

	if (!f) {
		perror(argv[1]);
		return 1;
	}
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n <= 0) {
		fprintf(stderr, "empty file\n");
		return 1;
	}
	unsigned char *buf = malloc((size_t)n);
	if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
		fprintf(stderr, "read failed\n");
		return 1;
	}
	fclose(f);

	lance_fdt_probe((uintptr_t)buf, &p);
	printf("valid=%d totalsize=%u banks=%d\n", p.valid, p.totalsize, p.nbanks);
	for (int i = 0; i < p.nbanks; i++)
		printf("  mem[%d] base=0x%llx size=0x%llx\n", i,
		       (unsigned long long)p.mb_base[i],
		       (unsigned long long)p.mb_size[i]);
	printf("fb=0x%llx vram=0x%llx lcmfound=%d lcm=%s\n",
	       (unsigned long long)(p.fb_base_l | (p.fb_base_h << 32)),
	       (unsigned long long)p.vram, p.lcmfound,
	       p.lcmname[0] ? p.lcmname : "(none)");
	free(buf);
	return p.valid ? 0 : 2;
}
