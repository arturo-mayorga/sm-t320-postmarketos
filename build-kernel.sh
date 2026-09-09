#!/bin/sh
# Rebuild the mondrianwifi kernel and regenerate the boot image.
#
# Run from a terminal where sudo can prompt -- pmbootstrap needs root for its
# chroot mounts and there is no TTY in the agent's shell.
#
# The install step regenerates the rootfs with FRESH filesystem UUIDs. We do
# NOT reflash the rootfs, only boot.img, so the new boot image's cmdline must
# be patched back to the device's existing UUIDs with patch-bootimg-uuids.py
# before flashing. See findings/NOTES.md.
set -e
cd "$(dirname "$0")"

PMB="pmbootstrap -p $PWD/pmaports -w $PWD/pmb-work"

# stale mounts from a previous run break losetup during install
$PMB shutdown

# build explicitly first so a compile error surfaces in seconds, not after
# install has already spent ten minutes assembling a rootfs
$PMB build --force linux-postmarketos-qcom-msm8974

exec ./install-hyprland.sh
