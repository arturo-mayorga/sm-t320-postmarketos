# Samsung Galaxy Tab Pro 8.4 (SM-T320, "mondrianwifi") -> mainline Linux

Work toward a postmarketOS / mainline-kernel port. As of 2026-09-08 the device
runs TWRP 3.4.0-0 with verified backups. **There is no bootable Linux for this
tablet yet** - the sources here are drafted and cross-checked but have never
been compiled.

## Hardware
APQ8074 (msm8974, non-pro) + PM8941/PM8841, Adreno 330, 2 GB RAM, Wi-Fi only.
Panel: Renesas R63319, dual-DSI, native 1600x2560 portrait (2 x 800x2560).
Touchscreen: Synaptics RMI4 @0x20 on blsp1_i2c2 (wiring identical to hlte).
Wi-Fi/BT: Qualcomm WCNSS "pronto" -> mainline wcn36xx (NOT Broadcom).

## Layout
- `findings/`  NOTES.md is the primary record - read this first. Also the four
               downstream .dts/.dtsi files the port was derived from, and the PIT.
- `draft/`     UNCOMPILED. panel-samsung-r63319.c, the board .dts, the bonded-DSI
               panel .dtsi, and the pmaports device package.
- `firmware/`  Pulled off the device: a330_p{fp,m4}.fw (freedreno),
               wlan/prima/WCNSS_* (wcn36xx).
- `refs/`      Upstream files used as references (r63419 driver, hlte/hammerhead/
               klte device trees).
- `flash/`     TWRP recovery.img (sha256-verified), its published checksums,
               and the PIT dumps.
- `logs/`      USB watch logs and the hotplug helper script.

Partition backups live in `../tabpro84-backup/` (13 images, all sha256-verified,
see its MANIFEST.txt). `efs-mmcblk0p11.img` is irreplaceable.

## Deliberately NOT kept (large, regenerable)
    git clone --depth 1 https://github.com/LineageOS/android_device_samsung_mondrianwifi
    git clone --depth 1 --filter=blob:none --sparse \
        https://github.com/LineageOS/android_kernel_samsung_mondrianwifi
    cd android_kernel_samsung_mondrianwifi
    git sparse-checkout set arch/arm/boot/dts drivers/video/msm
The four downstream files that actually mattered are copied into `findings/`.

## State
DONE: hardware inventory verified against live silicon; panel timings extracted
and cross-validated (derived 961.9 MHz/lane vs declared 964 MHz, 0.22%); TWRP
flashed; stock-recovery restore neutralised; backups taken and verified.

NOT DONE: nothing compiled. No dtc run, no cross-compiler. Bonded DSI on MDP5 is
unproven by anyone - no mainline msm8974 board drives a dual-link panel. That is
the main technical risk.

NEXT: build linux-postmarketos-qcom-msm8974 with the draft DTS, get a UART or
framebuffer console, then iterate on the panel.
