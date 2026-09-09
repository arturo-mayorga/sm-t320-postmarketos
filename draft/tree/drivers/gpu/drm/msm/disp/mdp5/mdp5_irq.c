// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/pm_runtime.h>

#include <drm/drm_print.h>
#include <drm/drm_vblank.h>

#include "msm_drv.h"
#include "mdp5_kms.h"

/*
 * mondrianwifi vsync diagnostics.
 *
 * On this panel INTF1's timing engine demonstrably runs -- FRAME_COUNT and
 * LINE_COUNT both advance at 60Hz -- and INTF1_UNDER_RUN (bit 26) reaches the
 * CPU, so MDP -> MDSS -> GIC is intact. Yet INTF1_VSYNC (bit 27), the adjacent
 * bit in the very same register, never arrives, so no page flip ever
 * completes. The one thing that cannot be observed from userspace on ARM32
 * (/dev/mem is restricted to physical RAM by valid_phys_addr_range) is the
 * MDP register file itself, which is exactly what is needed to tell a driver
 * arming bug from silent hardware.
 *
 * Writing to /sys/module/msm/parameters/mdp5_dump samples INTR_STATUS eight
 * times across ~24ms -- more than one 16.6ms frame -- without clearing it.
 * INTR_STATUS latches until written to INTR_CLEAR, and the handler only ever
 * clears (status & enable), so if the hardware raises INTF1_VSYNC at all it
 * has to show up here.
 */
static struct mdp5_kms *mdp5_dbg_kms;

static int mdp5_dump_set(const char *val, const struct kernel_param *kp)
{
	struct mdp5_kms *mdp5_kms = mdp5_dbg_kms;
	struct device *dev;
	u32 en, st, line;
	int i;

	if (!mdp5_kms)
		return -ENODEV;

	dev = &mdp5_kms->pdev->dev;
	pm_runtime_get_sync(dev);

	en = mdp5_read(mdp5_kms, REG_MDP5_INTR_EN);
	st = mdp5_read(mdp5_kms, REG_MDP5_INTR_STATUS);

	pr_info("mdp5dump: INTR_EN=%08x [INTF1_VSYNC=%d INTF1_UNDER=%d] INTR_STATUS=%08x\n",
		en, !!(en & MDP5_IRQ_INTF1_VSYNC), !!(en & MDP5_IRQ_INTF1_UNDER_RUN), st);
	pr_info("mdp5dump: INTF1 timing_en=%u flcnt_en=%u frame=%u line=%u\n",
		mdp5_read(mdp5_kms, REG_MDP5_INTF_TIMING_ENGINE_EN(1)),
		mdp5_read(mdp5_kms, REG_MDP5_INTF_FRAME_LINE_COUNT_EN(1)),
		mdp5_read(mdp5_kms, REG_MDP5_INTF_FRAME_COUNT(1)),
		mdp5_read(mdp5_kms, REG_MDP5_INTF_LINE_COUNT(1)));

	/* sweep a full frame without clearing, watching bit 27 latch or not */
	for (i = 1; i <= 8; i++) {
		usleep_range(3000, 3200);
		st = mdp5_read(mdp5_kms, REG_MDP5_INTR_STATUS);
		line = mdp5_read(mdp5_kms, REG_MDP5_INTF_LINE_COUNT(1));
		pr_info("mdp5dump: ~%2dms status=%08x vsync=%d line=%u\n",
			i * 3, st, !!(st & MDP5_IRQ_INTF1_VSYNC), line);
	}

	pm_runtime_put_sync(dev);
	return 0;
}

static const struct kernel_param_ops mdp5_dump_ops = {
	.set = mdp5_dump_set,
};
module_param_cb(mdp5_dump, &mdp5_dump_ops, NULL, 0200);

/*
 * Generic MDP register window, because every split-display question is "what
 * did CTL1 / INTF2 / LM1 actually end up with?" and /dev/mem cannot reach the
 * register file on ARM32.
 *
 *   echo 0x21400      > /sys/module/msm/parameters/mdp5_peek   # 16 words
 *   echo 0x21400,64   > /sys/module/msm/parameters/mdp5_peek   # 64 words
 *   echo 0x2f4=1      > /sys/module/msm/parameters/mdp5_poke   # write
 *
 * Offsets are relative to the MDP base (same space as mdp5.xml.h).
 */
static int mdp5_peek_set(const char *val, const struct kernel_param *kp)
{
	struct mdp5_kms *mdp5_kms = mdp5_dbg_kms;
	struct device *dev;
	unsigned int off, count = 16, i;
	char buf[64];
	char *p, *cnt;

	if (!mdp5_kms)
		return -ENODEV;
	strscpy(buf, val, sizeof(buf));
	p = strim(buf);
	cnt = strchr(p, ',');
	if (cnt) {
		*cnt++ = '\0';
		if (kstrtouint(cnt, 0, &count))
			return -EINVAL;
	}
	if (kstrtouint(p, 0, &off) || off & 3 || count > 256)
		return -EINVAL;

	dev = &mdp5_kms->pdev->dev;
	pm_runtime_get_sync(dev);
	for (i = 0; i < count; i += 4) {
		u32 a = off + i * 4;
		u32 v[4] = { 0 };
		unsigned int j;

		for (j = 0; j < 4 && i + j < count; j++)
			v[j] = mdp5_read(mdp5_kms, a + j * 4);
		pr_info("mdp5peek: %05x: %08x %08x %08x %08x\n",
			a, v[0], v[1], v[2], v[3]);
	}
	pm_runtime_put_sync(dev);
	return 0;
}

static const struct kernel_param_ops mdp5_peek_ops = {
	.set = mdp5_peek_set,
};
module_param_cb(mdp5_peek, &mdp5_peek_ops, NULL, 0200);

static int mdp5_poke_set(const char *val, const struct kernel_param *kp)
{
	struct mdp5_kms *mdp5_kms = mdp5_dbg_kms;
	struct device *dev;
	unsigned int off, v;
	char buf[64];
	char *p, *eq;

	if (!mdp5_kms)
		return -ENODEV;
	strscpy(buf, val, sizeof(buf));
	p = strim(buf);
	eq = strchr(p, '=');
	if (!eq)
		return -EINVAL;
	*eq++ = '\0';
	if (kstrtouint(p, 0, &off) || off & 3 || kstrtouint(eq, 0, &v))
		return -EINVAL;

	dev = &mdp5_kms->pdev->dev;
	pm_runtime_get_sync(dev);
	mdp5_write(mdp5_kms, off, v);
	pr_info("mdp5poke: %05x <= %08x (reads back %08x)\n", off, v,
		mdp5_read(mdp5_kms, off));
	pm_runtime_put_sync(dev);
	return 0;
}

static const struct kernel_param_ops mdp5_poke_ops = {
	.set = mdp5_poke_set,
};
module_param_cb(mdp5_poke, &mdp5_poke_ops, NULL, 0200);

/*
 * Read back what the display is actually scanning out.
 *
 * "Is the panel dark or is the compositor drawing black?" cannot be answered
 * from userspace here: grim hangs, and /dev/mem cannot reach the scanout
 * because the VRAM carveout sits above lowmem, so ARM32's
 * valid_phys_addr_range() rejects it. But the MDP itself holds the scanout
 * address in the pipe's SRC0_ADDR register, and the carveout is ordinary
 * reserved RAM, so memremap() can read it directly.
 *
 * Writing to /sys/module/msm/parameters/mdp5_fbdump samples pixels down the
 * middle of the framebuffer and reports how many are non-black.
 */
static int mdp5_fbdump_set(const char *val, const struct kernel_param *kp)
{
	struct mdp5_kms *mdp5_kms = mdp5_dbg_kms;
	struct device *dev;
	u32 base, stride, i, nonzero = 0;
	void *p;

	if (!mdp5_kms)
		return -ENODEV;

	dev = &mdp5_kms->pdev->dev;
	pm_runtime_get_sync(dev);
	base   = mdp5_read(mdp5_kms, REG_MDP5_PIPE_SRC0_ADDR(SSPP_DMA0));
	stride = mdp5_read(mdp5_kms, REG_MDP5_PIPE_SRC_STRIDE_A(SSPP_DMA0));
	pm_runtime_put_sync(dev);

	pr_info("mdp5fb: DMA0 scanout base=%08x stride=%08x\n", base, stride);
	if (!base)
		return 0;

	stride &= 0xffff;
	if (!stride)
		stride = 3200;

	p = memremap(base, 8 << 20, MEMREMAP_WB);
	if (!p) {
		pr_info("mdp5fb: memremap(%08x) failed\n", base);
		return 0;
	}

	for (i = 0; i < 32; i++) {
		u32 row = i * 80;
		u32 off = row * stride + 400 * 4;	/* column 400 */
		u32 px;

		if (off + 4 > (8u << 20))
			break;
		px = *(volatile u32 *)((char *)p + off);
		if (px & 0x00ffffff)
			nonzero++;
		if (i < 8)
			pr_info("mdp5fb: row %4u px=%08x\n", row, px);
	}

	pr_info("mdp5fb: %u of 32 sampled pixels are non-black\n", nonzero);
	memunmap(p);
	return 0;
}

static const struct kernel_param_ops mdp5_fbdump_ops = {
	.set = mdp5_fbdump_set,
};
module_param_cb(mdp5_fbdump, &mdp5_fbdump_ops, NULL, 0200);

/*
 * Dump the DSI host's video-mode programming.
 *
 * Everything from the scanout buffer down to the MDP INTF is verified: the
 * buffer holds correct pixels, INTF1's timing engine counts frames at 60Hz,
 * vblank interrupts arrive, the DSI link answers DCS reads, and the panel
 * reports sleep_out=1 display_on=1 -- yet the glass stays dark. The one layer
 * with no visibility is what the DSI host is actually programmed to transmit.
 *
 * This panel is physically 1600x2560 fed by two DSI links carrying 800
 * columns each (downstream sets qcom,mdss-dsi-panel-width = <800>), and we
 * drive a single link. If the host's ACTIVE_H does not agree with the INTF's
 * 800, no line ever assembles and the panel shows nothing while looking
 * perfectly healthy over the control path.
 *
 * DSI0 is at a fixed address on msm8974, so ioremap reaches it without having
 * to reach into the msm_dsi driver's private state.
 */
#define MONDRIAN_DSI0_BASE	0xfd922800

/*
 * On DSI 6G, offset 0 is the 6G_HW_VERSION register and every other register
 * is shifted down by 4 (dsi_cfg.h: DSI_6G_REG_SHIFT). Reading the raw
 * dsi.xml.h offsets therefore lands one word low and makes an enabled
 * controller look disabled: DSI_CTRL reads back as the version word
 * 0x10010000 (6G v1.1, which is msm8974) with its ENABLE bit clear, while the
 * real DSI_CTRL sits in what looks like STATUS0.
 */
#define DSI6G(reg)		((reg) + 4)

static int dsi_dump_set(const char *val, const struct kernel_param *kp)
{
	void __iomem *p;
	u32 ctrl, ah, av, tot, hs, vh, vv, lane, st;

	p = ioremap(MONDRIAN_DSI0_BASE, 0x400);
	if (!p) {
		pr_info("dsidump: ioremap failed\n");
		return 0;
	}

	pr_info("dsidump: 6G_HW_VERSION=%08x\n", readl(p + 0x0000));

	ctrl = readl(p + DSI6G(0x0000));	/* REG_DSI_CTRL */
	st   = readl(p + DSI6G(0x0004));	/* REG_DSI_STATUS0 */
	ah   = readl(p + DSI6G(0x0020));	/* REG_DSI_ACTIVE_H */
	av   = readl(p + DSI6G(0x0024));	/* REG_DSI_ACTIVE_V */
	tot  = readl(p + DSI6G(0x0028));	/* REG_DSI_TOTAL */
	hs   = readl(p + DSI6G(0x002c));	/* REG_DSI_ACTIVE_HSYNC */
	vh   = readl(p + DSI6G(0x0030));	/* REG_DSI_ACTIVE_VSYNC_HPOS */
	vv   = readl(p + DSI6G(0x0034));	/* REG_DSI_ACTIVE_VSYNC_VPOS */
	lane = readl(p + DSI6G(0x00a8));	/* REG_DSI_LANE_CTRL */

	pr_info("dsidump: CTRL=%08x [en=%d video=%d cmd=%d lanes=%x clk=%d] STATUS0=%08x LANE_CTRL=%08x\n",
		ctrl, ctrl & 1, !!(ctrl & 2), !!(ctrl & 4),
		(ctrl >> 4) & 0xf, !!(ctrl & 0x100), st, lane);
	pr_info("dsidump: ACTIVE_H=%08x -> start=%u end=%u width=%u\n",
		ah, ah & 0xfff, (ah >> 16) & 0xfff,
		(((ah >> 16) & 0xfff) - (ah & 0xfff)));
	pr_info("dsidump: ACTIVE_V=%08x -> start=%u end=%u height=%u\n",
		av, av & 0xfff, (av >> 16) & 0xfff,
		(((av >> 16) & 0xfff) - (av & 0xfff)));
	pr_info("dsidump: TOTAL=%08x -> h=%u v=%u\n",
		tot, tot & 0xffff, (tot >> 16) & 0xffff);
	pr_info("dsidump: HSYNC=%08x VSYNC_HPOS=%08x VSYNC_VPOS=%08x\n", hs, vh, vv);

	iounmap(p);
	return 0;
}

static const struct kernel_param_ops dsi_dump_ops = {
	.set = dsi_dump_set,
};
module_param_cb(dsi_dump, &dsi_dump_ops, NULL, 0200);

void mdp5_set_irqmask(struct mdp_kms *mdp_kms, uint32_t irqmask,
		uint32_t old_irqmask)
{
	mdp5_write(to_mdp5_kms(mdp_kms), REG_MDP5_INTR_CLEAR,
		   irqmask ^ (irqmask & old_irqmask));
	mdp5_write(to_mdp5_kms(mdp_kms), REG_MDP5_INTR_EN, irqmask);
}

static void mdp5_irq_error_handler(struct mdp_irq *irq, uint32_t irqstatus)
{
	struct mdp5_kms *mdp5_kms = container_of(irq, struct mdp5_kms, error_handler);
	static DEFINE_RATELIMIT_STATE(rs, 5*HZ, 1);
	extern bool dumpstate;

	DRM_ERROR_RATELIMITED("errors: %08x\n", irqstatus);

	if (dumpstate && __ratelimit(&rs)) {
		struct drm_printer p = drm_info_printer(mdp5_kms->dev->dev);
		drm_state_dump(mdp5_kms->dev, &p);
	}
}

void mdp5_irq_preinstall(struct msm_kms *kms)
{
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(kms));
	struct device *dev = &mdp5_kms->pdev->dev;

	pm_runtime_get_sync(dev);
	mdp5_write(mdp5_kms, REG_MDP5_INTR_CLEAR, 0xffffffff);
	mdp5_write(mdp5_kms, REG_MDP5_INTR_EN, 0x00000000);
	pm_runtime_put_sync(dev);
}

int mdp5_irq_postinstall(struct msm_kms *kms)
{
	struct mdp_kms *mdp_kms = to_mdp_kms(kms);
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(mdp_kms);
	struct device *dev = &mdp5_kms->pdev->dev;
	struct mdp_irq *error_handler = &mdp5_kms->error_handler;

	mdp5_dbg_kms = mdp5_kms;

	error_handler->irq = mdp5_irq_error_handler;
	error_handler->irqmask = MDP5_IRQ_INTF0_UNDER_RUN |
			MDP5_IRQ_INTF1_UNDER_RUN |
			MDP5_IRQ_INTF2_UNDER_RUN |
			MDP5_IRQ_INTF3_UNDER_RUN;

	pm_runtime_get_sync(dev);
	mdp_irq_register(mdp_kms, error_handler);
	pm_runtime_put_sync(dev);

	return 0;
}

void mdp5_irq_uninstall(struct msm_kms *kms)
{
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(kms));
	struct device *dev = &mdp5_kms->pdev->dev;

	pm_runtime_get_sync(dev);
	mdp5_write(mdp5_kms, REG_MDP5_INTR_EN, 0x00000000);
	pm_runtime_put_sync(dev);
}

irqreturn_t mdp5_irq(struct msm_kms *kms)
{
	struct mdp_kms *mdp_kms = to_mdp_kms(kms);
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(mdp_kms);
	struct drm_device *dev = mdp5_kms->dev;
	struct drm_crtc *crtc;
	uint32_t status, enable, raw;

	enable = mdp5_read(mdp5_kms, REG_MDP5_INTR_EN);
	raw = mdp5_read(mdp5_kms, REG_MDP5_INTR_STATUS);
	status = raw & enable;
	mdp5_write(mdp5_kms, REG_MDP5_INTR_CLEAR, status);

	/* underruns arrive ~1/s and give us a free sampling clock */
	pr_info_ratelimited("mdp5_irq: en=%08x raw=%08x handled=%08x\n",
			    enable, raw, status);

	VERB("status=%08x", status);

	mdp_dispatch_irqs(mdp_kms, status);

	drm_for_each_crtc(crtc, dev)
		if (status & mdp5_crtc_vblank(crtc))
			drm_crtc_handle_vblank(crtc);

	return IRQ_HANDLED;
}

int mdp5_enable_vblank(struct msm_kms *kms, struct drm_crtc *crtc)
{
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(kms));
	struct device *dev = &mdp5_kms->pdev->dev;

	pm_runtime_get_sync(dev);
	mdp_update_vblank_mask(to_mdp_kms(kms),
			mdp5_crtc_vblank(crtc), true);
	pm_runtime_put_sync(dev);

	return 0;
}

void mdp5_disable_vblank(struct msm_kms *kms, struct drm_crtc *crtc)
{
	struct mdp5_kms *mdp5_kms = to_mdp5_kms(to_mdp_kms(kms));
	struct device *dev = &mdp5_kms->pdev->dev;

	pm_runtime_get_sync(dev);
	mdp_update_vblank_mask(to_mdp_kms(kms),
			mdp5_crtc_vblank(crtc), false);
	pm_runtime_put_sync(dev);
}
