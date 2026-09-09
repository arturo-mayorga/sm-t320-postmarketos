# SM-T320 (Galaxy Tab Pro 8.4 / mondrianwifi) -> mainline Linux

## Confirmed
- SoC: APQ8074 (msm8974, NON-pro). PMIC: PM8941 + PM8841.
- Reference boards in mainline: qcom-msm8974-samsung-hlte.dts (Samsung idiom),
  qcom-msm8974-lge-nexus5-hammerhead.dts (most complete). NOT klte (8974pro/PMA8084).
- Touchscreen: Atmel maxTouch (atmel_mxt_ts) -> mainline `atmel,maxtouch`. Free.
- postmarketOS: NO mondrianwifi port exists (checked 1423 device dirs).
- Mainline: NO mondrian DTS exists.

## Panel (the hard part)
- Controller: Renesas R63319, DUAL-DSI, video mode.
- Each link: 800x2560, 4 lanes, 24bpp, 60Hz, clock 964000000.
- Combined native: 1600x2560 portrait (= 2560x1600 landscape).
- Porches: h back 64 / pulse 20 / front 150 ; v back 8 / pulse 4 / front 12
- Reset sequence: <1 5>, <0 12>, <1 12>
- lp11-init, init-delay 20000us
- Init: ~12 DCS commands (see dsi_panel_samsung_2560p_video_R63319.dtsi)
  01 softreset(10ms), 36 MADCTL=00, 3A COLMOD=70, 35 TE on, 53 CTRL=2C,
  B0=00, D6=01, CA <32-byte CE tuning>, 11 sleep-out(120ms), 29 disp-on, 51 bl=15
- NO samsung-specific panel .c downstream -> whole panel def is in DT. Good.
- Mainline sibling to adapt: drivers/gpu/drm/panel/panel-renesas-r63419.c
  (also panel-jdi-fhd-r63452, panel-synaptics-r63353)

## Open risks
- Bonded/dual-DSI exists in msm DSI core (dsi.h: is_bonded_dsi) but NO mainline
  msm8974 board uses it. We'd likely be first on MDP5. Biggest unknown.
- Panel reset GPIO + supply rails not yet located (in base dtsi, not tabpro-common).

## Sources cloned
- github.com/LineageOS/android_device_samsung_mondrianwifi
- github.com/LineageOS/android_kernel_samsung_mondrianwifi (sparse: dts + video/msm)

## VERIFIED ON HARDWARE (adb, 2026-09-08)
serial d8a38f36027e240b | ro.product.device=mondrianwifiue | bootloader T320UES1AQA2
Android 4.4.2 stock | warranty_bit=0 (Knox never tripped)

### Display CONFIRMS the DTS research
fb0 modes = "U:1600x2560p-0", virtual_size 1600,5120, 32bpp
-> native 1600x2560 portrait = 2x 800x2560 bonded DSI. Matches R63319 dtsi exactly.

### Real hardware inventory (i2c driver bindings)
- Touchscreen: synaptics_rmi4_i2c / "rmi4-ts"  <-- SYNAPTICS, *not* Atmel.
  (earlier atmel_mxt_ts.kl keylayout was a shared/leftover file - misleading)
- Accel+Gyro: mpu6515 (inv-mpu-iio)
- Light/prox: gp2a030a
- Grip:       sx9500-i2c
- Backlight:  lp8556_backlight  (TI LP8556)
- MUIC:       max77888
- Fuelgauge:  sec-fuelgauge
- MHL:        sii8240
- IR:         barcode_hlte  <-- driver literally named for hlte (Note 3). Confirms kinship.
- WiFi/BT:    Qualcomm WCNSS "pronto" (wlan.ko + WCNSS_CTRL) -> mainline wcn36xx
              NOT Broadcom. Differs from hammerhead.

### Mainline driver mapping
syna,rmi4-i2c      OK (already in hlte+hammerhead)
inv_mpu6050        OK (hammerhead uses invensense,mpu6515)
sx9500             OK (IIO)
lp855x_bl          OK (supports LP8556)
wcn36xx            OK - targets WCNSS/pronto; verify msm8974 path
gp2ap020a00f       CHECK - gp2a030a variant may need work
max77888           CHECK - mainline has max77693/843, not 888
R63319 panel       WRITE IT (adapt panel-renesas-r63419.c)

### Partition map (msm_sdcc.1/by-name)
aboot p6, sbl1 p3, tz p8, rpm p7, boot p14, recovery p15, system p23,
cache p24, userdata p26, efs p11, param p10, persist p21

### PANEL WIRING FOUND (last unknown - resolved)
Both DSI halves share ONE physical panel:
  qcom,enable-gpio = <&pm8941_gpios 14 0>   (PMIC gpio, not TLMM)
  qcom,rst-gpio    = <&pm8941_gpios  9 0>
  right half -> mdss_dsi0 ; left half -> mdss_dsi1
  qcom,cont-splash-enabled = <1>   <-- bootloader lights the panel
Reset seq <1 5>,<0 12>,<1 12>; lp11-init; init delay 20000us
Off cmds: 28 (disp off, 20ms), 10 (sleep in, 50ms)
=> mainline: one drm_panel, one reset gpio, one enable gpio, bonded dsi0+dsi1.

## DRAFTS WRITTEN (port/draft/)
- panel-samsung-r63319.c    : mode table + on/off/reset, adapted from
  panel-renesas-r63419.c (which is ALREADY dual-DSI via mipi_dsi_dual()).
- mondrianwifi-panel.dtsi   : bonded mdss_dsi0+dsi1 + panel node + pm8941 pinctrl

### Timing cross-check (independent validation)
per-link 800x2560, htotal 1034, vtotal 2584, 60fps -> 160.31 MHz pixel clock
lane rate = 160.31M * 24bpp / 4 lanes = 961.9 MHz
downstream declares qcom,mdss-dsi-panel-clockrate = 964 MHz  -> 0.22% match. GOOD.
combined mode: 1600x2560, clock 320622 kHz, h 1600/1900/1940/2068, v 2560/2572/2576/2584
physical: 8.4" 16:10 -> 114 x 182 mm (portrait native)

### Reset polarity (subtle, got it wrong first pass)
downstream <1 5>,<0 12>,<1 12> = RAW pin high/low/high -> reset is ACTIVE LOW.
DT: GPIO_ACTIVE_LOW. Driver uses logical values 0,1,0 (NOT 1,0,1) so that the
line ends DEASSERTED. Using the raw numbers verbatim leaves the panel in reset.

### STILL UNVERIFIED in drafts (marked /* VERIFY */)
- vdda / vddio regulator rails for mdss_dsi0/1 + phys (guessed pm8941_l2 / l12)
- PM8941_GPIO_S3 power-source for the two pinctrl states
- whether MDP5 bonded DSI actually works (infra is generic in dsi_manager.c,
  but no mainline msm8974 board exercises it - we would be first)

## SESSION 2 - all placeholders resolved, drafts complete

### CRITICAL: correct mdss file is msm8974-mdss-sec.dtsi
The board include chain (31 files, resolved from apq8074-sec-mondrianwifi-r12.dts)
pulls msm8974-mdss-SEC.dtsi, NOT the generic msm8974-mdss.dtsi.
- generic file says reset=pm8941_gpios 19, enable=msmgpio 58, vdd=3.0V  <-- WRONG, not in chain
- sec file says     reset=pm8941_gpios  9, enable=pm8941_gpios 14, vdd=3.3V  <-- CORRECT
Also: qcom,rst-gpio / qcom,enable-gpio in the PANEL node are DEAD properties -
only qpic_panel_ili_qvga.c reads them. mdss_dsi.c reads qcom,platform-*-gpio
from the CONTROLLER node. Right answer, but via the controller node.

### Rails confirmed against LIVE sysfs (device booted)
  8941_l2  = 1.200V enabled   -> vdda
  8941_l10 = 1.850V enabled   -> touch vio
  8941_l12 = 1.800V enabled   -> vddio
  8941_l22 = 3.300V enabled   -> panel vdd + touch vdd
(tabpro-common overrides l22 min to 2.5V, but live operating point is 3.3V)
Extra: backlight PWM = pm8941_gpios 36 (LP8556); TE = msmgpio 12.

### Touchscreen = byte-identical to hlte
blsp1_i2c2 (i2c@f9924000), syna,rmi4-i2c @0x20, irq pm8941_gpios 30 EDGE_FALLING
Keys: home pm8941_gpios 3, voldn 2, volup 5 (all active low); hall_flip gpio 31.

### Boot image (from downstream mkbootimg.mk)
base 0x00000000, kernel 0x00008000, ramdisk 0x02900000, tags 0x02700000,
pagesize 2048, BOARD_KERNEL_SEPARATED_DT=true + DTBTOOL -2 => QCDT image.
(identical offsets to hammerhead)

### Firmware extracted to port/firmware/
  a330_pfp.fw, a330_pm4.fw            -> freedreno Adreno 330
  wlan/prima/WCNSS_qcom_wlan_nv.bin   -> wcn36xx NV data
  wlan/prima/WCNSS_cfg.dat
(/firmware/image is root-only; modem blobs not needed - Wi-Fi only device)

### Drafts complete, no VERIFY placeholders left
  draft/panel-samsung-r63319.c              276 lines, full driver
  draft/qcom-msm8974-samsung-mondrianwifi.dts  141 lines
  draft/mondrianwifi-panel.dtsi              96 lines
  draft/device-samsung-mondrianwifi/{deviceinfo,APKBUILD}
Structural check: brace/angle balance 0, no unterminated lines.
NOT yet compiled - dtc is not installed on this host, and the .c has never
seen a cross-compiler. "Structurally valid" != "builds".

### >>> HARDWARE GATE <<<
Need `heimdall print-pit` from Download mode to confirm the PIT partition
labels for deviceinfo_flash_heimdall_partition_kernel/rootfs. Currently
guessed as BOOT/SYSTEM. by-name says boot=mmcblk0p14, system=mmcblk0p23,
but PIT labels are not necessarily the by-name labels.

## PIT CONFIRMED (heimdall print-pit, device in Download mode 04e8:685d)
Header: Entry Count 30, COM_TAR2, CPU/bootloader tag MSM8974
  10  EFS       efs.img.ext4      28672   <- BACK UP BEFORE FLASHING
  13  BOOT      boot.img          20480   -> deviceinfo kernel partition
  14  RECOVERY  recovery.img      26624   -> where TWRP goes
  22  SYSTEM    system.img.ext4 5017600   -> deviceinfo rootfs partition
  26  PGPT      pgpt.img             34
Guessed BOOT/SYSTEM were CORRECT. All PIT names are UPPERCASE.
Captured 28/30 entries - piping to `head` SIGPIPE'd heimdall mid-dump.
Entries 27-29 not captured; entry 26 was PGPT so they are almost certainly
trailing GPT/reserved entries. Nothing we need.
NOTE: killing a heimdall session wedges the device's Odin protocol state -
subsequent print-pit fails with "Protocol initialisation failed" until the
tablet is power-cycled back into Download mode. Never pipe heimdall to head.

## EFS backup - chicken and egg
EFS cannot be dumped with heimdall (Odin is write-oriented; no partition read).
Backup requires root or TWRP => TWRP must be flashed FIRST, which trips Knox.
Order: flash TWRP -> boot TWRP -> back up EFS from there -> then experiment.
Mitigating: this is a Wi-Fi-only tablet, so EFS holds WLAN/BT MAC, not IMEI.
Stakes are lower than on a phone, but it is still unrecoverable if lost.

## TWRP FLASHED 2026-09-08
twrp-3.4.0-0-mondrianwifi  sha256 b65674668e39757e754f7b98581697144f47d52f01d7eb18a3588c0aff7e3c41
verified before flash: sha256+md5 match, ANDROID! magic, 11304976 B into
26624*512 = 13631488 B partition (2.3 MB headroom).
Command: heimdall flash --RECOVERY recovery.img --no-reboot   -> exit 0
Knox warranty_bit: 0 -> 1 (permanent, at this moment).

### HEIMDALL GOTCHAS ON THIS DEVICE (cost us 4 failed attempts)
1. Device accepts exactly ONE heimdall session per Download-mode entry.
   Any probe (even read-only print-pit) CONSUMES it. Go straight to the
   real command; do not "verify the session is up" first.
2. Never pipe heimdall to head/tail-with-early-close: SIGPIPE wedges the
   device's Odin protocol state. Redirect to a file instead.
3. --resume only works if a prior --no-reboot session is still open. On a
   freshly re-entered Download mode it fails with "Failed to begin session!".
4. heimdall takes the RAW .img, NOT the .img.tar. The tar is Odin's format.
5. Hub chain (3-2.3, two hubs deep) was NOT the problem - flash worked fine
   through it once the session was fresh.

## TWRP BOOTED + BACKUPS TAKEN 2026-09-08
TWRP 3.4.0-0 running as root (uid=0), adb state "recovery", ro.build.product=mondrianwifi.

### Why the first flash "failed"
It did NOT fail. heimdall wrote RECOVERY successfully both times. Android booted
at 22:24:20 and restored stock recovery via:
  /system/etc/install-recovery.sh   (1668 B)
  /system/recovery-from-boot.p      (2845012 B)
Both renamed to .bak from TWRP -> TWRP now survives an Android boot.
LESSON: on Samsung stock ROMs, never let Android boot between flashing a custom
recovery and first booting into it.

### Backups in scratchpad/backup/ (55 MB, all sha256-verified vs live device)
efs p11 (14680064, ext4, WLAN/BT MAC - irreplaceable), sbl1 p3, dbi p4, ddr p5,
aboot p6, rpm p7, tz p8, param p10, modemst1 p12, modemst2 p13, boot p14,
fsg p18, persist p21.  See backup/MANIFEST.txt.
NOT backed up: system p23 (2.4G) and userdata p26 - restore from stock firmware
instead if ever needed.

## SESSION 3 - IT COMPILES (2026-09-08)
Toolchain: clang 22.1.8 + LLVM=1, ARCH=arm. No GNU cross-toolchain needed.
Kernel: torvalds/linux shallow clone at 893e11787, multi_v7_defconfig.

### RESULTS
  DTB    qcom-msm8974-samsung-mondrianwifi.dtb   45212 bytes   BUILDS
  DRIVER panel-samsung-r63319.o                  ARM EABI5     BUILDS, clean at W=1
Patch of all kernel changes: draft/0001-mondrianwifi-port.patch (600 insertions).

### Bugs the compiler caught that review had not
1. pm8941_l22 / l2 / l12 / l10 / l20 / s3 were referenced but NEVER DECLARED.
   In mainline these are not labels in pm8941.dtsi - each board declares them
   under &rpm_requests { regulators-1 { ... } }. Added, with the live-measured
   voltages. Without this the DTS does not even parse.
2. Referenced pinctrl states sdc1_on/sdc1_off that do not exist. sdc1_off was
   invented outright; sdc1_on has to be defined per-board in &tlmm. Fixed.
3. mipi_dsi_dcs_write_seq_multi() does NOT take (dsi0, dsi1). The mipi_dsi_dual
   macro signature is mipi_dsi_dual(_func, _ctx, _dsi1, _dsi2, ...) - context
   FIRST. Every one of my dual calls had the wrong arity.
4. Missing <video/mipi_display.h> -> all MIPI_DCS_* constants undeclared.
5. There are dedicated helpers I did not know existed and should have used:
   mipi_dsi_dual_dcs_write_seq_multi() and mipi_dsi_dual_generic_write_seq_multi().
6. mipi_dsi_multi_context is initialised { 0 }, not { .dsi = ... }.

### Host workaround
`bc` is not installed and sudo needs a password. kernel/time/Kbuild uses bc only
to generate include/generated/timeconst.h. Wrote hostbin/bc - a ~50-line python
port of kernel/time/timeconst.bc, faithful for that one invocation. Verified:
HZ=100 -> HZ_TO_MSEC_MUL32 0xA0000000 >> 28 = 10 ms/tick. Correct.
NO LONGER NEEDED: real bc 1.08.2 installed 2026-09-08; shim output verified
byte-identical to real bc, then removed.

### STILL NOT PROVEN
Compiling is not booting. Untested: whether the panel driver's sequence actually
lights the panel, whether bonded DSI works on MDP5, whether the regulator and
GPIO assignments are right at runtime. No kernel image built yet, nothing flashed.

## IMPORTANT: use the msm8974-mainline fork, not torvalds/linux
pmaports builds linux-postmarketos-qcom-msm8974 (6.16.12) from
  https://github.com/msm8974-mainline/linux   branch qcom-msm8974-6.16.y
That fork carries msm8974 device trees BEFORE they reach mainline - e.g.
qcom-msm8974-samsung-lt03lte.dtb exists there and NOT in torvalds/linux.
That is the correct base for this port and where a mondrianwifi DTS should go.

## lt03lte is our closest precedent - and it has NO DISPLAY
device-samsung-lt03lte (Galaxy Note 10.1 2014, msm8974, Samsung, 2560x1600)
is a working pmOS port. Its DTS enables ONLY:
  &blsp1_i2c2 &blsp1_i2c6 &blsp2_i2c5 &blsp2_i2c6 &pm8941_gpios &rpm_requests
  &sdhc_1 &sdhc_3 &tlmm &usb &usb_hs1_phy
No mdss, no mdss_dsi, no panel node at all. Working: atmel maxtouch, ak8963,
bmi055, cm3323, max17050, wacom digitiser.
=> The one same-SoC same-vendor same-resolution device in the fork has NOT
   solved display. Bonded DSI on MDP5 is still unproven by anyone. This
   strengthens rather than weakens the earlier risk assessment.
=> It also shows a device is USEFULLY portable without display first.

## USB is the debug channel (added to our DTS)
This tablet has no exposed UART without disassembly, and display is the thing
we cannot yet trust. USB networking from the pmOS initramfs is therefore the
ONLY way to learn whether a blind first boot worked.
Added &usb + &usb_hs1_phy (copied from lt03lte) plus the rails they need:
  pm8941_l6  = 1.8V     pm8941_l24 = 3.075V
Without this, a failed boot and a successful-but-headless boot look identical.

## Boot method for Samsung msm8974 in pmOS
lt03lte depends on lk2nd-msm8974 (GENERIC, not per-device) + mkbootimg +
msm-firmware-loader, and uses flash_method="fastboot" (lk2nd provides fastboot).
samsung-chagallwifi instead uses heimdall-bootimg with partition_kernel="BOOT".
Both are viable; lk2nd is the better path if it supports mondrianwifi.
Note: pmOS devices use deviceinfo_append_dtb="true", NOT bootimg_qcdt.
Our deviceinfo currently sets bootimg_qcdt="true" from the downstream
BOARD_KERNEL_SEPARATED_DT - REVISIT once the boot path is chosen.

## SCHEMA VALIDATION PASSES (dtschema 2026.6, in ./venv)
  make ARCH=arm LLVM=1 CHECK_DTBS=1 qcom/qcom-msm8974-samsung-mondrianwifi.dtb
Only remaining output is the pre-existing upstream warning
  l2-cache (cache): 'qcom,saw' was unexpected
which qcom-msm8974-samsung-hlte.dtb produces IDENTICALLY. Not ours.

### What validation caught that plain compilation did not
1. Board compatible was unregistered. Fixed by adding samsung,mondrianwifi to
   Documentation/devicetree/bindings/arm/qcom.yaml under the apq8074 enum.
2. Compatible had THREE entries ("samsung,mondrianwifi","qcom,apq8074",
   "qcom,msm8974") but the apq8074 schema takes two, and the precedent
   (qcom-apq8074-dragonboard) is 2-item. Now: "samsung,mondrianwifi","qcom,apq8074".
3. Panel had no binding at all. Wrote
   Documentation/devicetree/bindings/display/panel/samsung,r63319.yaml
   (dt-doc-validate clean).

## FULL KERNEL BUILDS
zImage 12,972,544 bytes (multi_v7_defconfig, clang 22.1.8, LLVM=1). No warnings
from our files. Note multi_v7 is a GENERIC config - fine for validation, wrong
for the device. A real image should use the pmOS msm8974 config.

## USB ADDED
&usb + &usb_hs1_phy + pm8941_l6 (1.8V) / pm8941_l24 (3.075V), from lt03lte.
dtb grew 45212 -> 45589 bytes.

## Current patch: draft/0001-mondrianwifi-port.patch, 740 insertions, 7 files
  qcom.yaml (+1), samsung,r63319.yaml (+101), dts Makefile (+1),
  mondrianwifi.dts (+335), panel Kconfig (+10), panel Makefile (+1),
  panel-samsung-r63319.c (+291)

## BLOCKED ON SUDO
pmbootstrap needs root for chroot ops, so a real bootable pmOS image cannot be
built from this session without a sudo password. Options:
  (a) user installs+runs pmbootstrap
  (b) hand-roll boot.img: needs mkbootimg + an initramfs that brings up the USB
      gadget, and the pmOS kernel config rather than multi_v7_defconfig

## SESSION 4 - pmaports integration (2026-09-08)

### CRITICAL: pmOS kernel is 6.16.12, our driver was written for ~7.x
linux-postmarketos-qcom-msm8974 builds v6.16.12-msm8974 from
github.com/msm8974-mainline/linux. Checked the API before assuming:
  MISSING in 6.16: mipi_dsi_dual(), mipi_dsi_dual_dcs_write_seq_multi(),
                   mipi_dsi_dual_generic_write_seq_multi/_write_multi(),
                   devm_drm_panel_add(), and panel-renesas-r63419.c itself
  PRESENT in 6.16: mipi_dsi_dcs_write_buffer_multi, mipi_dsi_generic_write_multi,
                   the *_seq_multi variants, mipi_dsi_multi_context,
                   devm_drm_panel_alloc, drm_connector_helper_get_modes_fixed,
                   drm_panel_add/drm_panel_remove
FIX: driver now defines its OWN r63319_dual / r63319_dcs_seq / r63319_gen_seq
macros on top of the base *_multi primitives, and uses drm_panel_add() plus an
explicit .remove calling drm_panel_remove(). One identical source file now
builds clean at W=1 against BOTH 6.16.12 and mainline. Verified, not assumed.

### pmaports changes (in ./pmaports, a real checkout of the upstream repo)
  NEW device/testing/device-samsung-mondrianwifi/{APKBUILD,deviceinfo}
  NEW device/testing/linux-postmarketos-qcom-msm8974/0001-add-samsung-mondrianwifi.patch
  MOD linux-postmarketos-qcom-msm8974/APKBUILD  (patch in source=, pkgrel 1->2,
      sha512sums regenerated)
  MOD config-postmarketos-qcom-msm8974.armv7    (CONFIG_DRM_PANEL_SAMSUNG_R63319=m)
Patch verified with `patch -p1 --dry-run` against a pristine 6.16.12 tree: applies
cleanly, 6 files. Tarball sha512 matches the one the APKBUILD already expected.

### deviceinfo decisions
bootimg_qcdt="true"        - downstream sets BOARD_KERNEL_SEPARATED_DT + DTBTOOL -2.
                             96 pmaports devices use this key; still supported in 3.11.
bootimg_append_seandroidenforce="true" - Samsung aboot wants this magic (18 devices).
flash_method="heimdall-bootimg", partition_kernel="BOOT" (PIT entry 13, confirmed).
screen 1600x2560 - native portrait, matches stock fb0 "U:1600x2560p-0".
create_initfs_extra="true" - we NEED the debug initramfs; USB is the only channel.
NOT using lk2nd yet (lt03lte does; adds a variable we do not need on first attempt).

### How to invoke pmbootstrap against THIS checkout
  pmbootstrap -p /home/amayorga/agents/tabpro84-port/pmaports \
              -w /home/amayorga/agents/tabpro84-port/pmb-work <cmd>
-p points at our pmaports, -w confines all chroots to the project dir.

### SAFETY
NEVER pass --sdcard to `pmbootstrap install`. This laptop has exactly one block
device (nvme0n1, LUKS) and it is the system disk. Use plain `pmbootstrap install`
to produce image FILES, then flash with heimdall as we already do.

## FIRST pmbootstrap install: kernel built fine, deviceinfo_dtb path was wrong
The kernel package BUILT and the patch applied cleanly (all 6 files, no fuzz):
  >>> linux-postmarketos-qcom-msm8974: 0001-add-samsung-mondrianwifi.patch
  patching file drivers/gpu/drm/panel/Kconfig ... (6 files)
  Build complete, 7m40s -> linux-postmarketos-qcom-msm8974-6.16.12-r2.apk
and our dtb IS in the apk. mkinitfs/boot-deploy then failed with:
  ERROR: Unable to find qcom/qcom-msm8974-samsung-mondrianwifi.dtb in /boot/dtbs*

CAUSE: I wrote deviceinfo_dtb="qcom/qcom-msm8974-samsung-mondrianwifi", copying
mainline's arch/arm/boot/dts/qcom/ SOURCE layout. But this kernel's dtbs_install
FLATTENS everything - all 64 dtbs sit directly in /boot/dtbs/:
  boot/dtbs/qcom-msm8974-samsung-mondrianwifi.dtb
Peer devices confirm the convention: lt03lte (same SoC, same vendor) uses
  deviceinfo_dtb="qcom-msm8974-samsung-lt03lte"
FIX: dropped the "qcom/" prefix. Source path != install path.

## bootimg_qcdt WAS WRONG - corrected to append_dtb
Second failure was:
  ERROR: File not found: /boot/dt.img, but 'deviceinfo_bootimg_qcdt' is set
(the earlier "Unable to find ...dtb" was gone, so the flat-dtb fix worked.)

WHY IT WAS WRONG: I set bootimg_qcdt="true" from the DOWNSTREAM Android
BoardConfig (BOARD_KERNEL_SEPARATED_DT=true, DTBTOOL -2). But we build a
MAINLINE kernel. Evidence gathered rather than assumed:
 - dtbTool builds a QCDT by reading qcom,msm-id / qcom,board-id from each dtb.
   Our dtb has ZERO such properties. So does upstream qcom-msm8974-samsung-hlte.
   Mainline simply does not carry QCDT metadata.
 - Every APKBUILD in pmaports that runs dtbTool lives in device/archived/ and is
   named linux-*-downstream. QCDT is a downstream-kernel concept.
 - Every MAINLINE msm8974 device (fairphone-fp2, lg-hammerhead, oneplus-bacon,
   samsung-lt03lte, sony-leo) uses append_dtb="true" + lk2nd-msm8974.

FIX: append_dtb="true", removed bootimg_qcdt, added partition_type="msdos"
(lk2nd does not do GPT subpartitions), pkgrel 1 -> 2.

## lk2nd is the bridge between stock aboot and a mainline kernel
main/lk2nd builds a GENERIC lk2nd-msm8974 subpackage (only htc-m8 and
lg-hammerhead need special variants, for offsets). It is built with LK2ND_QCDTBS,
i.e. lk2nd.img IS a QCDT image that stock Samsung aboot will accept; lk2nd then
provides fastboot and boots the mainline appended-dtb kernel.
NOT added as a dependency yet - first see whether stock aboot will take our
appended-dtb boot.img directly. Flashing BOOT is fully recoverable: Download mode
lives in aboot/sbl (untouched) and we hold a verified boot-p14.img backup.

################################################################################
## MILESTONE: postmarketOS BOOTS ON THE TABLET (2026-09-08 23:35)
################################################################################
Flashed ONLY boot.img to BOOT (heimdall, Android left intact). Result:
  23:35:09  04e8:685d disappeared      (left download mode)
  23:35:19  18d1:d001 appeared         (Linux USB gadget = OUR kernel)
  23:35:19  new net iface, host auto-configured 172.16.42.2/24
  ping 172.16.42.1 -> 3/3, ~2.0 ms
  telnet 172.16.42.1:23 -> postmarketOS debug shell, root

  postmarketOS debug shell
    Device: Samsung Galaxy Tab Pro 8.4 (Wi-Fi) (samsung-mondrianwifi)
    Kernel: 6.16.12    OS ver: edge    initrd: 3.12.3-r1
  Linux (none) 6.16.12 #3-postmarketos-qcom-msm8974 SMP PREEMPT armv7l

USB gadget identifies as: 18d1:d001 "Samsung Galaxy Tab Pro 8.4 (Wi-Fi)",
serial "postmarketOS" - strings straight out of our deviceinfo.

### CONFIRMED WORKING on first boot
  USB gadget + networking (our &usb / &usb_hs1_phy nodes)   YES
  gpio-keys (our pm8941_gpios 2/3/5 node)                   YES, registered
  Synaptics touchscreen driver bound (i2c name "rmi4-i2c")  YES
  eMMC: mmcblk0 ~15 GB with partitions                      YES
  Stock aboot ACCEPTS a non-QCDT appended-dtb boot.img      YES  <-- lk2nd NOT needed
Kernel cmdline confirms: samsung.hardware=SM-T320, board_rev=12,
androidboot.warranty_bit=1 (Knox tripped), lcd_id=0x5f1460, mode=charger.

### DISPLAY FAILED - and it is an UPSTREAM KERNEL BUG, not our port
  msm_dsi fd922e00.dsi: unable to identify DSI host index
  msm_dsi fd922e00.dsi: probe with driver msm_dsi failed with error -22

dsi_host_get_id() matches a controller's reg base against a per-SoC table.
drivers/gpu/drm/msm/dsi/dsi_cfg.c, msm8974_apq8084_dsi_cfg:
    .io_start = { { 0xfd922800, 0xfd922b00 } },   <-- WRONG
0xfd922b00 is NOT a DSI controller. It is a PLL range inside
mdss_dsi0_phy@fd922a00 (reg = <0xfd922a00 0xd4>, <0xfd922b00 0x280>, ...).
The real DSI1 controller is 0xfd922e00 (mdss_dsi1: dsi@fd922e00,
reg = <0xfd922e00 0x1f8>) - per mainline's OWN device tree.
So DSI0 probes, DSI1 returns -EINVAL, the bonded pair never forms, no card0.

VERIFIED IDENTICAL in v6.16.12-msm8974 AND current torvalds/linux master.
Never caught because every mainline msm8974 board is a single-DSI phone; this
tablet is the first to use DSI1. One-line fix, upstreamable:
    .io_start = { { 0xfd922800, 0xfd922e00 } },

### Staged for next round
  dsi_cfg.c fix folded into 0001-add-samsung-mondrianwifi.patch (now 7 files)
  CONFIG_DRM_PANEL_SAMSUNG_R63319: m -> y (present at initramfs time)
  kernel pkgrel 2 -> 3, all sha512sums regenerated

## SECOND UPSTREAM BUG: DSI1 interrupt collides with DSI0
After the dsi_cfg.c io_start fix, the error MOVED (good sign):
  before: msm_dsi fd922e00.dsi: unable to identify DSI host index   -EINVAL(-22)
  after:  msm_dsi fd922e00.dsi: failed to request IRQ58             -EBUSY (-16)

CAUSE: arch/arm/boot/dts/qcom/qcom-msm8974.dtsi gives BOTH DSI controllers the
same MDSS interrupt:
  mdss_dsi0: dsi@fd922800 { interrupt-parent = <&mdss>; interrupts = <4>; }
  mdss_dsi1: dsi@fd922e00 { interrupt-parent = <&mdss>; interrupts = <4>; }  <-- WRONG
The mdss node is an interrupt-controller whose hwirqs are bit positions in
REG_MDSS_HW_INTR_STATUS (see msm_mdss_irq() in drivers/gpu/drm/msm/msm_mdss.c).
DSI1 is bit 5, not 4. Confirmed against three SoCs that actually use both DSIs:
  msm8996.dtsi   dsi0 interrupts=<4>   dsi1 interrupts=<5>
  msm8998.dtsi   dsi0 interrupts=<4>   dsi1 interrupts=<5>
  sdm660.dtsi                          dsi1 interrupts=<5>
FIX: mdss_dsi1 interrupts = <5>. Same root cause as the io_start bug - DSI1 on
msm8974 has never been exercised, so both copy-paste errors survived upstream.
(NOTE: qcom-msm8226.dtsi also carries the 0xfd922b00 value - may have the same
class of bug, not investigated.)

Patch now 8 files. Kernel pkgrel 3 -> 4.

## Two upstream fixes worth submitting once display works
 1. drm/msm/dsi: correct msm8974 DSI1 base address (0xfd922b00 -> 0xfd922e00)
 2. ARM: dts: qcom: msm8974: fix DSI1 interrupt (4 -> 5)
Both are one-liners, both blocked dual-DSI on this SoC entirely.

## BONDED DSI NOW ACTIVE - connector count proves it
After adding qcom,dual-dsi-mode / qcom,master-dsi / qcom,sync-dual-dsi and
reparenting DSI1's byte/pixel clocks onto mdss_dsi0_phy:
  before: card0  card0-DSI-1  card0-DSI-2   (two independent connectors)
  after:  card0  card0-DSI-1                (one connector = bonded pair)
"failed to enable link clocks -22" is GONE. The clock rates are now in range
because each host drives half the mode.

## MY BUG: missing panel.prepare_prev_first
Remaining error was:
  panel-samsung-r63319 fd922800.dsi.0: Failed to mipi_dsi_dcs_soft_reset: -22
  dsi_err_worker: status=4 / status=6  (flooding ~50k/s)
dsi_host_transfer() starts with:
    if (!msg || !msm_host->power_on) return -EINVAL;
so every DCS command from our prepare() was rejected - the DSI host was not
powered yet. DRM has a flag for exactly this:
    ctx->panel.prepare_prev_first = true;   /* drm_panel.h */
  "the DSI host controller should be initialised to LP-11 before the panel is
   powered up" - which is literally qcom,mdss-dsi-lp11-init from the downstream
   panel DTS, a property I recorded in these notes hours ago and then ignored.
panel-renesas-r63419.c (the driver I adapted) sets it on line 301. I dropped it.
38 upstream panel drivers set it.

Error bits decoded (dsi_host.c): status=4 = DSI_ERR_STATE_FIFO,
status=6 = FIFO|DLN0_PHY -> MDP streaming video at an uninitialised panel.

## OPEN, if the panel still does not light after this
Downstream sets qcom,mdss-dsi-on-command-state = "dsi_hs_mode" (commands in
HIGH SPEED). Our driver sets MIPI_DSI_MODE_LPM (low power). Deliberately NOT
changed this round - prepare_prev_first alone explains the -EINVAL exactly, and
changing two things at once destroys attribution. Next candidate if needed.

## PANEL INIT NOW SUCCEEDS - remaining fault is an MDP UNDERRUN
prepare_prev_first fixed the command path. This boot:
  - NO "Failed to mipi_dsi_dcs_soft_reset" - our DCS sequence was delivered
  - NO "Power on failed" / "failed to enable link clocks"
  - NO flip_done / commit timeouts
  - /sys/class/drm/card0-DSI-1/status = connected
  - /sys/class/drm/card0-DSI-1/modes  = 1600x2560
  - [drm] fb0: msmdrmfb frame buffer device   <-- framebuffer created
Screen still black. Remaining errors:
  [drm:mdp5_irq_error_handler] *ERROR* errors: 04000000
  dsi_err_worker: status=4
0x04000000 = MDP5_IRQ_INTF1_UNDER_RUN (generated/mdp5.xml.h:181)
status=4    = DSI_ERR_STATE_FIFO (dsi_host.c:91)
Both say the same thing: pixels are not reaching the DSI link fast enough.

### Cause: our mode is 0.2% over what the MDP can source
mdp5_kms.c:742 does, unconditionally:
    clk_set_rate(mdp5_kms->core_clk, config->hw->max_clk);
msm8x74v2_config.max_clk = 320000000. Our mode wanted 320622 kHz. Over cap.
SMP is NOT the problem: 22 MMBs x 4096, and an SMP shortage prints
"out of blks (req=%d > avail=%d)", which never appeared.

FIX: trim the per-link front porch 150 -> 140, htotal 2068 -> 2048,
clock 320622 -> 317522 kHz (~0.8% headroom). Lane rate 962 -> 953 MHz.
Porch length is not panel-critical; this deviates from downstream deliberately.
Kernel pkgrel -> 7.

### If this does not fix it, next candidates in order
 1. MIPI_DSI_MODE_LPM vs downstream qcom,mdss-dsi-on-command-state="dsi_hs_mode"
 2. PHY timings: downstream specifies qcom,mdss-dsi-panel-timings =
    [F9 50 42 00 6D 80 38 50 46 03 04 A0]; mainline computes these itself
    (dsi_phy_timing_calc) and may get them wrong for this panel.
 3. Backlight: LP8556 on pm8941_gpios 36 is NOT wired up at all yet - the panel
    could be initialised and scanning correctly with the backlight simply off,
    which would look exactly like "black screen".

## THE SCREEN IS SCANNING - THERE IS JUST NO BACKLIGHT
After the mode trim, debug shell shows:
  /sys/class/drm/card0-DSI-1/enabled  = enabled
  /sys/kernel/debug/dri/0/state       -> crtc=crtc-0, crtc-pos=1600x2560+0+0
  /sys/class/backlight/               -> DOES NOT EXIST. No backlight device at all.
A plane is assigned to crtc-0 at full panel resolution and the connector is
enabled. The pipeline is live. An LCD with no backlight is black regardless.

Downstream wiring (msm8974-sec-mondrian-r12.dtsi):
  lp8556_backlight@2C {
      compatible = "lp8556,backlight-control";
      reg = <0x2C>;
      lp8556,en-gpio  = <&pm8941_gpios 19 0x00>;
      lp8556,scl-gpio = <&msmgpio 11 0x00>;
      lp8556,sda-gpio = <&msmgpio 10 0x00>;
  };
It is on a BIT-BANGED i2c-gpio bus, not a hardware QUP - which is exactly why
nothing ever probed it. (Also note gpio 19 is the BACKLIGHT ENABLE. The generic
msm8974-mdss.dtsi lists gpio 19 as panel reset; more evidence that file is not
for this board.)

ADDED: regulator-fixed backlight_en (pm8941_gpios 19) + i2c-gpio bus
(sda tlmm 10, scl tlmm 11) + backlight@2c "ti,lp8556" (dev-ctrl 0x85,
init-brt 0x80 per the mainline binding example) + backlight = <&backlight>
on the panel node. CONFIG_I2C_GPIO m -> y (=m would not be in the initramfs).
CONFIG_BACKLIGHT_LP855X was already =y. pkgrel -> 8.

NOTE: dev-ctrl 0x85 is the value from the binding's own lp8556 example; the
correct strapping for THIS board is unverified. If the panel lights but looks
wrong, that byte is the first thing to try changing.

## Also seen, probably separate and cosmetic
  fb0: sys_imageblit: framebuffer is not in virtual address space
  fb0: sys_fillrect:  framebuffer is not in virtual address space
DRM fbdev emulation cannot draw into the physically-contiguous VRAM carveout
("no IOMMU, fallback to phys contig buffers for scanout"). This would stop the
CONSOLE from rendering, but does not stop the panel scanning. Deal with it after
the backlight question is settled.

################################################################################
## BACKLIGHT WORKS - PANEL IS LIT AND SCANNING (2026-09-09 ~00:40)
################################################################################
After adding the i2c-gpio bus + ti,lp8556:
  /sys/class/backlight/lcd-bl/  brightness=128 max=255 bl_power=0
  USER CONFIRMS: "the back light seems to be on"
  /sys/class/graphics/fb0/virtual_size = 1600,2560   bits_per_pixel = 32
  /sys/kernel/debug/dri/0/framebuffer:
     framebuffer[86] allocated by [fbcon], XR24, 1600x2560, pitch 6400 (=1600*4)
  connector card0-DSI-1 = connected + enabled, crtc-0 @ 1600x2560+0+0

So: bonded DSI up, panel initialised, backlight lit, CRTC scanning a correctly
formatted 1600x2560 XR24 buffer. The whole chain exists.

## REMAINING: fbdev emulation cannot write the scanout buffer
  fb0: sys_imageblit: framebuffer is not in virtual address space
  fb0: sys_fillrect:  framebuffer is not in virtual address space
Writing white to /dev/fb0 succeeds at the syscall level but does NOT change the
display. Root cause is upstream of us:
  [drm] no IOMMU, fallback to phys contig buffers for scanout
  [drm] using 16m VRAM carveout   (VRAM: 70100000->71100000)
msm DRM without an IOMMU scans out of a physically contiguous carveout that the
DRM fbdev helper cannot vmap, so fbcon/console drawing is broken. Writes land in
a shadow buffer that is never flushed to the panel.
/dev/mem bypass is blocked (CONFIG_STRICT_DEVMEM) - 0 bytes written.

### This is NOT a port bug and NOT a panel bug
The panel, DSI, PHY, clocks, mode and backlight are all working. Only the
LEGACY fbdev console path is broken. A native DRM/KMS client (modetest, or any
compositor) allocates its own dumb buffer and does a real atomic modeset, which
does not go through the broken fbdev vmap path.

### NEXT STEP: flash the rootfs and test with real DRM userspace
  pmbootstrap ... shutdown        (clears the 21 stale mounts that break losetup)
  pmbootstrap ... install         (produces samsung-mondrianwifi.img, 659M)
  heimdall flash --SYSTEM <img>   (PIT entry 22, 2.57 GB - our 659M fits)
then from a real rootfs:
  modetest -M msm -s <connector>@<crtc>:1600x2560   -> should paint test pattern
WARNING: flashing SYSTEM DESTROYS the stock Android install. We never backed up
SYSTEM (deliberately - 2.4 GB, restorable from stock firmware via SamMobile).
Everything up to now has been BOOT-only and fully reversible.

################################################################################
## WHY THE SCREEN IS STILL BLACK: MDP5 DOES NOT IMPLEMENT BONDED DSI
################################################################################
Removing MIPI_DSI_CLOCK_NON_CONTINUOUS did NOT change anything: FIFO error rate
stayed at ~246k/5s (~49k/s). That hypothesis is dead.

The real blocker is architectural, in mainline:

drivers/gpu/drm/msm/disp/mdp5/mdp5_ctl.c:733
  /* In bonded DSI case, CTL0 and CTL1 are always assigned to two DSI
   * interfaces to support single FLUSH feature ... to keep two DSI pipes in
   * sync. Single FLUSH is supported from hw rev v3.0. */
  if ((rev >= 3) && (dsi_cnt > 1)) { ctl_mgr->single_flush_supported = true; ...

Our hardware logs:  [drm:mdp5_kms_init] MDP5 version v1.2   -> rev 1, NOT >= 3.
So single_flush_supported = false, and every boot logs:
  fall back to the other CTL category for INTF 1!
  fall back to the other CTL category for INTF 2!

Grep across the whole msm driver:
  DPU  (sdm845/sm8150/sm8250): 'bonded' x4 plus _split_ handling in dpu_crtc.c,
       dpu_kms.c, dpu_encoder_phys.h, dpu_hw_top.c
  MDP5 (ours):                 'bonded' x1 (a COMMENT), 'split' x0
Every mainline board with qcom,dual-dsi-mode is a DPU-era SoC. NO MDP5 device
anywhere in mainline uses bonded DSI.

=> The DSI manager bonds the two hosts fine (we proved that: one connector,
   correct clocks, panel init succeeds, backlight on). But MDP5 never splits the
   image across the two interfaces, so the link is fed wrongly and
   REG_DSI_FIFO_STATUS errors continuously. No image.

=> Downstream Android DID drive this panel dual-DSI on this exact silicon, so the
   HARDWARE can do it. mainline MDP5 simply has not implemented the display-
   controller half of bonded DSI for pre-v3.0 revisions.

### This is not a configuration problem. Options:
 (a) Implement bonded/split-display support in mainline MDP5. Real kernel work
     (slave encoder, dual-CTL programming, sync without single-FLUSH). Weeks.
     Would be a genuinely novel upstream contribution.
 (b) Try driving ONE DSI link only at 800x2560. Cheap test. Uncertain: the
     R63319 is strapped for dual-link input and may not scan from one link.
     If it DOES light half the panel, that proves the whole rest of the chain.
 (c) Accept headless for now. The device is already a working Linux machine
     over USB networking with a root shell.

## *** SCREEN WORKS *** (cont-splash adoption)
Text visible on panel. Connector connected, mode 800x2560, fb0 live,
dsi_err_worker count stays at 1, one underrun at modeset. No flood.

### The actual bug was ours, and it hid behind a lying read
  ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
DT declares reset as GPIO_ACTIVE_LOW, so GPIOD_OUT_HIGH = ASSERTED. The driver
held the panel in reset from probe onward and tore down a panel that aboot had
already initialised and lit (vendor DTS: qcom,cont-splash-enabled = <1>).

FIX: request both reset and enable as GPIOD_ASIS, and in prepare/unprepare
adopt the bootloader panel - no enable toggle, no reset, no init sequence.
Behind module param inherit_splash (default true).

### Why this took so many cycles: mipi_dsi_dcs_read returns 0x00 on silence
A panel held in reset does not answer. msm returned SUCCESS with a zero-filled
buffer, so "= 0x00" was indistinguishable from a real reply of zero. Chased
several dead hypotheses on that basis:
  - "writes do not stick"      -> panel was in reset, could not act OR answer
  - "needs both DSI links"     -> bonded read 0x00 was silence, not evidence
  - "LP vs HS transfer mode"   -> both read 0x00 for the same reason
Once reset was released, power_mode@entry = 0x1c (sleep_out=1, display_on=1).
RULE: treat a 0x00 DCS read as NO REPLY until proven otherwise. Read a register
with a known-nonzero default to prove the link before trusting any read.

### Confirmed dead ends (do not revisit)
  - init sequence transcription: verified byte-for-byte against vendor
    dsi_panel_samsung_2560p_video_R63319.dtsi (CyanogenMod cm-14.1). Only
    difference is two 00 00 NOPs, which are padding. Sequence was never wrong.
  - transfer mode: vendor says dsi_hs_mode; tested LP and HS in one boot, no
    difference (both were silence).
  - GPIO/rail wiring: gpio9 out high, gpio14 out high, l22 3.3V, l12 1.8V,
    l2 1.2V - all verified live and all correct.

### Still open
  - Only 800 of 1600 columns: one DSI link. Full width needs MDP5 bonded DSI,
    which MDP5 does not implement (mdp5_encoder.c never halves the INTF timing;
    contrast dpu_encoder_phys_vid.c:283 "mode.hdisplay >>= 1"). r_mixer plumbing
    already exists in mdp5_ctl.c/mdp5_crtc.c/mdp5_mixer.c.
  - Console font tiny: 800x2560 at 8x16 = 100x160 chars on an 8.4" panel.
  - Adoption depends on aboot lighting the panel. A proper reset+init path is
    still unsolved; inherit_splash=0 still yields a dark panel.
