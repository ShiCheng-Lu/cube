// SPDX-License-Identifier: GPL-2.0
/*
 * Jadard JD9365TX MIPI-DSI panel driver
 *
 * Supports the HT05S-30MI-A1 5.08" 720x720 panel (IVO glass, 4-lane MIPI DSI).
 * Initialization sequence taken from the vendor reference code
 * "IVO_5.08_INCH(G050PH)+JD9365TX_MIPI 4L.c".
 *
 * Structure modeled on drivers/gpu/drm/panel/panel-jadard-jd9365da-h3.c.
 */

#include <drm/drm_connector.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

/* Register page select. Pages 0x00..0x03 hold the vendor settings. */
#define JD9365TX_PAGE_SELECT	0xde
/* Password / extended command set unlock. */
#define JD9365TX_PASSWORD	0xdf

struct jd9365tx;

struct jd9365tx_panel_desc {
	const struct drm_display_mode *mode;
	unsigned int lanes;
	enum mipi_dsi_pixel_format format;
	unsigned long mode_flags;
	int (*init)(struct jd9365tx *ctx);

	/* Power sequencing delays (ms) */
	unsigned int vccio_to_vdd_delay_ms;
	unsigned int vdd_to_reset_delay_ms;
	unsigned int reset_assert_ms;
	unsigned int reset_to_init_delay_ms;
	unsigned int display_off_to_sleep_delay_ms;
	unsigned int sleep_to_power_off_delay_ms;
};

struct jd9365tx {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	const struct jd9365tx_panel_desc *desc;
	enum drm_panel_orientation orientation;
	struct regulator *vccio;	/* 1.8 V logic / IO */
	struct regulator *vdd;		/* 2.8 V analog (and 5 V boost enable) */
	struct gpio_desc *reset;
};

static inline struct jd9365tx *panel_to_jd9365tx(struct drm_panel *panel)
{
	return container_of(panel, struct jd9365tx, panel);
}

#define jd9365tx_switch_page(dsi_ctx, page) \
	mipi_dsi_generic_write_seq_multi(dsi_ctx, JD9365TX_PAGE_SELECT, (page))

/*
 * HT05S-30MI-A1 init sequence.
 *
 * The vendor code sends every register write with SSD_SEND(0x11, ...),
 * i.e. as generic (non-DCS) writes through an SSD2828 bridge in LP mode,
 * so the same packet type is used here.
 */
static int ht05s_30mi_a1_init_cmds(struct jd9365tx *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, JD9365TX_PASSWORD,
					 0x90, 0x84, 0x14);

	jd9365tx_switch_page(&dsi_ctx, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbb,
					 0x44, 0x67, 0xb5, 0x22, 0x22, 0xaa);

	jd9365tx_switch_page(&dsi_ctx, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb7,
					 0x16, 0x00, 0x6e, 0x00, 0x33);

	/* Page 0: panel / power / gamma / GIP */
	jd9365tx_switch_page(&dsi_ctx, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb3,
					 0x00, 0x01, 0x53, 0x53, 0x2b, 0x2b,
					 0x50, 0x00, 0x70, 0xb6);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbc, 0x0c, 0x38, 0x88);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbd, 0x00, 0x41, 0x41);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbf, 0x10, 0x35, 0xc3);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc0, 0xbd, 0xbd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc3,
					 0x03, 0x09, 0x00, 0x05, 0x30, 0xfd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc4,
					 0x06, 0xfe, 0x0e, 0xea, 0x06, 0x31,
					 0x07, 0x75, 0x07, 0x75, 0x07, 0x75,
					 0x00, 0xe0, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0xe0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc5,
					 0x01, 0x00, 0x8c, 0x00, 0x5e, 0x01,
					 0x7d, 0x09, 0x60, 0x02, 0xd0, 0x02,
					 0xd0, 0x00, 0x00, 0x02, 0x00, 0x02,
					 0x00, 0x00, 0x00, 0x00, 0x01, 0x0e,
					 0x01, 0x92, 0x02, 0x00, 0x02, 0xb0,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc6,
					 0x01, 0x47, 0x00, 0x78, 0x00, 0x14,
					 0x28, 0x82, 0x00, 0x00, 0x00, 0x01,
					 0x00, 0x01, 0x00, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc8, 0x10, 0x68, 0xb4);
	/* Gamma (positive / negative) */
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcb,
					 0x7c, 0x60, 0x50, 0x3a, 0x2a, 0x21,
					 0x13, 0x19, 0x06, 0x24, 0x28, 0x2b,
					 0x4c, 0x3c, 0x46, 0x3a, 0x39, 0x2e,
					 0x20, 0x14, 0x06,
					 0x7c, 0x60, 0x50, 0x3a, 0x2a, 0x21,
					 0x13, 0x19, 0x06, 0x24, 0x28, 0x2b,
					 0x4c, 0x3c, 0x46, 0x3a, 0x39, 0x2e,
					 0x20, 0x14, 0x06);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcc, 0x33);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcd,
					 0x34, 0x06, 0x34, 0x06, 0x34, 0x06,
					 0x34, 0x06, 0x1a, 0x1a, 0x1a, 0x1a);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0xff, 0xff, 0xff,
					 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
					 0xff, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcf,
					 0x40, 0x00, 0x00, 0x3f, 0xff, 0xff,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x3f, 0xff, 0xff, 0x00, 0x00, 0x00);
	/* GIP signal mapping */
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd0,
					 0x00, 0x3f, 0x07, 0x3f, 0x24, 0x1b,
					 0x19, 0x17, 0x15, 0x13, 0x11, 0x01,
					 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
					 0x36, 0x36, 0x36, 0x36, 0x36);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd1,
					 0x00, 0x3f, 0x3f, 0x06, 0x3f, 0x24,
					 0x1a, 0x18, 0x16, 0x14, 0x12, 0x10,
					 0x00, 0x36, 0x36, 0x36, 0x36, 0x36,
					 0x36, 0x36, 0x36, 0x36, 0x36);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd2,
					 0x00, 0x3f, 0x00, 0x24, 0x3f, 0x10,
					 0x12, 0x14, 0x16, 0x18, 0x1a, 0x06,
					 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
					 0x36, 0x36, 0x36, 0x36, 0x36);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd3,
					 0x00, 0xbf, 0xbf, 0x81, 0xa4, 0xbf,
					 0x11, 0x13, 0x15, 0x17, 0x19, 0x1b,
					 0x07, 0x36, 0x36, 0x36, 0x36, 0x36,
					 0x36, 0x36, 0x36, 0x36, 0x36);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd4,
					 0x00, 0x20, 0x0d, 0x01, 0x03, 0x20,
					 0x04, 0x00, 0x00, 0x00, 0x05, 0x81,
					 0x02, 0xe7, 0x01, 0x23, 0x45, 0x60,
					 0x00, 0x02, 0x00, 0x00, 0x00, 0x01,
					 0x05, 0x20, 0x02, 0xe9, 0x01, 0x03,
					 0x80, 0x0d, 0x00, 0x02, 0x02, 0xd8,
					 0x02, 0xd8, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
					 0x00, 0x00, 0x40, 0x01, 0x00, 0x00,
					 0x20, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd5,
					 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x00, 0x00, 0x20, 0x00, 0x00,
					 0x00, 0x0a, 0x78, 0x00, 0x00, 0x03,
					 0x5d, 0xb4, 0xa0, 0x11, 0x00, 0x00,
					 0x00, 0x00, 0x2f, 0x04);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd7,
					 0x00, 0x01, 0xff, 0x01, 0xff, 0x01,
					 0xff, 0x01, 0xff, 0x01, 0xff, 0x01,
					 0xff, 0x01, 0xff);

	/* Page 1 */
	jd9365tx_switch_page(&dsi_ctx, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb7,
					 0x43, 0x00, 0x00, 0x00, 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc7,
					 0x14, 0x14, 0x04, 0x04);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcd, 0x30, 0x53);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce,
					 0x09, 0x00, 0xe0, 0x00, 0xe0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xcf, 0x00, 0x65);

	/* Page 2 */
	jd9365tx_switch_page(&dsi_ctx, 0x02);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb3,
					 0x4a, 0xa3, 0xe2, 0x2f, 0x43);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb4, 0x7f);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbb,
					 0x00, 0x7d, 0x00, 0x00, 0x00, 0x00,
					 0x00, 0x41, 0x40, 0x43, 0x04);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbd, 0x1b);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xbf, 0x0f, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc1,
					 0x50, 0x40, 0x00, 0x00, 0x00, 0x02,
					 0x02, 0x02, 0x02, 0x00, 0x00, 0x00,
					 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc3, 0x20, 0xff);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc4,
					 0x00, 0x11, 0x07, 0x00, 0x18, 0x01,
					 0x08);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc6, 0x48, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe5,
					 0x00, 0x3c, 0x3c, 0x27, 0x8d, 0x09,
					 0x00, 0xc2, 0x01, 0x30, 0x05, 0x00,
					 0x00, 0x05, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe6,
					 0x10, 0x0b, 0x4e, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xe9, 0x10, 0xa0, 0xa0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xec, 0x0c, 0x77, 0x0d);

	/* Page 3 */
	jd9365tx_switch_page(&dsi_ctx, 0x03);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd1,
					 0x00, 0x00, 0x39, 0xff, 0x08);

	/* Back to page 0 for standard DCS commands */
	jd9365tx_switch_page(&dsi_ctx, 0x00);

	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_msleep(&dsi_ctx, 50);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 150);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 50);

	return dsi_ctx.accum_err;
}

/*
 * Timings from the panel parameter sheet (doc/屏参.png):
 *   H: active 720, FP 20, sync 20, BP 40  -> total 800
 *   V: active 720, FP 120, sync 2, BP 19  -> total 861
 *   800 * 861 * 60 Hz = 41.328 MHz
 */
static const struct drm_display_mode ht05s_30mi_a1_mode = {
	.clock		= 41328,

	.hdisplay	= 720,
	.hsync_start	= 720 + 20,
	.hsync_end	= 720 + 20 + 20,
	.htotal		= 720 + 20 + 20 + 40,

	.vdisplay	= 720,
	.vsync_start	= 720 + 120,
	.vsync_end	= 720 + 120 + 2,
	.vtotal		= 720 + 120 + 2 + 19,

	/* 5.08" diagonal, 1:1 aspect */
	.width_mm	= 91,
	.height_mm	= 91,
	.flags		= DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	.type		= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct jd9365tx_panel_desc ht05s_30mi_a1_desc = {
	.mode = &ht05s_30mi_a1_mode,
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	/* Vendor: SSD_MODE(0, 1) = non-burst with sync pulses */
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
		      MIPI_DSI_MODE_LPM,
	.init = ht05s_30mi_a1_init_cmds,

	/* Vendor main(): 1.8V on, 150ms, 2.8V/5V on, 150ms, boost, 50ms */
	.vccio_to_vdd_delay_ms = 150,
	.vdd_to_reset_delay_ms = 200,
	/* Vendor: LCD reset low 100ms, high 200ms */
	.reset_assert_ms = 100,
	.reset_to_init_delay_ms = 200,
	/* Vendor uses 200ms / 1000ms; 120ms after sleep-in meets the spec */
	.display_off_to_sleep_delay_ms = 50,
	.sleep_to_power_off_delay_ms = 120,
};

static int jd9365tx_prepare(struct drm_panel *panel)
{
	struct jd9365tx *ctx = panel_to_jd9365tx(panel);
	const struct jd9365tx_panel_desc *desc = ctx->desc;
	int ret;

	gpiod_set_value_cansleep(ctx->reset, 1);

	ret = regulator_enable(ctx->vccio);
	if (ret)
		return ret;
	msleep(desc->vccio_to_vdd_delay_ms);

	ret = regulator_enable(ctx->vdd);
	if (ret)
		goto err_vccio;
	msleep(desc->vdd_to_reset_delay_ms);

	/* Reset is active-low in DT: 1 = asserted */
	gpiod_set_value_cansleep(ctx->reset, 1);
	msleep(desc->reset_assert_ms);
	gpiod_set_value_cansleep(ctx->reset, 0);
	msleep(desc->reset_to_init_delay_ms);

	ret = desc->init(ctx);
	if (ret) {
		dev_err(panel->dev, "failed to initialize panel: %d\n", ret);
		goto err_vdd;
	}

	return 0;

err_vdd:
	gpiod_set_value_cansleep(ctx->reset, 1);
	regulator_disable(ctx->vdd);
err_vccio:
	regulator_disable(ctx->vccio);
	return ret;
}

static int jd9365tx_disable(struct drm_panel *panel)
{
	struct jd9365tx *ctx = panel_to_jd9365tx(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, ctx->desc->display_off_to_sleep_delay_ms);

	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, ctx->desc->sleep_to_power_off_delay_ms);

	return dsi_ctx.accum_err;
}

static int jd9365tx_unprepare(struct drm_panel *panel)
{
	struct jd9365tx *ctx = panel_to_jd9365tx(panel);

	gpiod_set_value_cansleep(ctx->reset, 1);
	usleep_range(5000, 6000);

	/* Vendor: drop 2.8V/5V first, then 1.8V */
	regulator_disable(ctx->vdd);
	msleep(150);
	regulator_disable(ctx->vccio);

	return 0;
}

static int jd9365tx_get_modes(struct drm_panel *panel,
			      struct drm_connector *connector)
{
	struct jd9365tx *ctx = panel_to_jd9365tx(panel);
	static const u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	int count;

	count = drm_connector_helper_get_modes_fixed(connector, ctx->desc->mode);
	drm_display_info_set_bus_formats(&connector->display_info,
					 &bus_format, 1);

	return count;
}

static enum drm_panel_orientation
jd9365tx_get_orientation(struct drm_panel *panel)
{
	struct jd9365tx *ctx = panel_to_jd9365tx(panel);

	return ctx->orientation;
}

static const struct drm_panel_funcs jd9365tx_funcs = {
	.prepare = jd9365tx_prepare,
	.disable = jd9365tx_disable,
	.unprepare = jd9365tx_unprepare,
	.get_modes = jd9365tx_get_modes,
	.get_orientation = jd9365tx_get_orientation,
};

static int jd9365tx_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct jd9365tx_panel_desc *desc;
	struct jd9365tx *ctx;
	int ret;

	desc = of_device_get_match_data(dev);
	if (!desc)
		return -ENODEV;

	/*
	 * devm_drm_panel_alloc() only exists from v6.15; use the older
	 * kzalloc + drm_panel_init() pattern so this builds on rpi-6.12.y.
	 */
	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dsi = dsi;
	ctx->desc = desc;

	dsi->lanes = desc->lanes;
	dsi->format = desc->format;
	dsi->mode_flags = desc->mode_flags;

	ctx->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset))
		return dev_err_probe(dev, PTR_ERR(ctx->reset),
				     "failed to get reset GPIO\n");

	/* Missing supplies fall back to dummy regulators */
	ctx->vccio = devm_regulator_get(dev, "vccio");
	if (IS_ERR(ctx->vccio))
		return dev_err_probe(dev, PTR_ERR(ctx->vccio),
				     "failed to get vccio regulator\n");

	ctx->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(ctx->vdd))
		return dev_err_probe(dev, PTR_ERR(ctx->vdd),
				     "failed to get vdd regulator\n");

	drm_panel_init(&ctx->panel, dev, &jd9365tx_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = of_drm_get_panel_orientation(dev->of_node, &ctx->orientation);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to get orientation\n");

	/* Optional "backlight" phandle in DT */
	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get backlight\n");

	/* Init commands are sent in prepare(): DSI host must be in LP-11 */
	ctx->panel.prepare_prev_first = true;

	drm_panel_add(&ctx->panel);
	mipi_dsi_set_drvdata(dsi, ctx);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "failed to attach to DSI host\n");
	}

	return 0;
}

static void jd9365tx_remove(struct mipi_dsi_device *dsi)
{
	struct jd9365tx *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id jd9365tx_of_match[] = {
	{ .compatible = "custom,ht05s-30mi-a1", .data = &ht05s_30mi_a1_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jd9365tx_of_match);

static struct mipi_dsi_driver jd9365tx_driver = {
	.probe = jd9365tx_probe,
	.remove = jd9365tx_remove,
	.driver = {
		.name = "panel-jadard-jd9365tx",
		.of_match_table = jd9365tx_of_match,
	},
};
module_mipi_dsi_driver(jd9365tx_driver);

MODULE_AUTHOR("ShiCheng Lu");
MODULE_DESCRIPTION("Jadard JD9365TX MIPI-DSI panel driver");
MODULE_LICENSE("GPL");
