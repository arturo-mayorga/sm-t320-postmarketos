// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/moduleparam.h>

#include <drm/drm_crtc.h>
#include <drm/drm_probe_helper.h>

#include "mdp5_kms.h"

/*
 * Split display for bonded DSI on MDP5 v1.x (msm8974).
 *
 * The Tab Pro 8.4 panel is one 1600x2560 surface fed by two DSI links of 800
 * columns each. msm_dsi already handles its half: given the full-width mode it
 * halves the horizontal timing per host (dsi_host.c, is_bonded_dsi) and
 * creates no encoder or connector for the slave link. Nothing in MDP5 picked
 * up the slack, so INTF2 never ran and only the left half was ever driven.
 *
 * Mainline's bonded-DSI support in mdp5_ctl.c is the "single flush" scheme,
 * which the driver gates on hardware rev 3+; on v1.x it is dead code. This
 * hardware instead does what the downstream MDSS driver calls a split
 * display (mdss_mdp_ctl.c, mdss_mdp_ctl_split_display_setup): two layer
 * mixers, each with its own CTL and INTF, tied together by the split-display
 * block so that INTF2's timing generator follows INTF1's:
 *
 *	SPLIT_DPL_LOWER = INTF2_TG_SYNC
 *	SPLIT_DPL_UPPER = 0		(sw-trigger mux is command-mode only)
 *	SPLIT_DPL_EN    = 1
 *
 * Only INTF1's timing engine is ever started; INTF2 is slaved to it.
 *
 * The master encoder borrows the slave encoder's INTF and CTL and records
 * them in the CRTC pipeline (r_intf / r_ctl); the CRTC, planes and CTL code
 * then route the right mixer through them. All of it sits behind
 *
 *	msm.mdp5_split_dsi=1
 *
 * so one kernel boots both the known-good single-link configuration and this.
 */
static bool mdp5_split_dsi;
module_param(mdp5_split_dsi, bool, 0600);
MODULE_PARM_DESC(mdp5_split_dsi,
		 "Drive INTF2 alongside INTF1 for a bonded (dual-link) DSI panel");

/* fixed by the SoC: DSI0 -> INTF1 (master), DSI1 -> INTF2 (slave) */
#define MDP5_SPLIT_MASTER_INTF	1
#define MDP5_SPLIT_SLAVE_INTF	2

static struct mdp5_kms *get_kms(struct drm_encoder *encoder)
{
	struct msm_drm_private *priv = encoder->dev->dev_private;
	return to_mdp5_kms(to_mdp_kms(priv->kms));
}

static bool mdp5_encoder_is_split_master(struct mdp5_encoder *mdp5_encoder)
{
	return mdp5_split_dsi &&
	       mdp5_encoder->intf->type == INTF_DSI &&
	       mdp5_encoder->intf->num == MDP5_SPLIT_MASTER_INTF;
}

static struct mdp5_encoder *mdp5_encoder_find_intf(struct drm_device *dev,
						   int intf_num)
{
	struct drm_encoder *encoder;

	drm_for_each_encoder(encoder, dev) {
		struct mdp5_encoder *e = to_mdp5_encoder(encoder);

		if (e->intf && e->intf->num == intf_num)
			return e;
	}
	return NULL;
}

static void mdp5_vid_intf_program(struct mdp5_kms *mdp5_kms, int intf,
				  struct drm_display_mode *mode,
				  uint32_t hsync_start_x, uint32_t hsync_end_x,
				  uint32_t vsync_period, uint32_t vsync_len,
				  uint32_t display_v_start, uint32_t display_v_end,
				  uint32_t dtv_hsync_skew, uint32_t ctrl_pol,
				  uint32_t format)
{
	mdp5_write(mdp5_kms, REG_MDP5_INTF_HSYNC_CTL(intf),
			MDP5_INTF_HSYNC_CTL_PULSEW(mode->hsync_end - mode->hsync_start) |
			MDP5_INTF_HSYNC_CTL_PERIOD(mode->htotal));
	mdp5_write(mdp5_kms, REG_MDP5_INTF_VSYNC_PERIOD_F0(intf), vsync_period);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_VSYNC_LEN_F0(intf), vsync_len);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_DISPLAY_HCTL(intf),
			MDP5_INTF_DISPLAY_HCTL_START(hsync_start_x) |
			MDP5_INTF_DISPLAY_HCTL_END(hsync_end_x));
	mdp5_write(mdp5_kms, REG_MDP5_INTF_DISPLAY_VSTART_F0(intf), display_v_start);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_DISPLAY_VEND_F0(intf), display_v_end);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_BORDER_COLOR(intf), 0);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_UNDERFLOW_COLOR(intf), 0xff);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_HSYNC_SKEW(intf), dtv_hsync_skew);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_POLARITY_CTL(intf), ctrl_pol);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_ACTIVE_HCTL(intf),
			MDP5_INTF_ACTIVE_HCTL_START(0) |
			MDP5_INTF_ACTIVE_HCTL_END(0));
	mdp5_write(mdp5_kms, REG_MDP5_INTF_ACTIVE_VSTART_F0(intf), 0);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_ACTIVE_VEND_F0(intf), 0);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_PANEL_FORMAT(intf), format);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_FRAME_LINE_COUNT_EN(intf), 0x3);  /* frame+line? */
}

static void mdp5_vid_encoder_mode_set(struct drm_encoder *encoder,
				      struct drm_display_mode *mode,
				      struct drm_display_mode *adjusted_mode)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	struct drm_device *dev = encoder->dev;
	struct drm_connector *connector;
	struct mdp5_pipeline *pipeline = mdp5_crtc_get_pipeline(encoder->crtc);
	int intf = mdp5_encoder->intf->num;
	uint32_t dtv_hsync_skew, vsync_period, vsync_len, ctrl_pol;
	uint32_t display_v_start, display_v_end;
	uint32_t hsync_start_x, hsync_end_x;
	struct drm_display_mode split_mode;
	uint32_t format = 0x2100;
	unsigned long flags;

	mode = adjusted_mode;

	/*
	 * Split display: the DRM mode carries the whole panel, but each INTF
	 * only clocks out its own half, so halve the horizontal numbers here
	 * (dpu_encoder_phys_vid.c does the same with "mode.hdisplay >>= 1").
	 * The vertical timing is shared and untouched.
	 */
	if (mdp5_pipeline_is_split(pipeline)) {
		mode = &split_mode;
		drm_mode_copy(mode, adjusted_mode);
		mode->hdisplay    /= 2;
		mode->hsync_start /= 2;
		mode->hsync_end   /= 2;
		mode->htotal      /= 2;
		mode->clock       /= 2;
		DBG("split display: per-INTF mode " DRM_MODE_FMT, DRM_MODE_ARG(mode));
	}

	DBG("set mode: " DRM_MODE_FMT, DRM_MODE_ARG(mode));

	ctrl_pol = 0;

	/* DSI controller cannot handle active-low sync signals. */
	if (mdp5_encoder->intf->type != INTF_DSI) {
		if (mode->flags & DRM_MODE_FLAG_NHSYNC)
			ctrl_pol |= MDP5_INTF_POLARITY_CTL_HSYNC_LOW;
		if (mode->flags & DRM_MODE_FLAG_NVSYNC)
			ctrl_pol |= MDP5_INTF_POLARITY_CTL_VSYNC_LOW;
	}
	/* probably need to get DATA_EN polarity from panel.. */

	dtv_hsync_skew = 0;  /* get this from panel? */

	/* Get color format from panel, default is 8bpc */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (connector->encoder == encoder) {
			switch (connector->display_info.bpc) {
			case 4:
				format |= 0;
				break;
			case 5:
				format |= 0x15;
				break;
			case 6:
				format |= 0x2A;
				break;
			case 8:
			default:
				format |= 0x3F;
				break;
			}
			break;
		}
	}

	hsync_start_x = (mode->htotal - mode->hsync_start);
	hsync_end_x = mode->htotal - (mode->hsync_start - mode->hdisplay) - 1;

	vsync_period = mode->vtotal * mode->htotal;
	vsync_len = (mode->vsync_end - mode->vsync_start) * mode->htotal;
	display_v_start = (mode->vtotal - mode->vsync_start) * mode->htotal + dtv_hsync_skew;
	display_v_end = vsync_period - ((mode->vsync_start - mode->vdisplay) * mode->htotal) + dtv_hsync_skew - 1;

	/*
	 * For edp only:
	 * DISPLAY_V_START = (VBP * HCYCLE) + HBP
	 * DISPLAY_V_END = (VBP + VACTIVE) * HCYCLE - 1 - HFP
	 */
	if (mdp5_encoder->intf->type == INTF_eDP) {
		display_v_start += mode->htotal - mode->hsync_start;
		display_v_end -= mode->hsync_start - mode->hdisplay;
	}

	spin_lock_irqsave(&mdp5_encoder->intf_lock, flags);

	mdp5_vid_intf_program(mdp5_kms, intf, mode, hsync_start_x, hsync_end_x,
			      vsync_period, vsync_len, display_v_start,
			      display_v_end, dtv_hsync_skew, ctrl_pol, format);
	/* the slave INTF scans the other half of the same panel: same programme */
	if (mdp5_pipeline_is_split(pipeline))
		mdp5_vid_intf_program(mdp5_kms, pipeline->r_intf->num, mode,
				      hsync_start_x, hsync_end_x,
				      vsync_period, vsync_len, display_v_start,
				      display_v_end, dtv_hsync_skew, ctrl_pol,
				      format);

	spin_unlock_irqrestore(&mdp5_encoder->intf_lock, flags);

	mdp5_crtc_set_pipeline(encoder->crtc);
}

static void mdp5_vid_encoder_disable(struct drm_encoder *encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	struct mdp5_ctl *ctl = mdp5_encoder->ctl;
	struct mdp5_pipeline *pipeline = mdp5_crtc_get_pipeline(encoder->crtc);
	struct mdp5_hw_mixer *mixer = mdp5_crtc_get_mixer(encoder->crtc);
	struct mdp5_interface *intf = mdp5_encoder->intf;
	int intfn = mdp5_encoder->intf->num;
	unsigned long flags;

	if (WARN_ON(!mdp5_encoder->enabled))
		return;

	mdp5_ctl_set_encoder_state(ctl, pipeline, false);
	if (mdp5_pipeline_is_split(pipeline)) {
		struct mdp5_pipeline rp;

		mdp5_pipeline_right(pipeline, &rp);
		mdp5_ctl_set_encoder_state(pipeline->r_ctl, &rp, false);
	}

	spin_lock_irqsave(&mdp5_encoder->intf_lock, flags);
	mdp5_write(mdp5_kms, REG_MDP5_INTF_TIMING_ENGINE_EN(intfn), 0);
	if (mdp5_pipeline_is_split(pipeline)) {
		mdp5_write(mdp5_kms, REG_MDP5_SPLIT_DPL_EN, 0);
		mdp5_write(mdp5_kms, REG_MDP5_SPLIT_DPL_LOWER, 0);
		mdp5_write(mdp5_kms, REG_MDP5_SPLIT_DPL_UPPER, 0);
	}
	spin_unlock_irqrestore(&mdp5_encoder->intf_lock, flags);
	mdp5_ctl_commit(ctl, pipeline, mdp_ctl_flush_mask_encoder(intf), true);
	if (mdp5_pipeline_is_split(pipeline)) {
		struct mdp5_pipeline rp;

		mdp5_pipeline_right(pipeline, &rp);
		mdp5_ctl_commit(pipeline->r_ctl, &rp,
				mdp_ctl_flush_mask_encoder(rp.intf), true);
	}

	/*
	 * Wait for a vsync so we know the ENABLE=0 latched before
	 * the (connector) source of the vsync's gets disabled,
	 * otherwise we end up in a funny state if we re-enable
	 * before the disable latches, which results that some of
	 * the settings changes for the new modeset (like new
	 * scanout buffer) don't latch properly..
	 */
	mdp_irq_wait(&mdp5_kms->base, intf2vblank(mixer, intf));

	mdp5_encoder->enabled = false;
}

static void mdp5_vid_encoder_enable(struct drm_encoder *encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	struct mdp5_ctl *ctl = mdp5_encoder->ctl;
	struct mdp5_interface *intf = mdp5_encoder->intf;
	struct mdp5_pipeline *pipeline = mdp5_crtc_get_pipeline(encoder->crtc);
	int intfn = intf->num;
	unsigned long flags;

	if (WARN_ON(mdp5_encoder->enabled))
		return;

	spin_lock_irqsave(&mdp5_encoder->intf_lock, flags);
	/*
	 * Split display: arm the split block before the master timing engine
	 * starts, so INTF2 locks to INTF1 from the very first frame. INTF2's
	 * own TIMING_ENGINE_EN is deliberately left alone, as downstream does.
	 */
	if (mdp5_pipeline_is_split(pipeline)) {
		mdp5_write(mdp5_kms, REG_MDP5_SPLIT_DPL_UPPER, 0);
		mdp5_write(mdp5_kms, REG_MDP5_SPLIT_DPL_LOWER,
			   MDP5_SPLIT_DPL_LOWER_INTF2_TG_SYNC);
		mdp5_write(mdp5_kms, REG_MDP5_SPLIT_DPL_EN, 1);
	}
	mdp5_write(mdp5_kms, REG_MDP5_INTF_TIMING_ENGINE_EN(intfn), 1);
	spin_unlock_irqrestore(&mdp5_encoder->intf_lock, flags);
	mdp5_ctl_commit(ctl, pipeline, mdp_ctl_flush_mask_encoder(intf), true);
	if (mdp5_pipeline_is_split(pipeline)) {
		struct mdp5_pipeline rp;

		mdp5_pipeline_right(pipeline, &rp);
		mdp5_ctl_commit(pipeline->r_ctl, &rp,
				mdp_ctl_flush_mask_encoder(rp.intf), true);
		mdp5_ctl_set_encoder_state(pipeline->r_ctl, &rp, true);
	}

	mdp5_ctl_set_encoder_state(ctl, pipeline, true);

	mdp5_encoder->enabled = true;
}

static void mdp5_encoder_mode_set(struct drm_encoder *encoder,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_interface *intf = mdp5_encoder->intf;

	if (intf->mode == MDP5_INTF_DSI_MODE_COMMAND)
		mdp5_cmd_encoder_mode_set(encoder, mode, adjusted_mode);
	else
		mdp5_vid_encoder_mode_set(encoder, mode, adjusted_mode);
}

static void mdp5_encoder_disable(struct drm_encoder *encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_interface *intf = mdp5_encoder->intf;

	if (intf->mode == MDP5_INTF_DSI_MODE_COMMAND)
		mdp5_cmd_encoder_disable(encoder);
	else
		mdp5_vid_encoder_disable(encoder);
}

static void mdp5_encoder_enable(struct drm_encoder *encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_interface *intf = mdp5_encoder->intf;
	/* this isn't right I think */
	struct drm_crtc_state *cstate = encoder->crtc->state;

	mdp5_encoder_mode_set(encoder, &cstate->mode, &cstate->adjusted_mode);

	if (intf->mode == MDP5_INTF_DSI_MODE_COMMAND)
		mdp5_cmd_encoder_enable(encoder);
	else
		mdp5_vid_encoder_enable(encoder);
}

static int mdp5_encoder_atomic_check(struct drm_encoder *encoder,
				     struct drm_crtc_state *crtc_state,
				     struct drm_connector_state *conn_state)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_crtc_state *mdp5_cstate = to_mdp5_crtc_state(crtc_state);
	struct mdp5_interface *intf = mdp5_encoder->intf;
	struct mdp5_ctl *ctl = mdp5_encoder->ctl;

	mdp5_cstate->ctl = ctl;
	mdp5_cstate->pipeline.intf = intf;
	mdp5_cstate->pipeline.r_intf = NULL;
	mdp5_cstate->pipeline.r_ctl = NULL;

	if (mdp5_encoder_is_split_master(mdp5_encoder)) {
		struct mdp5_encoder *slave =
			mdp5_encoder_find_intf(encoder->dev, MDP5_SPLIT_SLAVE_INTF);

		if (!slave || !slave->ctl) {
			DRM_DEV_ERROR(encoder->dev->dev,
				      "split display: no INTF%d encoder/CTL\n",
				      MDP5_SPLIT_SLAVE_INTF);
			return -EINVAL;
		}
		mdp5_cstate->pipeline.r_intf = slave->intf;
		mdp5_cstate->pipeline.r_ctl = slave->ctl;
		DBG("split display: INTF%d master, INTF%d slave", intf->num,
		    slave->intf->num);
	}

	/*
	 * This is a bit awkward, but we want to flush the CTL and hit the
	 * START bit at most once for an atomic update.  In the non-full-
	 * modeset case, this is done from crtc->atomic_flush(), but that
	 * is too early in the case of full modeset, in which case we
	 * defer to encoder->enable().  But we need to *know* whether
	 * encoder->enable() will be called to do this:
	 */
	if (drm_atomic_crtc_needs_modeset(crtc_state))
		mdp5_cstate->defer_start = true;

	return 0;
}

static const struct drm_encoder_helper_funcs mdp5_encoder_helper_funcs = {
	.disable = mdp5_encoder_disable,
	.enable = mdp5_encoder_enable,
	.atomic_check = mdp5_encoder_atomic_check,
};

int mdp5_encoder_get_linecount(struct drm_encoder *encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	int intf = mdp5_encoder->intf->num;

	return mdp5_read(mdp5_kms, REG_MDP5_INTF_LINE_COUNT(intf));
}

u32 mdp5_encoder_get_framecount(struct drm_encoder *encoder)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_kms *mdp5_kms = get_kms(encoder);
	int intf = mdp5_encoder->intf->num;

	return mdp5_read(mdp5_kms, REG_MDP5_INTF_FRAME_COUNT(intf));
}

void mdp5_encoder_set_intf_mode(struct drm_encoder *encoder, bool cmd_mode)
{
	struct mdp5_encoder *mdp5_encoder = to_mdp5_encoder(encoder);
	struct mdp5_interface *intf = mdp5_encoder->intf;

	/* TODO: Expand this to set writeback modes too */
	if (cmd_mode) {
		WARN_ON(intf->type != INTF_DSI);
		intf->mode = MDP5_INTF_DSI_MODE_COMMAND;
	} else {
		if (intf->type == INTF_DSI)
			intf->mode = MDP5_INTF_DSI_MODE_VIDEO;
		else
			intf->mode = MDP5_INTF_MODE_NONE;
	}
}

/* initialize encoder */
struct drm_encoder *mdp5_encoder_init(struct drm_device *dev,
				      struct mdp5_interface *intf,
				      struct mdp5_ctl *ctl)
{
	struct drm_encoder *encoder = NULL;
	struct mdp5_encoder *mdp5_encoder;
	int enc_type = (intf->type == INTF_DSI) ?
		DRM_MODE_ENCODER_DSI : DRM_MODE_ENCODER_TMDS;

	mdp5_encoder = drmm_encoder_alloc(dev, struct mdp5_encoder, base,
					  NULL, enc_type, NULL);
	if (IS_ERR(mdp5_encoder))
		return ERR_CAST(mdp5_encoder);

	encoder = &mdp5_encoder->base;
	mdp5_encoder->ctl = ctl;
	mdp5_encoder->intf = intf;

	spin_lock_init(&mdp5_encoder->intf_lock);

	drm_encoder_helper_add(encoder, &mdp5_encoder_helper_funcs);

	return encoder;
}
