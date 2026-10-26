#!/usr/bin/env python3
"""Boot a lancelot SWEETXNU payload in QEMU and report where it faults.

`qemu-sweet.py` assumes phys_base (0xb0000000) sits above the DTB that QEMU
always places at 0x40000000. On lancelot phys_base *is* 0x40000000, so the
loader refuses to start with "Some ROM regions are overlapping". This script
slides the whole image up by --shift, patches the three absolute addresses that
depend on it (boot_args physBase/topOfKernelData/deviceTreeP and the ADT's
/chosen dram-base plus the absolute end of DRAM), and boots that.

Virtual addresses are untouched: only the physical placement moves, so every
symbol, PC and fault address you read here is the same one the phone reports.
That is what makes this a reproducible stand-in for the phone: TEST #26 and the
panel showed the same first fault QEMU logs here.

  qemu-lancelot.py PAYLOAD OUTDIR [--secs N] [--shift HEX] [--fb HEX]

Writes into OUTDIR:
  ram.bin, stub.bin   what QEMU loaded
  qemu.log            -d int,guest_errors, i.e. every exception with ESR/FAR/ELR
  fb.png              the panel's worth of framebuffer, if --fb is given
  summary.txt         the first exception, decoded

The payload is the ramdisk of a shim boot image:
  tools/unpack_bootv2.py shim/out/shim-boot-t25.img /tmp/b && /tmp/b/ramdisk
(the ramdisk is the raw SWEETXNU blob, not gzipped, unless build.sh gzipped an
empty one).

Exit codes: 0 booted without exceptions, 3 faulted (see summary.txt), 4 QEMU
failed to start.
"""
import argparse
import importlib.util
import os
import struct
import subprocess
import sys
import time

QEMU = 'qemu-system-aarch64'
HERE = os.path.dirname(os.path.abspath(__file__))

# lancelot's bootloader framebuffer (shim/board.h): the panel the kernel draws on.
FB_BASE, FB_W, FB_H = 0x7e605000, 1080, 2340
FB_STRIDE = FB_W * 4
FB_BYTES = FB_STRIDE * FB_H


def load_harness():
    spec = importlib.util.spec_from_file_location(
        'qemu_sweet', os.path.join(HERE, 'qemu-sweet.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def shifted_image(payload, shift):
    """Rebuild the payload at phys_base + shift, fixing the absolute addresses."""
    qs = load_harness()
    virt_base, _entry, _ao, _as, kernel_end, phys_base, mem_size = \
        struct.unpack_from('<7Q', payload, 0x10)
    _pb, img, ba_pa, entry_pa = qs.build_image(payload)
    img = bytearray(img)
    rel_ba = ba_pa - phys_base
    for off in (16, 32, 96):        # physBase, topOfKernelData, deviceTreeP
        struct.pack_into('<Q', img, rel_ba + off,
                         struct.unpack_from('<Q', img, rel_ba + off)[0] + shift)
    # qemu-sweet.py boots sweet, so its boot_args carry sweet's panel. The kernel
    # draws its console where boot_args->Video points, so without this the screen
    # stays black and the boot looks worse than it is.
    for off, val in ((40, FB_BASE), (56, FB_STRIDE), (64, FB_W), (72, FB_H)):
        struct.pack_into('<Q', img, rel_ba + off, val)
    rel_adt = (kernel_end + phys_base - virt_base) - phys_base
    for off in (0x244, 0x2718):     # /chosen dram-base, absolute end of DRAM
        struct.pack_into('<Q', img, rel_adt + off,
                         struct.unpack_from('<Q', img, rel_adt + off)[0] + shift)
    return bytes(img), qs.stub(ba_pa + shift, entry_pa + shift), phys_base + shift


def qmp(sock_path, *commands):
    """Run QMP commands; HMP's pmemsave silently writes nothing on a stopped guest."""
    import json
    import socket
    s = socket.socket(socket.AF_UNIX)
    s.connect(sock_path)
    s.settimeout(10)
    f = s.makefile('rwb')
    f.readline()

    def one(execute, **args):
        f.write((json.dumps({'execute': execute, 'arguments': args}) + '\n').encode())
        f.flush()
        while True:
            line = f.readline()
            if not line:
                return None
            msg = json.loads(line)
            if 'return' in msg or 'error' in msg:
                return msg

    one('qmp_capabilities')
    for c in commands:
        one(c[0], **c[1])
    return None


def summarize(log, outdir):
    blocks = []
    with open(log, 'r', errors='replace') as fh:
        cur = None
        for line in fh:
            if line.startswith('Taking exception'):
                cur = [line.strip()]
                blocks.append(cur)
                if len(blocks) > 3:
                    break
            elif cur is not None and line.startswith('...'):
                cur.append(line.strip())
            else:
                cur = None
    text = '\n'.join(' '.join(b) for b in blocks) or 'no exceptions'
    open(os.path.join(outdir, 'summary.txt'), 'w').write(text + '\n')
    return text


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('payload')
    ap.add_argument('outdir')
    ap.add_argument('--secs', type=float, default=25.0)
    ap.add_argument('--shift', default='0x200000',
                    help='clear QEMU\'s own DTB, which occupies the first MiB of RAM')
    ap.add_argument('--fb', default=hex(FB_BASE),
                    help="panel base PA (default: lancelot's boot framebuffer)")
    ap.add_argument('--fb-size', default=hex(FB_BYTES))
    a = ap.parse_args()

    out = os.path.abspath(a.outdir)
    os.makedirs(out, exist_ok=True)
    shift = int(a.shift, 0)
    payload = open(a.payload, 'rb').read()
    if payload[:8] != b'SWEETXNU':
        sys.exit('%s: not a SWEETXNU payload' % a.payload)

    ram, stub, load_at = shifted_image(payload, shift)
    open(os.path.join(out, 'ram.bin'), 'wb').write(ram)
    open(os.path.join(out, 'stub.bin'), 'wb').write(stub)
    stub_at = 0x60000000        # free RAM above the image, below the 4G top

    log = os.path.join(out, 'qemu.log')
    sock = os.path.join(out, 'mon.sock')
    for p in (log, sock, os.path.join(out, 'fb.raw')):
        if os.path.exists(p):
            os.unlink(p)
    cmd = [QEMU, '-M', 'virt,gic-version=3', '-cpu', 'cortex-a76', '-smp', '1',
           '-m', '4G', '-nographic', '-serial', f'file:{out}/serial.txt',
           '-qmp', f'unix:{sock},server,nowait',
           '-d', 'int,guest_errors', '-D', log, '-no-reboot',
           '-device', f'loader,file={out}/ram.bin,addr={load_at:#x},force-raw=on',
           '-device', f'loader,file={out}/stub.bin,addr={stub_at:#x},force-raw=on',
           '-device', f'loader,addr={stub_at:#x},cpu-num=0']
    print('[+] loading %#x at %#x, jumping at %#x' % (len(ram), load_at, stub_at))
    p = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                         stderr=subprocess.PIPE)
    time.sleep(a.secs)
    if p.poll() is not None:
        print(p.stderr.read().decode('utf-8', 'replace'), file=sys.stderr)
        return 4
    cmds = [('stop', {})]
    if a.fb:
        cmds.append(('pmemsave', {'val': int(a.fb, 0), 'size': int(a.fb_size, 0),
                                 'filename': os.path.join(out, 'fb.raw')}))
        cmds.append(('quit', {}))
    qmp(sock, *cmds)
    if a.fb and not p.poll():
        want = int(a.fb_size, 0)
        for _ in range(50):
            if os.path.exists(os.path.join(out, 'fb.raw')) and \
                    os.path.getsize(os.path.join(out, 'fb.raw')) >= want:
                break
            time.sleep(0.2)
    time.sleep(0.3)
    p.kill()

    text = summarize(log, out)
    print('[+] ' + text.replace('\n', '\n    '))
    raw = os.path.join(out, 'fb.raw')
    if a.fb and os.path.exists(raw):
        qs = load_harness()
        qs.FB_BASE, qs.FB_W, qs.FB_H, qs.FB_STRIDE = int(a.fb, 0), FB_W, FB_H, FB_STRIDE
        qs.write_png(os.path.join(out, 'fb.png'), open(raw, 'rb').read())
        print('[+] wrote %s/fb.png' % out)
    return 3 if text != 'no exceptions' else 0


if __name__ == '__main__':
    sys.exit(main())
