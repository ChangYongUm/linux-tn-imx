// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017-2018, Bootlin
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>


struct ili9881c {
	struct drm_panel	panel;
	struct mipi_dsi_device	*dsi;

	struct regulator	*power;
	struct gpio_desc	*reset;
	u32	timing_mode;
};

enum ili9881c_op {
	ILI9881C_SWITCH_PAGE,
	ILI9881C_COMMAND,
};

struct ili9881c_instr {
	enum ili9881c_op	op;

	union arg {
		struct cmd {
			u8	cmd;
			u8	data;
		} cmd;
		u8	page;
	} arg;
};

#define ILI9881C_SWITCH_PAGE_INSTR(_page)	\
	{					\
		.op = ILI9881C_SWITCH_PAGE,	\
		.arg = {			\
			.page = (_page),	\
		},				\
	}

#define ILI9881C_COMMAND_INSTR(_cmd, _data)		\
	{						\
		.op = ILI9881C_COMMAND,		\
		.arg = {				\
			.cmd = {			\
				.cmd = (_cmd),		\
				.data = (_data),	\
			},				\
		},					\
	}

static const struct ili9881c_instr ili9881c_init[] = {
	ILI9881C_SWITCH_PAGE_INSTR(3),
	ILI9881C_COMMAND_INSTR(0x01, 0x00),
	ILI9881C_COMMAND_INSTR(0x02, 0x00),
	ILI9881C_COMMAND_INSTR(0x03, 0x73),
	ILI9881C_COMMAND_INSTR(0x04, 0x00),
	ILI9881C_COMMAND_INSTR(0x05, 0x00),
	ILI9881C_COMMAND_INSTR(0x06, 0x0a),
	ILI9881C_COMMAND_INSTR(0x07, 0x00),
	ILI9881C_COMMAND_INSTR(0x08, 0x00),
	ILI9881C_COMMAND_INSTR(0x09, 0x01),
	ILI9881C_COMMAND_INSTR(0x0a, 0x00),
	ILI9881C_COMMAND_INSTR(0x0b, 0x00),
	ILI9881C_COMMAND_INSTR(0x0c, 0x01),
	ILI9881C_COMMAND_INSTR(0x0d, 0x00),
	ILI9881C_COMMAND_INSTR(0x0e, 0x00),
	ILI9881C_COMMAND_INSTR(0x0f, 0x14),
	ILI9881C_COMMAND_INSTR(0x10, 0x14),
	ILI9881C_COMMAND_INSTR(0x11, 0x00),
	ILI9881C_COMMAND_INSTR(0x12, 0x00),
	ILI9881C_COMMAND_INSTR(0x13, 0x00),
	ILI9881C_COMMAND_INSTR(0x14, 0x00),
	ILI9881C_COMMAND_INSTR(0x15, 0x00),
	ILI9881C_COMMAND_INSTR(0x16, 0x00),
	ILI9881C_COMMAND_INSTR(0x17, 0x00),
	ILI9881C_COMMAND_INSTR(0x18, 0x00),
	ILI9881C_COMMAND_INSTR(0x19, 0x00),
	ILI9881C_COMMAND_INSTR(0x1a, 0x00),
	ILI9881C_COMMAND_INSTR(0x1b, 0x00),
	ILI9881C_COMMAND_INSTR(0x1c, 0x00),
	ILI9881C_COMMAND_INSTR(0x1d, 0x00),
	ILI9881C_COMMAND_INSTR(0x1e, 0x40),
	ILI9881C_COMMAND_INSTR(0x1f, 0x80),
	ILI9881C_COMMAND_INSTR(0x20, 0x06),
	ILI9881C_COMMAND_INSTR(0x21, 0x01),
	ILI9881C_COMMAND_INSTR(0x22, 0x00),
	ILI9881C_COMMAND_INSTR(0x23, 0x00),
	ILI9881C_COMMAND_INSTR(0x24, 0x00),
	ILI9881C_COMMAND_INSTR(0x25, 0x00),
	ILI9881C_COMMAND_INSTR(0x26, 0x00),
	ILI9881C_COMMAND_INSTR(0x27, 0x00),
	ILI9881C_COMMAND_INSTR(0x28, 0x33),
	ILI9881C_COMMAND_INSTR(0x29, 0x03),
	ILI9881C_COMMAND_INSTR(0x2a, 0x00),
	ILI9881C_COMMAND_INSTR(0x2b, 0x00),
	ILI9881C_COMMAND_INSTR(0x2c, 0x00),
	ILI9881C_COMMAND_INSTR(0x2d, 0x00),
	ILI9881C_COMMAND_INSTR(0x2e, 0x00),
	ILI9881C_COMMAND_INSTR(0x2f, 0x00),
	ILI9881C_COMMAND_INSTR(0x30, 0x00),
	ILI9881C_COMMAND_INSTR(0x31, 0x00),
	ILI9881C_COMMAND_INSTR(0x32, 0x00),
	ILI9881C_COMMAND_INSTR(0x33, 0x00),
	ILI9881C_COMMAND_INSTR(0x34, 0x04),
	ILI9881C_COMMAND_INSTR(0x35, 0x00),
	ILI9881C_COMMAND_INSTR(0x36, 0x00),
	ILI9881C_COMMAND_INSTR(0x37, 0x00),
	ILI9881C_COMMAND_INSTR(0x38, 0x78),
	ILI9881C_COMMAND_INSTR(0x39, 0x00),
	ILI9881C_COMMAND_INSTR(0x3a, 0x40),
	ILI9881C_COMMAND_INSTR(0x3b, 0x40),
	ILI9881C_COMMAND_INSTR(0x3c, 0x00),
	ILI9881C_COMMAND_INSTR(0x3d, 0x00),
	ILI9881C_COMMAND_INSTR(0x3e, 0x00),
	ILI9881C_COMMAND_INSTR(0x3f, 0x00),
	ILI9881C_COMMAND_INSTR(0x40, 0x00),
	ILI9881C_COMMAND_INSTR(0x41, 0x00),
	ILI9881C_COMMAND_INSTR(0x42, 0x00),
	ILI9881C_COMMAND_INSTR(0x43, 0x00),
	ILI9881C_COMMAND_INSTR(0x44, 0x00),
	ILI9881C_COMMAND_INSTR(0x50, 0x01),
	ILI9881C_COMMAND_INSTR(0x51, 0x23),
	ILI9881C_COMMAND_INSTR(0x52, 0x45),
	ILI9881C_COMMAND_INSTR(0x53, 0x67),
	ILI9881C_COMMAND_INSTR(0x54, 0x89),
	ILI9881C_COMMAND_INSTR(0x55, 0xab),
	ILI9881C_COMMAND_INSTR(0x56, 0x01),
	ILI9881C_COMMAND_INSTR(0x57, 0x23),
	ILI9881C_COMMAND_INSTR(0x58, 0x45),
	ILI9881C_COMMAND_INSTR(0x59, 0x67),
	ILI9881C_COMMAND_INSTR(0x5a, 0x89),
	ILI9881C_COMMAND_INSTR(0x5b, 0xab),
	ILI9881C_COMMAND_INSTR(0x5c, 0xcd),
	ILI9881C_COMMAND_INSTR(0x5d, 0xef),
	ILI9881C_COMMAND_INSTR(0x5e, 0x11),
	ILI9881C_COMMAND_INSTR(0x5f, 0x01),
	ILI9881C_COMMAND_INSTR(0x60, 0x00),
	ILI9881C_COMMAND_INSTR(0x61, 0x15),
	ILI9881C_COMMAND_INSTR(0x62, 0x14),
	ILI9881C_COMMAND_INSTR(0x63, 0x0e),
	ILI9881C_COMMAND_INSTR(0x64, 0x0f),
	ILI9881C_COMMAND_INSTR(0x65, 0x0c),
	ILI9881C_COMMAND_INSTR(0x66, 0x0d),
	ILI9881C_COMMAND_INSTR(0x67, 0x06),
	ILI9881C_COMMAND_INSTR(0x68, 0x02),
	ILI9881C_COMMAND_INSTR(0x69, 0x07),
	ILI9881C_COMMAND_INSTR(0x6a, 0x02),
	ILI9881C_COMMAND_INSTR(0x6b, 0x02),
	ILI9881C_COMMAND_INSTR(0x6c, 0x02),
	ILI9881C_COMMAND_INSTR(0x6d, 0x02),
	ILI9881C_COMMAND_INSTR(0x6e, 0x02),
	ILI9881C_COMMAND_INSTR(0x6f, 0x02),
	ILI9881C_COMMAND_INSTR(0x70, 0x02),
	ILI9881C_COMMAND_INSTR(0x71, 0x02),
	ILI9881C_COMMAND_INSTR(0x72, 0x02),
	ILI9881C_COMMAND_INSTR(0x73, 0x02),
	ILI9881C_COMMAND_INSTR(0x74, 0x02),
	ILI9881C_COMMAND_INSTR(0x75, 0x01),
	ILI9881C_COMMAND_INSTR(0x76, 0x00),
	ILI9881C_COMMAND_INSTR(0x77, 0x14),
	ILI9881C_COMMAND_INSTR(0x78, 0x15),
	ILI9881C_COMMAND_INSTR(0x79, 0x0e),
	ILI9881C_COMMAND_INSTR(0x7a, 0x0f),
	ILI9881C_COMMAND_INSTR(0x7b, 0x0c),
	ILI9881C_COMMAND_INSTR(0x7c, 0x0d),
	ILI9881C_COMMAND_INSTR(0x7d, 0x06),
	ILI9881C_COMMAND_INSTR(0x7e, 0x02),
	ILI9881C_COMMAND_INSTR(0x7f, 0x07),
	ILI9881C_COMMAND_INSTR(0x80, 0x02),
	ILI9881C_COMMAND_INSTR(0x81, 0x02),
	ILI9881C_COMMAND_INSTR(0x82, 0x02),
	ILI9881C_COMMAND_INSTR(0x83, 0x02),
	ILI9881C_COMMAND_INSTR(0x84, 0x02),
	ILI9881C_COMMAND_INSTR(0x85, 0x02),
	ILI9881C_COMMAND_INSTR(0x86, 0x02),
	ILI9881C_COMMAND_INSTR(0x87, 0x02),
	ILI9881C_COMMAND_INSTR(0x88, 0x02),
	ILI9881C_COMMAND_INSTR(0x89, 0x02),
	ILI9881C_COMMAND_INSTR(0x8A, 0x02),

	ILI9881C_SWITCH_PAGE_INSTR(4),
	ILI9881C_COMMAND_INSTR(0x00, 0x80),
	ILI9881C_COMMAND_INSTR(0x6C, 0x15),
	ILI9881C_COMMAND_INSTR(0x6E, 0x2a),
	ILI9881C_COMMAND_INSTR(0x6F, 0x33),
	ILI9881C_COMMAND_INSTR(0x3A, 0x94),
	ILI9881C_COMMAND_INSTR(0x8D, 0x1a),
	ILI9881C_COMMAND_INSTR(0x87, 0xba),
	ILI9881C_COMMAND_INSTR(0x26, 0x76),
	ILI9881C_COMMAND_INSTR(0xB2, 0xd1),
	ILI9881C_COMMAND_INSTR(0xB5, 0x06),

	ILI9881C_SWITCH_PAGE_INSTR(1),
	ILI9881C_COMMAND_INSTR(0x22, 0x0A),
	ILI9881C_COMMAND_INSTR(0x31, 0x00),
	ILI9881C_COMMAND_INSTR(0x53, 0x8C),
	ILI9881C_COMMAND_INSTR(0x55, 0x8F),
	ILI9881C_COMMAND_INSTR(0x50, 0xc0),
	ILI9881C_COMMAND_INSTR(0x51, 0xc0),
	ILI9881C_COMMAND_INSTR(0x60, 0x08),
	ILI9881C_COMMAND_INSTR(0xA0, 0x08),
	ILI9881C_COMMAND_INSTR(0xA1, 0x19),
	ILI9881C_COMMAND_INSTR(0xA2, 0x26),
	ILI9881C_COMMAND_INSTR(0xA3, 0x1a),
	ILI9881C_COMMAND_INSTR(0xA4, 0x1d),
	ILI9881C_COMMAND_INSTR(0xA5, 0x2c),
	ILI9881C_COMMAND_INSTR(0xA6, 0x21),
	ILI9881C_COMMAND_INSTR(0xA7, 0x22),
	ILI9881C_COMMAND_INSTR(0xA8, 0x7c),
	ILI9881C_COMMAND_INSTR(0xA9, 0x21),
	ILI9881C_COMMAND_INSTR(0xAA, 0x2e),
	ILI9881C_COMMAND_INSTR(0xAB, 0x66),
	ILI9881C_COMMAND_INSTR(0xAC, 0x1C),
	ILI9881C_COMMAND_INSTR(0xAD, 0x18),
	ILI9881C_COMMAND_INSTR(0xAE, 0x4e),
	ILI9881C_COMMAND_INSTR(0xAF, 0x1a),
	ILI9881C_COMMAND_INSTR(0xB0, 0x22),
	ILI9881C_COMMAND_INSTR(0xB1, 0x49),
	ILI9881C_COMMAND_INSTR(0xB2, 0x56),
	ILI9881C_COMMAND_INSTR(0xB3, 0x39),
	ILI9881C_COMMAND_INSTR(0xC0, 0x08),
	ILI9881C_COMMAND_INSTR(0xC1, 0x1a),
	ILI9881C_COMMAND_INSTR(0xC2, 0x26),
	ILI9881C_COMMAND_INSTR(0xC3, 0x0b),
	ILI9881C_COMMAND_INSTR(0xC4, 0x0e),
	ILI9881C_COMMAND_INSTR(0xC5, 0x24),
	ILI9881C_COMMAND_INSTR(0xC6, 0x18),
	ILI9881C_COMMAND_INSTR(0xC7, 0x1b),
	ILI9881C_COMMAND_INSTR(0xC8, 0x85),
	ILI9881C_COMMAND_INSTR(0xC9, 0x17),
	ILI9881C_COMMAND_INSTR(0xCA, 0x23),
	ILI9881C_COMMAND_INSTR(0xCB, 0x79),
	ILI9881C_COMMAND_INSTR(0xCC, 0x1C),
	ILI9881C_COMMAND_INSTR(0xCD, 0x1f),
	ILI9881C_COMMAND_INSTR(0xCE, 0x50),
	ILI9881C_COMMAND_INSTR(0xCF, 0x2d),
	ILI9881C_COMMAND_INSTR(0xD0, 0x31),
	ILI9881C_COMMAND_INSTR(0xD1, 0x49),
	ILI9881C_COMMAND_INSTR(0xD2, 0x57),
	ILI9881C_COMMAND_INSTR(0xD3, 0x39),

	ILI9881C_SWITCH_PAGE_INSTR(0),
};

static inline struct ili9881c *panel_to_ili9881c(struct drm_panel *panel)
{
	return container_of(panel, struct ili9881c, panel);
}

/*
 * The panel seems to accept some private DCS commands that map
 * directly to registers.
 *
 * It is organised by page, with each page having its own set of
 * registers, and the first page looks like it's holding the standard
 * DCS commands.
 *
 * So before any attempt at sending a command or data, we have to be
 * sure if we're in the right page or not.
 */
static int ili9881c_switch_page(struct ili9881c *ctx, u8 page)
{
	u8 buf[4] = { 0xff, 0x98, 0x81, page };
	int ret;

	ret = mipi_dsi_dcs_write_buffer(ctx->dsi, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	return 0;
}

static int ili9881c_send_cmd_data(struct ili9881c *ctx, u8 cmd, u8 data)
{
	u8 buf[2] = { cmd, data };
	int ret;

	ret = mipi_dsi_dcs_write_buffer(ctx->dsi, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	return 0;
}

static int ili9881c_prepare(struct drm_panel *panel)
{
	struct ili9881c *ctx = panel_to_ili9881c(panel);
	int ret;

	/* Power the panel */
	if (!IS_ERR(ctx->power)) {
		ret = regulator_enable(ctx->power);
		msleep(5);
	}
	/* And reset it */
	if (!IS_ERR(ctx->reset)) {
		gpiod_set_value_cansleep(ctx->reset, 1);
		msleep(20);

		gpiod_set_value_cansleep(ctx->reset, 0);
		msleep(20);
	}

	return ret;
}

static int ili9881c_enable(struct drm_panel *panel)
{
	struct ili9881c *ctx = panel_to_ili9881c(panel);
	unsigned int i;
	int ret;

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	for (i = 0; i < ARRAY_SIZE(ili9881c_init); i++) {
		const struct ili9881c_instr *instr = &ili9881c_init[i];

		if (instr->op == ILI9881C_SWITCH_PAGE)
			ret = ili9881c_switch_page(ctx, instr->arg.page);
		else if (instr->op == ILI9881C_COMMAND)
			ret = ili9881c_send_cmd_data(ctx, instr->arg.cmd.cmd,
						      instr->arg.cmd.data);

		if (ret)
			return ret;
	}

	ret = ili9881c_switch_page(ctx, 0);
	if (ret)
		return ret;

	ret = mipi_dsi_dcs_set_tear_on(ctx->dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret)
		return ret;

	ret = mipi_dsi_dcs_exit_sleep_mode(ctx->dsi);
	if (ret)
		return ret;

	msleep(120);

	mipi_dsi_dcs_set_display_on(ctx->dsi);

	return 0;
}

static int ili9881c_disable(struct drm_panel *panel)
{
	struct ili9881c *ctx = panel_to_ili9881c(panel);
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	usleep_range(10000, 12000);

	ret = mipi_dsi_dcs_set_display_off(ctx->dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display OFF (%d)\n", ret);
		return ret;
	}

	usleep_range(5000, 10000);

	ret = mipi_dsi_dcs_enter_sleep_mode(ctx->dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int ili9881c_unprepare(struct drm_panel *panel)
{
	struct ili9881c *ctx = panel_to_ili9881c(panel);

	if (!IS_ERR(ctx->power))
		regulator_disable(ctx->power);

	if (!IS_ERR(ctx->reset))
		gpiod_set_value_cansleep(ctx->reset, 1);

	return 0;
}

static const struct drm_display_mode high_clk_mode = {
	.clock		= 74250,
	.hdisplay	= 720,
	.hsync_start	= 720 + 34,
	.hsync_end	= 720 + 34 + 100,
	.htotal	= 720 + 34 + 100 + 100,
	.vdisplay	= 1280,
	.vsync_start	= 1280 + 2,
	.vsync_end	= 1280 + 2 + 30,
	.vtotal	= 1280 + 2 + 30 + 20,
};

static const struct drm_display_mode default_mode = {
	.clock		= 62000,
	.hdisplay	= 720,
	.hsync_start	= 720 + 10,
	.hsync_end	= 720 + 10 + 20,
	.htotal		= 720 + 10 + 20 + 30,
	.vdisplay	= 1280,
	.vsync_start	= 1280 + 10,
	.vsync_end	= 1280 + 10 + 10,
	.vtotal		= 1280 + 10 + 10 + 20,
};

static int ili9881c_get_modes(struct drm_panel *panel, struct drm_connector *connector)
{
	struct ili9881c *ctx = panel_to_ili9881c(panel);
	struct drm_display_mode *mode;
	struct drm_device *drm = connector->dev;
	const struct drm_display_mode *display_mode;
	u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	int ret;

	switch (ctx->timing_mode) {
		case 0:
			display_mode = &default_mode;
			break;
		case 1:
			display_mode = &high_clk_mode;
			break;
		default:
			dev_warn(&ctx->dsi->dev, "invalid timing mode %d, fail back to use default mode\n", ctx->timing_mode);
			display_mode = &default_mode;
			break;

	}

	mode = drm_mode_duplicate(drm, display_mode);
	if (!mode) {
		dev_err(&ctx->dsi->dev, "failed to add mode %ux%ux@60\n",
			display_mode->hdisplay, display_mode->vdisplay);
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	ret = drm_display_info_set_bus_formats(&connector->display_info,
					       &bus_format, 1);
	if (ret)
		return ret;

	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = 62;
	connector->display_info.height_mm = 110;

	return 1;
}

static const struct drm_panel_funcs ili9881c_funcs = {
	.prepare	= ili9881c_prepare,
	.unprepare	= ili9881c_unprepare,
	.enable		= ili9881c_enable,
	.disable	= ili9881c_disable,
	.get_modes	= ili9881c_get_modes,
};

static int ili9881c_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *np = dev->of_node;
	struct ili9881c *ctx;
	int ret;
	u32 video_mode;

	ctx = devm_kzalloc(&dsi->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dsi = dsi;

	drm_panel_init(&ctx->panel, dev, &ili9881c_funcs, DRM_MODE_CONNECTOR_DSI);
	ctx->panel.dev = &dsi->dev;
	ctx->panel.funcs = &ili9881c_funcs;

	ctx->power = devm_regulator_get(&dsi->dev, "power");
	if (IS_ERR(ctx->power)) {
		dev_err(&dsi->dev, "Couldn't get our power regulator\n");
	}

	ctx->reset = devm_gpiod_get(&dsi->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset)) {
		dev_err(&dsi->dev, "Couldn't get our reset GPIO\n");
	}

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	drm_panel_add(&ctx->panel);

	ret = of_property_read_u32(np, "timing-mode", &ctx->timing_mode);
	if (ret < 0) {
		dev_err(&dsi->dev, "Failed to get timing-mode, use default timing-mode (%d)\n", ret);
		ctx->timing_mode = 0;
		return ret;
	}

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags =  MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO |
			   MIPI_DSI_CLOCK_NON_CONTINUOUS;

	ret = of_property_read_u32(np, "video-mode", &video_mode);
	if (!ret) {
		switch (video_mode) {
		case 0:
			/* burst mode */
			dsi->mode_flags |= MIPI_DSI_MODE_VIDEO_BURST;
			break;
		case 1:
			/* non-burst mode with sync event */
			break;
		case 2:
			/* non-burst mode with sync pulse */
			dsi->mode_flags |= MIPI_DSI_MODE_VIDEO_SYNC_PULSE;
			break;
		case 3:
			/* disable clock non-continuous mode, enable burst and sync pulse mode */
			dsi->mode_flags = MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
					  MIPI_DSI_MODE_VIDEO_SYNC_PULSE;
			break;
		default:
			dev_warn(dev, "invalid video mode %d\n", video_mode);
			break;

		}
	}

	return mipi_dsi_attach(dsi);
}

static int ili9881c_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct ili9881c *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id ili9881c_of_match[] = {
	{ .compatible = "bananapi,lhr050h41" },
	{ }
};
MODULE_DEVICE_TABLE(of, ili9881c_of_match);

static struct mipi_dsi_driver ili9881c_dsi_driver = {
	.probe		= ili9881c_dsi_probe,
	.remove		= ili9881c_dsi_remove,
	.driver = {
		.name		= "ili9881c-dsi",
		.of_match_table	= ili9881c_of_match,
	},
};
module_mipi_dsi_driver(ili9881c_dsi_driver);

MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Ilitek ILI9881C Controller Driver");
MODULE_LICENSE("GPL v2");
