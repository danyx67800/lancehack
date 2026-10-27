#!/usr/bin/env python3
"""List 64-bit constants a SWEETXNU payload materialises with movz/movk pairs.

    tools/scan-mov64.py <payload.bin> [lo] [hi]

A payload carries no symbols, so when a kernel dies before its console this is
how a constant is located in the shipped image: every `movz`/`movk` run to the
same register is folded back into the value it builds, and the ones inside the
given range are printed with their virtual address. It answers questions like
"which framebuffer block does this build map?" or "does this build still carry
the sweet address 0x9c000000?" without a local kernel tree.
"""
import struct
import sys

SEG_TABLE = 0x2a8


def segments(payload):
    nsegs, = struct.unpack_from('<I', payload, 12)
    for i in range(nsegs):
        yield struct.unpack_from('<4Q', payload, SEG_TABLE + 32 * i)


def walk(text, vmaddr):
    """Yield (va, reg, value) whenever movz/movk instructions build a value.

    XNU's MOV64 macro emits one movz plus up to three movk, and the order is
    not guaranteed: it can start at the high half. Each instruction writes a
    distinct 16-bit field, so the fields are collected and ORed by field index
    instead of by position in the instruction stream. A chain is forgotten
    after 32 unrelated instructions, which keeps two moves of the same
    register far apart from being reported as one constant.
    """
    pending = {}                       # reg -> (value, va, field mask, last)
    for idx, off in enumerate(range(0, len(text) - 3, 4)):
        w, = struct.unpack_from('<I', text, off)
        # Keep bit 31: it is the size field, and dropping it merges the
        # 32-bit and 64-bit forms of the same instruction.
        op = w & 0xff800000
        if op not in (0xd2800000, 0xf2800000):
            continue
        is_movz = op == 0xd2800000
        imm = (w >> 5) & 0xffff
        hw = (w >> 21) & 3
        rd = w & 0x1f
        value = imm << (16 * hw)
        prev = pending.get(rd)
        if prev is not None and idx - prev[3] < 32 and not (prev[2] >> hw) & 1:
            fields = prev[2] | (1 << hw)
            combined = prev[0] | value
            pending[rd] = (combined, prev[1], fields, idx)
            yield prev[1], rd, combined
        else:
            pending[rd] = (value, vmaddr + off, 1 << hw, idx)


def main():
    payload = open(sys.argv[1], 'rb').read()
    lo = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x7e000000
    hi = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x80000000
    assert payload[:8] == b'SWEETXNU', 'not a SWEETXNU payload'
    virt_base, entry = struct.unpack_from('<2Q', payload, 0x10)
    print('%s: virt_base=%#x entry=%#x' % (sys.argv[1], virt_base, entry))
    hits = 0
    for vmaddr, vmsize, off, size in segments(payload):
        for va, reg, value in walk(payload[off:off + size], vmaddr):
            if lo <= value < hi:
                print('  %#014x  x%-2d = %#x' % (va, reg, value))
                hits += 1
    print('  %d constants in [%#x, %#x)' % (hits, lo, hi))


if __name__ == '__main__':
    main()
