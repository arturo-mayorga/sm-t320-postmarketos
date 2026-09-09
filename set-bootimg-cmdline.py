#!/usr/bin/env python3
"""Add, replace or remove kernel cmdline tokens in an Android boot image.

Companion to repack-bootimg-dtb.py: together they let a boot-time experiment
(new DT, new module parameter) be flashed without a kernel build.

    set-bootimg-cmdline.py <in.img> <out.img> [key=val | key | -key] ...

  key=val   set (replace an existing key= token, else append)
  key       append a bare flag if not present
  -key      remove every token whose key matches
"""
import sys

if len(sys.argv) < 3:
    sys.exit(__doc__)
src, dst, *ops = sys.argv[1:]

d = bytearray(open(src, 'rb').read())
if d[:8] != b'ANDROID!':
    sys.exit(f"{src}: not an Android boot image")

toks = bytes(d[64:64 + 512]).rstrip(b'\0').decode().split()
def key(t): return t.split('=', 1)[0]

for op in ops:
    if op.startswith('-'):
        toks = [t for t in toks if key(t) != op[1:]]
    elif '=' in op:
        k = key(op)
        if any(key(t) == k for t in toks):
            toks = [op if key(t) == k else t for t in toks]
        else:
            toks.append(op)
    elif op not in toks:
        toks.append(op)

new = ' '.join(toks)
if len(new) >= 512:
    sys.exit("cmdline would exceed the 512-byte header field")
d[64:64 + 512] = new.encode().ljust(512, b'\0')
open(dst, 'wb').write(bytes(d))
print(f"{dst}\n  {new[:160]}...")
