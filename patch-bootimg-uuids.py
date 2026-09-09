#!/usr/bin/env python3
"""Rewrite an Android boot image's cmdline UUIDs to match the device.

pmbootstrap install regenerates the rootfs with FRESH filesystem UUIDs every
time. If you then flash only boot.img, the kernel hunts for partitions that do
not exist and the initramfs drops to its debug shell. find_partition() in the
pmOS initramfs gives pmos_root_uuid absolute precedence and does NOT fall back
to a path or label, so this must be fixed in the cmdline.

Note you cannot fix it from the other direction: ext4 with metadata_csum seeds
its checksums from the filesystem UUID, so rewriting the UUID with dd corrupts
the superblock. Only tune2fs can change it.

Usage: patch-bootimg-uuids.py <in.img> <out.img> <boot-uuid> <root-uuid>
"""
import struct, sys

if len(sys.argv) != 5:
    sys.exit(__doc__)
src, dst, boot_uuid, root_uuid = sys.argv[1:5]

d = bytearray(open(src, 'rb').read())
if d[:8] != b'ANDROID!':
    sys.exit(f"{src}: not an Android boot image")

cmd = bytes(d[64:64 + 512]).rstrip(b'\0').decode()
out = []
for tok in cmd.split():
    if tok.startswith('pmos_boot_uuid='):
        tok = 'pmos_boot_uuid=' + boot_uuid
    elif tok.startswith('pmos_root_uuid='):
        tok = 'pmos_root_uuid=' + root_uuid
    out.append(tok)
new = ' '.join(out)
if len(new) >= 512:
    sys.exit("cmdline would exceed the 512-byte header field")

d[64:64 + 512] = new.encode().ljust(512, b'\0')
open(dst, 'wb').write(bytes(d))
print(f"{dst}\n  {new}")
