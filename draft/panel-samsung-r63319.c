// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samsung 1600x2560 dual-DSI video mode panel (Renesas R63319 controller)
 * as found in the Samsung Galaxy Tab Pro 8.4 (SM-T320, "mondrianwifi").
 *
 * DRAFT - derived from panel-renesas-r63419.c. Timings and init sequence
 * translated from the downstream Samsung MDSS device tree
 * (dsi_panel_samsung_2560p_video_R63319.dtsi).
 */

#include <linux/backlight.h>
#include <linux/bits.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include <video/mipi_display.h>

#include <drm/drm_connector.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

/*
 * The mipi_dsi_dual*() helpers only landed upstream after v6.16, but the
 * postmarketOS msm8974 kernel (linux-postmarketos-qcom-msm8974, currently
 * 6.16.12) is where this panel actually has to run. Define local equivalents
 * built solely on the *_multi primitives that exist in both, so this driver
 * compiles against either. Semantics match the upstream macros: issue the
 * same command to each link in turn, accumulating errors in one context.
 */
#define r63319_dual(_func, _ctx, _d0, _d1, ...)			\
	do {							\
		(_ctx)->dsi = (_d0);				\
		(_func)((_ctx), ##__VA_ARGS__);			\
		if (_d1) {					\
			(_ctx)->dsi = (_d1);			\
			(_func)((_ctx), ##__VA_ARGS__);		\
		}						\
	} while (0)

#define r63319_dcs_seq(_ctx, _d0, _d1, _cmd, _seq...)		\
	do {							\
		static const u8 _b[] = { _cmd, ##_seq };	\
		r63319_dual(mipi_dsi_dcs_write_buffer_multi,	\
			    _ctx, _d0, _d1, _b, ARRAY_SIZE(_b));\
	} while (0)

#define r63319_gen_seq(_ctx, _d0, _d1, _seq...)			\
	do {							\
		static const u8 _b[] = { _seq };		\
		r63319_dual(mipi_dsi_generic_write_multi,	\
			    _ctx, _d0, _d1, _b, ARRAY_SIZE(_b));\
	} while (0)

/*
 * The vendor DTS sets qcom,cont-splash-enabled: aboot initialises this panel
 * and leaves it lit and scanning. Taking it over rather than re-initialising
 * it avoids a reset we have never managed to drive the panel back out of.
 */
/*
 * Default changed to false: adopting the bootloader's panel looked attractive
 * (it is already lit, and cont-splash is set downstream) but it is a trap. The
 * MDP stops the timing engine on every DPMS blank, and this panel does not
 * survive the video stream going away -- it comes back with power_mode still
 * reporting 0x1c (sleep_out=1, display_on=1) but nothing on screen, and with
 * inherit_splash the driver never sends the commands that would recover it.
 * Cold init reproduces the vendor sequence in full and returns accum_err=0.
 */
static bool inherit_splash = false;
module_param(inherit_splash, bool, 0644);
MODULE_PARM_DESC(inherit_splash, "Adopt the bootloader-initialised panel instead of resetting it");

struct r63319_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi[2];
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;
};

/* pm8941_l22 @ 3.3V and pm8941_l12 @ 1.8V, per live sysfs on SM-T320 */
static const struct regulator_bulk_data r63319_supplies[] = {
	{ .supply = "vdd" },
	{ .supply = "vddio" },
};

static inline struct r63319_panel *to_r63319(struct drm_panel *panel)
{
	return container_of(panel, struct r63319_panel, panel);
}

/*
 * Colour-enhancement tuning payload, byte-for-byte from the downstream
 * qcom,mdss-dsi-on-command block. Undocumented vendor register.
 */
static const u8 r63319_ce_tuning[] = {
	0xca, 0x01, 0x80, 0xc8, 0xb9, 0xff, 0xff, 0xff,
	0xa0, 0x09, 0x20, 0x10, 0x8c, 0x0a, 0x4a, 0x37,
	0xa0, 0x00, 0xff, 0x0c, 0x0c, 0x0c, 0x0c, 0x3f,
	0x3f, 0xef, 0x00, 0x10, 0x10, 0x3f, 0x3f, 0x3f,
	0x3f,
};

/*
 * Full panel is 1600x2560; each DSI link drives one 800x2560 half, so every
 * horizontal figure below is the downstream per-link value doubled.
 * Derived pixel clock 320.622 MHz -> 961.9 MHz/lane at 24bpp over 4 lanes,
 * which matches the downstream qcom,mdss-dsi-panel-clockrate of 964 MHz.
 */
static const struct drm_display_mode r63319_mode_dual = {
	/*
	 * Downstream uses a 150-pixel front porch per link, giving htotal 2068
	 * and a 320.623 MHz pixel clock. mdp5_kms.c pins the MDP core clock at
	 * msm8x74v2_config.max_clk = 320 MHz flat (no per-mode calculation), so
	 * that mode is 0.2% over what the MDP can source and INTF1 underruns:
	 *   [drm:mdp5_irq_error_handler] *ERROR* errors: 04000000
	 *   (MDP5_IRQ_INTF1_UNDER_RUN)
	 * Trim the front porch to 140 per link -> htotal 2048, 317.522 MHz,
	 * which leaves ~0.8% headroom. Porch length is not panel-critical.
	 */
	.clock		= 317522,
	.hdisplay	= 1600,
	.hsync_start	= 1880,	/* 1600 + 2*140 front porch */
	.hsync_end	= 1920,	/* +      2*20  pulse width */
	.htotal		= 2048,	/* +      2*64  back porch  */
	.vdisplay	= 2560,
	.vsync_start	= 2572,	/* 2560 + 12 front porch */
	.vsync_end	= 2576,	/* +       4 pulse width */
	.vtotal		= 2584,	/* +       8 back porch  */
	.width_mm	= 114,	/* 8.4" diagonal, 16:10 */
	.height_mm	= 182,
};

/*
 * Single-link diagnostic mode: one DSI host driving the left 800x2560 half.
 *
 * MDP5 has no bonded-DSI support. mdp5_vid_encoder_mode_set() programs
 * INTF_HSYNC_CTL/DISPLAY_HCTL from the full mode -- there is no equivalent of
 * dpu_encoder_phys_vid.c's "mode.hdisplay >>= 1" -- and mdp5_crtc.c only
 * allocates a right mixer when hdisplay > lm.max_width (2048), which 1600 is
 * not. So with qcom,dual-dsi-mode the INTF is timed for 1600 px/line while the
 * DSI host is configured for 800, and the resulting overflow shows up as
 * dsi_err_worker: status=4 (DSI_ERR_STATE_FIFO) at ~49k/s.
 *
 * Halving the mode makes the INTF and the host agree. Per-link porches are the
 * same numbers the dual mode uses, just no longer doubled:
 *   htotal 1024 x vtotal 2584 x 60 Hz = 158.761 MHz  (exactly half of 317522)
 *   lane rate = 158.761 MHz * 24 bpp / 4 lanes = 953 Mbps, inside 28nm HPM
 *   MDP core clock is far below the 320 MHz msm8x74v2 cap
 */
/*
 * Exactly the downstream timing, which the dual mode above cannot afford:
 *
 *   h-front-porch 150  h-pulse 20  h-back-porch 64   -> htotal 1034
 *   v-front-porch  12  v-pulse  4  v-back-porch  8   -> vtotal 2584
 *   1034 * 2584 * 60 = 160.311 MHz
 *
 * The 140-pixel front porch this used to carry was copied from the dual mode,
 * where it exists only to keep the pixel clock under the MDP's flat 320 MHz
 * ceiling. Driving a single link needs half that, so there is no reason to
 * deviate from the panel's own numbers -- and "porch length is not
 * panel-critical", asserted there without evidence, is precisely what a panel
 * that answers DCS perfectly while showing nothing calls into question.
 */
static const struct drm_display_mode r63319_mode_single = {
	.clock		= 160311,
	.hdisplay	= 800,
	.hsync_start	= 950,	/* 800 + 150 front porch */
	.hsync_end	= 970,	/* +      20  pulse width */
	.htotal		= 1034,	/* +      64  back porch  */
	.vdisplay	= 2560,
	.vsync_start	= 2572,	/* 2560 + 12 front porch */
	.vsync_end	= 2576,	/* +       4 pulse width */
	.vtotal		= 2584,	/* +       8 back porch  */
	.width_mm	= 114,	/* 8.4" diagonal, 16:10 */
	.height_mm	= 182,
};

static void r63319_reset(struct r63319_panel *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);	/* deasserted, pin high */
	msleep(5);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);	/* asserted,   pin low  */
	msleep(12);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);	/* deasserted, pin high */
	msleep(12);
}

/* Read one DCS register. Always LP, so stages stay comparable. Returns <0 on error. */
static int r63319_rd(struct r63319_panel *ctx, u8 cmd, const char *label)
{
	struct device *dev = &ctx->dsi[0]->dev;
	unsigned long saved = ctx->dsi[0]->mode_flags;
	u8 val = 0;
	int ret;

	ctx->dsi[0]->mode_flags |= MIPI_DSI_MODE_LPM;
	ret = mipi_dsi_dcs_read(ctx->dsi[0], cmd, &val, 1);
	ctx->dsi[0]->mode_flags = saved;

	if (ret < 0) {
		dev_err(dev, "  rd %s (0x%02x): FAILED %d\n", label, cmd, ret);
		return ret;
	}
	dev_info(dev, "  rd %s (0x%02x) = 0x%02x\n", label, cmd, val);
	return val;
}

/*
 * Sequence transcribed from the downstream Samsung MDSS device tree,
 * qcom,mdss-dsi-on-command in dsi_panel_samsung_2560p_video_R63319.dtsi.
 * Factored out so it can be replayed in a different transfer mode.
 */
static void r63319_send_init(struct r63319_panel *ctx,
			     struct mipi_dsi_multi_context *dsi_ctx)
{
	struct mipi_dsi_device *d0 = ctx->dsi[0], *d1 = ctx->dsi[1];

	r63319_dual(mipi_dsi_dcs_soft_reset_multi, dsi_ctx, d0, d1);
	mipi_dsi_msleep(dsi_ctx, 10);

	r63319_dcs_seq(dsi_ctx, d0, d1, MIPI_DCS_SET_ADDRESS_MODE, 0x00);
	r63319_dcs_seq(dsi_ctx, d0, d1, MIPI_DCS_SET_PIXEL_FORMAT, 0x70);
	r63319_dcs_seq(dsi_ctx, d0, d1, MIPI_DCS_SET_TEAR_ON, 0x01);
	/* 0x53 = write_control_display: backlight ctrl on, dimming off */
	r63319_dcs_seq(dsi_ctx, d0, d1, 0x53, 0x2c);

	/* unlock manufacturer command access, then vendor colour-enhance block */
	r63319_gen_seq(dsi_ctx, d0, d1, 0xb0, 0x00);
	r63319_gen_seq(dsi_ctx, d0, d1, 0xd6, 0x01);
	r63319_dual(mipi_dsi_generic_write_multi, dsi_ctx, d0, d1,
		    r63319_ce_tuning, ARRAY_SIZE(r63319_ce_tuning));

	r63319_dual(mipi_dsi_dcs_exit_sleep_mode_multi, dsi_ctx, d0, d1);
	mipi_dsi_msleep(dsi_ctx, 120);

	r63319_dual(mipi_dsi_dcs_set_display_on_multi, dsi_ctx, d0, d1);
	mipi_dsi_msleep(dsi_ctx, 20);
	r63319_dcs_seq(dsi_ctx, d0, d1, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, 0x15);
}

/*
 * Cold init: the panel is ours from reset, so just replay the vendor sequence.
 *
 * This was a staged A-F walk that probed the panel one command at a time. It
 * existed because every DCS read came back 0x00 and the init appeared to have
 * no effect -- which turned out to be our own doing: reset is GPIO_ACTIVE_LOW
 * and was requested GPIOD_OUT_HIGH, so the driver held the panel in reset from
 * probe onward. With GPIOD_ASIS the reads are truthful and the premise of the
 * walk is gone. It is dropped because a dozen reads and writes against a panel
 * that is not answering spend long enough in DCS timeouts during probe to be a
 * plausible cause of the boot loop seen the one time cold init was tried.
 */
static int r63319_on(struct r63319_panel *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { 0 };
	struct device *dev = &ctx->dsi[0]->dev;
	int pm;

	r63319_send_init(ctx, &dsi_ctx);
	if (dsi_ctx.accum_err) {
		dev_err(dev, "init sequence failed: %d\n", dsi_ctx.accum_err);
		return dsi_ctx.accum_err;
	}

	/*
	 * Full DCS status readback. The control path demonstrably works while
	 * the glass stays dark, so the interesting registers are the ones that
	 * describe the panel's own view of the video it is being sent:
	 * get_signal_mode (0x0e) and get_diagnostic_result (0x0f, RDDSDR),
	 * whose bit 7 is register-loading detection and bit 6 functionality
	 * detection -- i.e. whether the panel thinks its display is working.
	 */
	pm = r63319_rd(ctx, MIPI_DCS_GET_POWER_MODE, "power_mode@init");
	dev_info(dev, "[cold-init] pm=0x%02x sleep_out=%d display_on=%d\n",
		 pm, pm > 0 && (pm & BIT(4)), pm > 0 && (pm & BIT(2)));

	r63319_rd(ctx, MIPI_DCS_GET_ADDRESS_MODE, "address_mode");
	r63319_rd(ctx, MIPI_DCS_GET_PIXEL_FORMAT, "pixel_format");
	r63319_rd(ctx, MIPI_DCS_GET_DISPLAY_MODE, "display_mode");
	r63319_rd(ctx, MIPI_DCS_GET_SIGNAL_MODE, "signal_mode");
	r63319_rd(ctx, MIPI_DCS_GET_DIAGNOSTIC_RESULT, "diagnostic");

	return 0;
}

/*
 * Re-run the init sequence on a live panel, without a reboot or a reflash.
 * Iterating on panel bring-up otherwise costs a full build/flash/boot cycle
 * per hypothesis, which is the single biggest drag on this port.
 *
 *   echo 1 > /sys/module/panel_samsung_r63319/parameters/reinit
 */
static struct r63319_panel *r63319_dbg_ctx;

static int r63319_reinit_set(const char *val, const struct kernel_param *kp)
{
	struct r63319_panel *ctx = r63319_dbg_ctx;

	if (!ctx)
		return -ENODEV;

	dev_info(&ctx->dsi[0]->dev, "reinit: replaying init sequence\n");
	r63319_reset(ctx);
	return r63319_on(ctx);
}

static const struct kernel_param_ops r63319_reinit_ops = {
	.set = r63319_reinit_set,
};
module_param_cb(reinit, &r63319_reinit_ops, NULL, 0200);

/*
 * Userspace DCS console.
 *
 * Panel bring-up is mostly "what if we sent this sequence instead?", and
 * mainline exposes no way to send a DSI command from userspace, so every
 * such question has been costing a kernel build, a flash and a reboot. This
 * turns that loop into a shell command:
 *
 *   echo "w 11"          > /sys/kernel/debug/r63319/cmd   # exit_sleep_mode
 *   echo "w b0 00"       > /sys/kernel/debug/r63319/cmd   # generic write
 *   echo "r 0f"          > /sys/kernel/debug/r63319/cmd   # read, result in dmesg
 *
 * 'w' picks DCS vs generic the way the DSI spec does, by payload length, so
 * the bytes written here match the vendor sequence byte for byte.
 */
static struct dentry *r63319_debugfs;

static ssize_t r63319_cmd_write(struct file *file, const char __user *ubuf,
				size_t len, loff_t *ppos)
{
	struct r63319_panel *ctx = r63319_dbg_ctx;
	struct mipi_dsi_multi_context dsi_ctx = { 0 };
	u8 buf[64];
	char kbuf[256], *p, *tok;
	int n = 0, ret;

	if (!ctx)
		return -ENODEV;
	if (len >= sizeof(kbuf))
		return -EINVAL;
	if (copy_from_user(kbuf, ubuf, len))
		return -EFAULT;
	kbuf[len] = '\0';

	p = strim(kbuf);
	tok = strsep(&p, " \t");
	if (!tok)
		return -EINVAL;

	if (*tok == 'r') {
		unsigned int reg;

		if (!p || kstrtouint(strim(p), 16, &reg))
			return -EINVAL;
		r63319_rd(ctx, (u8)reg, "debugfs");
		return len;
	}

	if (*tok != 'w')
		return -EINVAL;

	while ((tok = strsep(&p, " \t")) && n < (int)sizeof(buf)) {
		unsigned int byte;

		if (!*tok)
			continue;
		if (kstrtouint(tok, 16, &byte) || byte > 0xff)
			return -EINVAL;
		buf[n++] = byte;
	}
	if (!n)
		return -EINVAL;

	dsi_ctx.dsi = ctx->dsi[0];
	mipi_dsi_dcs_write_buffer_multi(&dsi_ctx, buf, n);
	ret = dsi_ctx.accum_err;

	dev_info(&ctx->dsi[0]->dev, "debugfs: wrote %d byte%s, cmd 0x%02x, err %d\n",
		 n, n == 1 ? "" : "s", buf[0], ret);

	if (ret)
		return ret;

	return len;
}

static const struct file_operations r63319_cmd_fops = {
	.owner	= THIS_MODULE,
	.write	= r63319_cmd_write,
	.open	= simple_open,
	.llseek	= noop_llseek,
};

static int r63319_off(struct r63319_panel *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { 0 };

	/* display_off is .disable's job now; this only has to sleep the panel */
	r63319_dual(mipi_dsi_dcs_enter_sleep_mode_multi, &dsi_ctx,
		    ctx->dsi[0], ctx->dsi[1]);
	mipi_dsi_msleep(&dsi_ctx, 50);

	return dsi_ctx.accum_err;
}

/*
 * Downstream qcom,mdss-dsi-reset-sequence = <1 5>, <0 12>, <1 12> gives RAW
 * pin levels high/low/high, i.e. the panel is held in reset while the line is
 * LOW -> reset is active low, declared GPIO_ACTIVE_LOW in the DT. gpiod values
 * below are therefore logical (1 = asserted = pin driven low), which inverts
 * the raw downstream numbers and, importantly, leaves reset DEASSERTED.
 */
static int r63319_prepare(struct drm_panel *panel)
{
	struct r63319_panel *ctx = to_r63319(panel);
	struct device *dev = &ctx->dsi[0]->dev;
	int ret, pm;

	ret = regulator_bulk_enable(ARRAY_SIZE(r63319_supplies), ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to enable regulators\n");

	/*
	 * The vendor DTS sets qcom,cont-splash-enabled, so aboot initialises
	 * and lights this panel before Linux runs -- that is the brief flash
	 * seen at every boot. Look at the panel BEFORE touching the enable or
	 * reset lines: if the bootloader already has it awake, inherit that
	 * state rather than resetting a working panel back into a state we
	 * have never managed to drive out of.
	 */
	pm = r63319_rd(ctx, MIPI_DCS_GET_POWER_MODE, "power_mode@entry");
	dev_info(dev, "[bootloader-state] pm=0x%02x sleep_out=%d display_on=%d booster=%d\n",
		 pm, pm > 0 && (pm & BIT(4)), pm > 0 && (pm & BIT(2)),
		 pm > 0 && (pm & BIT(6)));

	if (inherit_splash) {
		dev_info(dev, "cont-splash: adopting bootloader panel, no enable/reset/init\n");
		return 0;
	}

	gpiod_set_value_cansleep(ctx->enable_gpio, 1);
	usleep_range(20000, 21000);	/* qcom,mdss-dsi-init-delay-us = 20000 */

	r63319_reset(ctx);

	ret = r63319_on(ctx);
	dev_info(dev, "r63319_on() returned %d\n", ret);
	if (ret < 0) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		gpiod_set_value_cansleep(ctx->enable_gpio, 0);
		regulator_bulk_disable(ARRAY_SIZE(r63319_supplies), ctx->supplies);
		return ret;
	}

	return 0;
}

static int r63319_unprepare(struct drm_panel *panel)
{
	struct r63319_panel *ctx = to_r63319(panel);

	/* Mirror prepare: if we adopted the panel, do not tear it down either. */
	if (inherit_splash)
		return 0;

	r63319_off(ctx);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	gpiod_set_value_cansleep(ctx->enable_gpio, 0);
	regulator_bulk_disable(ARRAY_SIZE(r63319_supplies), ctx->supplies);

	return 0;
}

static int r63319_get_modes(struct drm_panel *panel, struct drm_connector *connector)
{
	struct r63319_panel *ctx = to_r63319(panel);

	return drm_connector_helper_get_modes_fixed(connector,
			ctx->dsi[1] ? &r63319_mode_dual : &r63319_mode_single);
}

/*
 * .enable runs after the DSI host is streaming video; .prepare runs before it.
 *
 * This driver used to do everything in .prepare, including set_display_on, so
 * the panel was told to light up while the link was still dark. It accepts
 * that -- every command returns accum_err 0 and power_mode reads back 0x1c,
 * sleep_out=1 display_on=1, exactly as though it had worked -- and then
 * quietly drops back to sleep on its own: by the time userspace is up,
 * power_mode reads 0x0c with the sleep_out bit clear and the screen is black
 * with only the backlight lit.
 *
 * It is not .unprepare doing it. DCS reads still succeed at that point, which
 * they could not if .unprepare had run, since it drops the regulators and
 * asserts reset. The panel puts itself to sleep because nothing ever tells it
 * to wake once there is actually video to display.
 *
 * So the sequence is split the way the DSI panel API intends: .prepare powers
 * the panel and loads the vendor register set, and .enable asserts sleep_out
 * and display_on once the stream is live.
 */
static int r63319_enable(struct drm_panel *panel)
{
	struct r63319_panel *ctx = to_r63319(panel);
	struct mipi_dsi_multi_context dsi_ctx = { 0 };
	struct device *dev = &ctx->dsi[0]->dev;
	int pm;

	r63319_dual(mipi_dsi_dcs_exit_sleep_mode_multi, &dsi_ctx,
		    ctx->dsi[0], ctx->dsi[1]);
	mipi_dsi_msleep(&dsi_ctx, 120);
	r63319_dual(mipi_dsi_dcs_set_display_on_multi, &dsi_ctx,
		    ctx->dsi[0], ctx->dsi[1]);
	mipi_dsi_msleep(&dsi_ctx, 20);

	pm = r63319_rd(ctx, MIPI_DCS_GET_POWER_MODE, "power_mode@enable");
	dev_info(dev, "[enable] pm=0x%02x sleep_out=%d display_on=%d err=%d\n",
		 pm, pm > 0 && (pm & BIT(4)), pm > 0 && (pm & BIT(2)),
		 dsi_ctx.accum_err);

	return dsi_ctx.accum_err;
}

static int r63319_disable(struct drm_panel *panel)
{
	struct r63319_panel *ctx = to_r63319(panel);
	struct mipi_dsi_multi_context dsi_ctx = { 0 };

	r63319_dual(mipi_dsi_dcs_set_display_off_multi, &dsi_ctx,
		    ctx->dsi[0], ctx->dsi[1]);
	mipi_dsi_msleep(&dsi_ctx, 20);

	return dsi_ctx.accum_err;
}

static const struct drm_panel_funcs r63319_panel_funcs = {
	.prepare	= r63319_prepare,
	.enable		= r63319_enable,
	.disable	= r63319_disable,
	.unprepare	= r63319_unprepare,
	.get_modes	= r63319_get_modes,
};

static int r63319_probe(struct mipi_dsi_device *dsi)
{
	struct mipi_dsi_device_info info = { "r63319-panel", 0, NULL };
	struct device *dev = &dsi->dev;
	struct mipi_dsi_host *dsi1_host;
	struct device_node *dsi1_node;
	struct r63319_panel *ctx;
	int ret, i;

	ctx = devm_drm_panel_alloc(dev, struct r63319_panel, panel,
				   &r63319_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ret = devm_regulator_bulk_get_const(dev, ARRAY_SIZE(r63319_supplies),
					    r63319_supplies, &ctx->supplies);
	if (ret < 0)
		return ret;

	/*
	 * GPIOD_ASIS, not GPIOD_OUT_*: requesting these as outputs drives reset
	 * ASSERTED (the line is GPIO_ACTIVE_LOW) and enable deasserted at probe,
	 * which tears down the bootloader's already-running panel before prepare
	 * ever gets a look at it. That is what made every pre-reset read come
	 * back 0x00 -- the panel was being held in reset, not answering zero.
	 */
	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_ASIS);
	if (IS_ERR(ctx->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->enable_gpio),
				     "Failed to get enable-gpios\n");

	ctx->dsi[0] = dsi;

	/*
	 * The second DSI link is optional. With both ports wired up the panel
	 * runs bonded at 1600x2560; with only port@0 it runs as a single link
	 * at 800x2560. The latter is the configuration MDP5 can actually drive
	 * today -- see the comment on r63319_mode_single.
	 */
	dsi1_node = of_graph_get_remote_node(dev->of_node, 1, -1);
	if (dsi1_node) {
		dsi1_host = of_find_mipi_dsi_host_by_node(dsi1_node);
		of_node_put(dsi1_node);
		if (!dsi1_host)
			return dev_err_probe(dev, -EPROBE_DEFER,
					     "Failed to find second DSI host\n");

		ctx->dsi[1] = devm_mipi_dsi_device_register_full(dev, dsi1_host,
								&info);
		if (IS_ERR(ctx->dsi[1]))
			return dev_err_probe(dev, PTR_ERR(ctx->dsi[1]),
					     "Failed to register second DSI device\n");
	} else {
		ctx->dsi[1] = NULL;
		dev_info(dev, "no second DSI link, running single-link 800x2560\n");
	}
	mipi_dsi_set_drvdata(dsi, ctx);

	/*
	 * The DSI host must be up and holding the lanes at LP-11 before the
	 * panel is prepared, otherwise dsi_host_transfer() rejects every DCS
	 * command with -EINVAL (it checks msm_host->power_on). This mirrors
	 * qcom,mdss-dsi-lp11-init in the downstream panel DTS.
	 */
	ctx->panel.prepare_prev_first = true;

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	/*
	 * drm_panel_add() rather than devm_drm_panel_add(): the devm variant
	 * does not exist in 6.16, which is what linux-postmarketos-qcom-msm8974
	 * currently builds. Paired with drm_panel_remove() in .remove below.
	 */
	drm_panel_add(&ctx->panel);

	r63319_dbg_ctx = ctx;	/* for the reinit module parameter */
	r63319_debugfs = debugfs_create_dir("r63319", NULL);
	debugfs_create_file("cmd", 0200, r63319_debugfs, ctx, &r63319_cmd_fops);

	for (i = 0; i < ARRAY_SIZE(ctx->dsi); i++) {
		if (!ctx->dsi[i])
			continue;

		ctx->dsi[i]->lanes = 4;
		ctx->dsi[i]->format = MIPI_DSI_FMT_RGB888;
		/*
		 * MIPI_DSI_CLOCK_NON_CONTINUOUS was copied from
		 * panel-renesas-r63419.c (a different panel) without evidence.
		 * The downstream config for THIS panel specifies only
		 * qcom,mdss-dsi-traffic-mode = "burst_mode" and never asks for a
		 * non-continuous clock. Letting the clock lane drop to LP between
		 * bursts makes the panel resync every line and floods
		 * REG_DSI_FIFO_STATUS (dsi_err_worker: status=4, ~49k/s).
		 */
		ctx->dsi[i]->mode_flags = MIPI_DSI_MODE_VIDEO |
					  MIPI_DSI_MODE_VIDEO_BURST |
					  MIPI_DSI_MODE_LPM;

		ret = devm_mipi_dsi_attach(dev, ctx->dsi[i]);
		if (ret < 0)
			return dev_err_probe(dev, ret,
					     "Failed to attach DSI %d\n", i);
	}

	return 0;
}

static void r63319_remove(struct mipi_dsi_device *dsi)
{
	debugfs_remove_recursive(r63319_debugfs);
	r63319_debugfs = NULL;
	r63319_dbg_ctx = NULL;

	struct r63319_panel *ctx = mipi_dsi_get_drvdata(dsi);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id r63319_of_match[] = {
	{ .compatible = "samsung,r63319-tabpro84" },
	{ }
};
MODULE_DEVICE_TABLE(of, r63319_of_match);

static struct mipi_dsi_driver r63319_driver = {
	.probe = r63319_probe,
	.remove = r63319_remove,
	.driver = {
		.name = "panel-samsung-r63319",
		.of_match_table = r63319_of_match,
	},
};
module_mipi_dsi_driver(r63319_driver);

MODULE_DESCRIPTION("DRM driver for Samsung 1600x2560 dual-DSI R63319 panel (SM-T320)");
MODULE_LICENSE("GPL");
