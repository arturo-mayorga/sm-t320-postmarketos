#!/usr/bin/env python3
"""Swap the appended device tree inside an Android boot image.

Device-tree questions have been costing a full pmbootstrap build, a flash and
a reboot each, which is most of the turnaround on this port. But the DTB is
just concatenated onto the zImage (CONFIG_ARM_APPENDED_DTB), and dtc lives in
the kernel tree, so a DT change only needs the DTB rebuilt and spliced back
in -- no compiler, no chroot, no sudo.

    cpp -nostdinc -I <k>/include -I <k>/arch/arm/boot/dts \\
        -I <k>/arch/arm/boot/dts/qcom -undef -D__DTS__ \\
        -x assembler-with-cpp variant.dts > variant.pp
    <k>/scripts/dtc/dtc -I dts -O dtb -o variant.dtb variant.pp
    repack-bootimg-dtb.py boot.img variant.dtb out.img

The kernel itself is untouched, so this only helps for changes that live in
the device tree. Anything in driver code still needs a real build.

Usage: repack-bootimg-dtb.py <in.img> <new.dtb> <out.img>
"""
import struct, sys

DTB_MAGIC = 0xd00dfeed

if len(sys.argv) != 4:
    sys.exit(__doc__)
src, dtb_path, dst = sys.argv[1:4]

img = bytearray(open(src, 'rb').read())
if img[:8] != b'ANDROID!':
    sys.exit(f"{src}: not an Android boot image")

kernel_size, = struct.unpack_from('<I', img, 8)
ramdisk_size, = struct.unpack_from('<I', img, 16)
second_size, = struct.unpack_from('<I', img, 24)
page_size, = struct.unpack_from('<I', img, 36)

def pages(n):
    return (n + page_size - 1) // page_size

k_off = page_size
kernel = bytes(img[k_off:k_off + kernel_size])

# The appended DTB is the last thing in the kernel blob. Walk every FDT magic
# and keep the one whose declared totalsize runs exactly to the end, so a
# stray 0xd00dfeed inside the compressed image cannot be mistaken for it.
found = None
pos = 0
while True:
    pos = kernel.find(struct.pack('>I', DTB_MAGIC), pos)
    if pos < 0:
        break
    total, = struct.unpack_from('>I', kernel, pos + 4)
    if pos + total == len(kernel):
        found = pos
        break
    pos += 4
if found is None:
    sys.exit(f"{src}: no appended DTB found at the end of the kernel")

new_dtb = open(dtb_path, 'rb').read()
if new_dtb[:4] != struct.pack('>I', DTB_MAGIC):
    sys.exit(f"{dtb_path}: not a DTB")

print(f"  old dtb: {len(kernel) - found} bytes at kernel offset {found}")
print(f"  new dtb: {len(new_dtb)} bytes")

new_kernel = kernel[:found] + new_dtb

r_off = k_off + pages(kernel_size) * page_size
s_off = r_off + pages(ramdisk_size) * page_size
ramdisk = bytes(img[r_off:r_off + ramdisk_size])
second = bytes(img[s_off:s_off + second_size]) if second_size else b''

hdr = bytearray(img[:page_size])
struct.pack_into('<I', hdr, 8, len(new_kernel))

def pad(b):
    return b + b'\0' * (pages(len(b)) * page_size - len(b))

out = bytes(hdr) + pad(new_kernel) + pad(ramdisk) + (pad(second) if second else b'')
open(dst, 'wb').write(out)
print(f"wrote {dst} ({len(out)} bytes)")
