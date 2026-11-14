// SPDX-License-Identifier: GPL-2.0
#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct z50006_panel {
    struct drm_panel panel;
    struct mipi_dsi_device* dsi;
    struct gpio_desc* reset_gpio;
    struct regulator* supply;
};

static inline struct z50006_panel* to_z50006_panel(struct drm_panel* panel) {
    return container_of(panel, struct z50006_panel, panel);
}

#define generic_write_seq(dsi, seq...)                       \
    do {                                                     \
        static const u8 d[] = {seq};                         \
        int ret;                                             \
        ret = mipi_dsi_generic_write(dsi, d, ARRAY_SIZE(d)); \
        if (ret < 0)                                         \
            return ret;                                      \
    } while (0)

static int z50006_init_sequence(struct z50006_panel* ctx) {
    struct mipi_dsi_device* dsi = ctx->dsi;
    int ret;

    /* Vendor Z50006 2-lane init sequence (SSD_SEND 0x11 -> Generic Write) */
    generic_write_seq(dsi, 0xDF, 0x90, 0x84, 0x14);
    generic_write_seq(dsi, 0xDE, 0x00);
    generic_write_seq(dsi, 0xBB, 0x44, 0x67, 0xB5, 0x22, 0x22, 0xAA);
    generic_write_seq(dsi, 0xDE, 0x02);

    generic_write_seq(dsi, 0xCC, 0x31);
    generic_write_seq(dsi, 0xB7, 0x16, 0x00, 0x6E, 0x00, 0x33);
    generic_write_seq(dsi, 0xDE, 0x00);
    generic_write_seq(dsi, 0xB3, 0x00, 0x01, 0x53, 0x53, 0x2B, 0x2B, 0x50, 0x00,
                      0x70, 0xB6);
    generic_write_seq(dsi, 0xBC, 0x0C, 0x38, 0x88);
    generic_write_seq(dsi, 0xBD, 0x00, 0x41, 0x41);
    generic_write_seq(dsi, 0xBF, 0x10, 0x35, 0xC3);
    generic_write_seq(dsi, 0xC0, 0xBD, 0xBD);
    generic_write_seq(dsi, 0xC3, 0x03, 0x09, 0x00, 0x05, 0x30, 0xFD);
    generic_write_seq(dsi, 0xC4, 0x06, 0xFE, 0x0E, 0xEA, 0x06, 0x31, 0x07, 0x75,
                      0x07, 0x75, 0x07, 0x75, 0x00, 0xE0, 0x00, 0x00, 0x00,
                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0);
    generic_write_seq(dsi, 0xC5, 0x01, 0x00, 0x8C, 0x00, 0x5E, 0x01, 0x7D, 0x09,
                      0x60, 0x02, 0xD0, 0x02, 0xD0, 0x00, 0x00, 0x02, 0x00,
                      0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x0E, 0x01, 0x92,
                      0x02, 0x00, 0x02, 0xB0, 0x00, 0x00, 0x00, 0x00, 0x00,
                      0x00);
    generic_write_seq(dsi, 0xC6, 0x01, 0x47, 0x00, 0x78, 0x00, 0x14, 0x28, 0x82,
                      0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x01);
    generic_write_seq(dsi, 0xC8, 0x10, 0x68, 0xB4);
    generic_write_seq(dsi, 0xCB, 0x7C, 0x60, 0x50, 0x3A, 0x2A, 0x21, 0x13, 0x19,
                      0x06, 0x24, 0x28, 0x2B, 0x4C, 0x3C, 0x46, 0x3A, 0x39,
                      0x2E, 0x20, 0x14, 0x06, 0x7C, 0x60, 0x50, 0x3A, 0x2A,
                      0x21, 0x13, 0x19, 0x06, 0x24, 0x28, 0x2B, 0x4C, 0x3C,
                      0x46, 0x3A, 0x39, 0x2E, 0x20, 0x14, 0x06);
    generic_write_seq(dsi, 0xCD, 0x34, 0x06, 0x34, 0x06, 0x34, 0x06, 0x34, 0x06,
                      0x1A, 0x1A, 0x1A, 0x1A);
    generic_write_seq(
        dsi, 0xCE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
    generic_write_seq(dsi, 0xCF, 0x40, 0x00, 0x00, 0x3F, 0xFF, 0xFF, 0x00, 0x00,
                      0x00, 0x00, 0x00, 0x00, 0x3F, 0xFF, 0xFF, 0x00, 0x00,
                      0x00);
    generic_write_seq(dsi, 0xD0, 0x00, 0x3F, 0x07, 0x3F, 0x24, 0x1B, 0x19, 0x17,
                      0x15, 0x13, 0x11, 0x01, 0x36, 0x36, 0x36, 0x36, 0x36,
                      0x36, 0x36, 0x36, 0x36, 0x36, 0x36);
    generic_write_seq(dsi, 0xD1, 0x00, 0x3F, 0x3F, 0x06, 0x3F, 0x24, 0x1A, 0x18,
                      0x16, 0x14, 0x12, 0x10, 0x00, 0x36, 0x36, 0x36, 0x36,
                      0x36, 0x36, 0x36, 0x36, 0x36, 0x36);
    generic_write_seq(dsi, 0xD2, 0x00, 0x3F, 0x00, 0x24, 0x3F, 0x10, 0x12, 0x14,
                      0x16, 0x18, 0x1A, 0x06, 0x36, 0x36, 0x36, 0x36, 0x36,
                      0x36, 0x36, 0x36, 0x36, 0x36, 0x36);
    generic_write_seq(dsi, 0xD3, 0x00, 0xBF, 0xBF, 0x81, 0xA4, 0xBF, 0x11, 0x13,
                      0x15, 0x17, 0x19, 0x1B, 0x07, 0x36, 0x36, 0x36, 0x36,
                      0x36, 0x36, 0x36, 0x36, 0x36, 0x36);
    generic_write_seq(
        dsi, 0xD4, 0x00, 0x20, 0x0D, 0x01, 0x03, 0x20, 0x04, 0x00, 0x00, 0x00,
        0x05, 0x81, 0x02, 0xE7, 0x01, 0x23, 0x45, 0x60, 0x00, 0x02, 0x00, 0x00,
        0x00, 0x01, 0x05, 0x20, 0x02, 0xE9, 0x01, 0x03, 0x80, 0x0D, 0x00, 0x02,
        0x02, 0xD8, 0x02, 0xD8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x10, 0x00, 0x00, 0x00, 0x40, 0x01, 0x00, 0x00, 0x20, 0x00);
    generic_write_seq(dsi, 0xD5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                      0x00, 0x20, 0x00, 0x00, 0x00, 0x0A, 0x78, 0x00, 0x00,
                      0x03, 0x5D, 0xB4, 0xA0, 0x11, 0x00, 0x00, 0x00, 0x00,
                      0x2F, 0x04);
    generic_write_seq(dsi, 0xD7, 0x00, 0x01, 0xFF, 0x01, 0xFF, 0x01, 0xFF, 0x01,
                      0xFF, 0x01, 0xFF, 0x01, 0xFF, 0x01, 0xFF);

    generic_write_seq(dsi, 0xDE, 0x01);
    generic_write_seq(dsi, 0xB7, 0x43, 0x00, 0x00, 0x00, 0x08);
    generic_write_seq(dsi, 0xC7, 0x14, 0x14, 0x04, 0x04);
    generic_write_seq(dsi, 0xCD, 0x30, 0x53);
    generic_write_seq(dsi, 0xCE, 0x09, 0x00, 0xE0, 0x00, 0xE0);
    generic_write_seq(dsi, 0xCF, 0x00, 0x65);

    generic_write_seq(dsi, 0xDE, 0x02);
    generic_write_seq(dsi, 0xB3, 0x4A, 0xA3, 0xE2, 0x2F, 0x43);
    generic_write_seq(dsi, 0xB4, 0x7F);
    generic_write_seq(dsi, 0xBB, 0x00, 0x7D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x41,
                      0x40, 0x43, 0x04);
    generic_write_seq(dsi, 0xBD, 0x1B);
    generic_write_seq(dsi, 0xBF, 0x0F, 0x03);
    generic_write_seq(dsi, 0xC1, 0x50, 0x40, 0x00, 0x00, 0x00, 0x02, 0x02, 0x02,
                      0x02, 0x00, 0x00, 0x00, 0x00);
    generic_write_seq(dsi, 0xC3, 0x20, 0xFF);
    generic_write_seq(dsi, 0xC4, 0x00, 0x11, 0x07, 0x00, 0x18, 0x01, 0x08);
    generic_write_seq(dsi, 0xC6, 0x48, 0x00);
    generic_write_seq(dsi, 0xE5, 0x00, 0x3C, 0x3C, 0x27, 0x8D, 0x09, 0x00, 0xC2,
                      0x01, 0x30, 0x05, 0x00, 0x00, 0x05, 0x00);
    generic_write_seq(dsi, 0xE6, 0x10, 0x0B, 0x4E, 0x00, 0x00);
    generic_write_seq(dsi, 0xE9, 0x10, 0xA0, 0xA0);
    generic_write_seq(dsi, 0xEC, 0x0C, 0x77, 0x0D);

    generic_write_seq(dsi, 0xDE, 0x03);
    generic_write_seq(dsi, 0xD1, 0x00, 0x00, 0x39, 0xFF, 0x08);

    generic_write_seq(dsi, 0xDE, 0x00);

    /* Enable driver IC internal PWM backlight control (DCS 0x53 & 0x51) */
    mipi_dsi_dcs_write(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY, (u8[]){0x2C}, 1);
    mipi_dsi_dcs_write(dsi, MIPI_DCS_SET_DISPLAY_BRIGHTNESS, (u8[]){0xFF}, 1);

    /* Standard DCS Sleep Out (0x11) & Display On (0x29) */
    ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
    if (ret < 0)
        return ret;

    msleep(120);

    ret = mipi_dsi_dcs_set_display_on(dsi);
    if (ret < 0)
        return ret;

    msleep(20);

    return 0;
}

static int z50006_prepare(struct drm_panel* panel) {
    struct z50006_panel* ctx = to_z50006_panel(panel);
    int ret;

    if (ctx->supply) {
        ret = regulator_enable(ctx->supply);
        if (ret < 0)
            return ret;
    }

    /* Hardware reset pulse on RP1 GPIO 46 (active-low) */
    gpiod_set_value_cansleep(ctx->reset_gpio, 0);
    msleep(10);
    gpiod_set_value_cansleep(ctx->reset_gpio, 1);
    msleep(20);
    gpiod_set_value_cansleep(ctx->reset_gpio, 0);
    msleep(120);

    ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

    ret = z50006_init_sequence(ctx);
    if (ret < 0) {
        dev_err(&ctx->dsi->dev, "Failed to initialize Z50006 panel: %d\n", ret);
        gpiod_set_value_cansleep(ctx->reset_gpio, 1);
        if (ctx->supply)
            regulator_disable(ctx->supply);
        return ret;
    }

    return 0;
}

static int z50006_unprepare(struct drm_panel* panel) {
    struct z50006_panel* ctx = to_z50006_panel(panel);

    mipi_dsi_dcs_set_display_off(ctx->dsi);
    msleep(20);
    mipi_dsi_dcs_enter_sleep_mode(ctx->dsi);
    msleep(120);

    gpiod_set_value_cansleep(ctx->reset_gpio, 1);

    if (ctx->supply)
        regulator_disable(ctx->supply);

    return 0;
}

static const struct drm_display_mode z50006_default_mode = {
    .clock = 41328, /* 800 * 861 * 60 / 1000 kHz */

    .hdisplay = 720,
    .hsync_start = 720 + 20,      /* HAdr + HFP = 740 */
    .hsync_end = 720 + 20 + 20,   /* HAdr + HFP + Hsync = 760 */
    .htotal = 720 + 20 + 20 + 40, /* 800 */

    .vdisplay = 720,
    .vsync_start = 720 + 120,     /* VAdr + VFP = 840 */
    .vsync_end = 720 + 120 + 2,   /* VAdr + VFP + Vsync = 842 */
    .vtotal = 720 + 120 + 2 + 19, /* 861 */

    .width_mm = 70,
    .height_mm = 70,
    .type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static int z50006_get_modes(struct drm_panel* panel,
                            struct drm_connector* connector) {
    struct drm_display_mode* mode;
    static const u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;

    mode = drm_mode_duplicate(connector->dev, &z50006_default_mode);
    if (!mode)
        return -ENOMEM;

    drm_mode_set_name(mode);
    drm_mode_probed_add(connector, mode);

    connector->display_info.width_mm = z50006_default_mode.width_mm;
    connector->display_info.height_mm = z50006_default_mode.height_mm;
    drm_display_info_set_bus_formats(&connector->display_info, &bus_format, 1);

    return 1;
}

static const struct drm_panel_funcs z50006_panel_funcs = {
    .prepare = z50006_prepare,
    .unprepare = z50006_unprepare,
    .get_modes = z50006_get_modes,
};

/* Linux sysfs / desktop slider brightness control over MIPI DCS */
static int z50006_bl_update_status(struct backlight_device* bl) {
    struct mipi_dsi_device* dsi = bl_get_data(bl);
    u16 brightness = backlight_get_brightness(bl);

    return mipi_dsi_dcs_set_display_brightness(dsi, brightness);
}

static const struct backlight_ops z50006_bl_ops = {
    .update_status = z50006_bl_update_status,
};

static int z50006_probe(struct mipi_dsi_device* dsi) {
    struct device* dev = &dsi->dev;
    struct z50006_panel* ctx;
    struct backlight_properties bl_props = {
        .type = BACKLIGHT_RAW,
        .brightness = 255,
        .max_brightness = 255,
    };
    int ret;

    ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
    if (!ctx)
        return -ENOMEM;

    ctx->supply = devm_regulator_get_optional(dev, "power");
    if (IS_ERR(ctx->supply)) {
        ret = PTR_ERR(ctx->supply);
        if (ret == -EPROBE_DEFER)
            return ret;
        ctx->supply = NULL;
    }

    ctx->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
    if (IS_ERR(ctx->reset_gpio))
        return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
                             "Failed to get reset GPIO\n");
    dsi->lanes = 2;
    dsi->format = MIPI_DSI_FMT_RGB888;
    dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
                      MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_NO_EOT_PACKET;
    ctx->dsi = dsi;
    mipi_dsi_set_drvdata(dsi, ctx);
    drm_panel_init(&ctx->panel, dev, &z50006_panel_funcs,
                   DRM_MODE_CONNECTOR_DSI);
    ctx->panel.backlight = devm_backlight_device_register(
        dev, dev_name(dev), dev, dsi, &z50006_bl_ops, &bl_props);
    if (IS_ERR(ctx->panel.backlight))
        return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
                             "Failed to register DCS backlight\n");
    drm_panel_add(&ctx->panel);
    ret = mipi_dsi_attach(dsi);
    if (ret < 0) {
        drm_panel_remove(&ctx->panel);
        return dev_err_probe(dev, ret, "Failed to attach DSI device\n");
    }
    return 0;
}

static void z50006_remove(struct mipi_dsi_device* dsi) {
    struct z50006_panel* ctx = mipi_dsi_get_drvdata(dsi);
    mipi_dsi_detach(dsi);
    drm_panel_remove(&ctx->panel);
}

static const struct of_device_id z50006_of_match[] = {
    {.compatible = "custom,z50006-720x720"},
    {/* sentinel */},
};

MODULE_DEVICE_TABLE(of, z50006_of_match);

static struct mipi_dsi_driver z50006_driver = {
    .probe = z50006_probe,
    .remove = z50006_remove,
    .driver =
        {
            .name = "panel-z50006",
            .of_match_table = z50006_of_match,
        },
};

module_mipi_dsi_driver(z50006_driver);

MODULE_DESCRIPTION("Z50006 720x720 2-Lane MIPI DSI Panel Driver");
MODULE_LICENSE("GPL");