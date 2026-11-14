#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

struct jadard_panel {
    struct drm_panel panel;
    struct mipi_dsi_device *dsi;
    struct gpio_desc *reset_gpio;
    struct regulator *supply;
};

static inline struct jadard_panel *to_jadard_panel(struct drm_panel *panel) {
    return container_of(panel, struct jadard_panel, panel);
}

/* 1. JD9365 Command Sequencing (Page Switching Example) */
static void jd9365da_switch_page(struct mipi_dsi_multi_context *ctx, u8 page) {
    // JD9365 requires unlocking configuration registers by writing to specific pages
    mipi_dsi_dcs_write_seq_multi(ctx, 0xE0, page); 
}

static int jd9365_init_cmds(struct jadard_panel *jpanel) {
    struct mipi_dsi_multi_context ctx = { .dsi = jpanel->dsi };

    /* Switch to page 0 and enable standard commands */
    jd9365da_switch_page(&ctx, 0x00);
    
    /* Switch to page 1 and start push display/power settings (modify using your panel's datasheet) */
    jd9365da_switch_page(&ctx, 0x01);
    mipi_dsi_dcs_write_seq_multi(&ctx, 0x00, 0x00);
    mipi_dsi_dcs_write_seq_multi(&ctx, 0x01, 0x7E);
    
    /* Exit Sleep and Turn Display On */
    jd9365da_switch_page(&ctx, 0x00);
    mipi_dsi_dcs_exit_sleep_mode_multi(&ctx);
    msleep(120); // Required delay after exiting sleep
    mipi_dsi_dcs_set_display_on_multi(&ctx);

    return ctx.accum_err;
}

/* 2. DRM Panel Callbacks */
static int jd9365_prepare(struct drm_panel *panel) {
    struct jadard_panel *jpanel = to_jadard_panel(panel);
    int ret;

    /* Power sequence hardware pins */
    regulator_enable(jpanel->supply);
    gpiod_set_value_cansleep(jpanel->reset_gpio, 1);
    msleep(10);
    gpiod_set_value_cansleep(jpanel->reset_gpio, 0);
    msleep(120);

    /* Send the initialization bytes over the MIPI DSI lane */
    ret = jd9365_init_cmds(jpanel);
    if (ret) {
        dev_err(&jpanel->dsi->dev, "Failed to send init sequences: %d\n", ret);
        return ret;
    }
    return 0;
}

static int jd9365_unprepare(struct drm_panel *panel) {
    struct jadard_panel *jpanel = to_jadard_panel(panel);
    struct mipi_dsi_multi_context ctx = { .dsi = jpanel->dsi };

    mipi_dsi_dcs_set_display_off_multi(&ctx);
    mipi_dsi_dcs_enter_sleep_mode_multi(&ctx);
    
    gpiod_set_value_cansleep(jpanel->reset_gpio, 1);
    regulator_disable(jpanel->supply);
    return 0;
}

/* Define display timing attributes matching your panel spec sheet */
static const struct drm_display_mode default_mode = {
    .clock = 72000, // kHz
    .hdisplay = 800,
    .hsync_start = 800 + 40,
    .hsync_end = 800 + 40 + 20,
    .htotal = 800 + 40 + 20 + 40,
    .vdisplay = 1280,
    .vsync_start = 1280 + 16,
    .vsync_end = 1280 + 16 + 4,
    .vtotal = 1280 + 16 + 4 + 14,
    .width_mm = 107,
    .height_mm = 172,
    .type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static int jd9365_get_modes(struct drm_panel *panel, struct drm_connector *connector) {
    struct drm_display_mode *mode;

    mode = drm_mode_duplicate(connector->dev, &default_mode);
    if (!mode)
        return -ENOMEM;

    drm_mode_set_name(mode);
    drm_connector_list_update_types(connector);
    drm_mode_probed_add(connector, mode);

    return 1;
}

static const struct drm_panel_funcs jd9365_funcs = {
    .prepare = jd9365_prepare,
    .unprepare = jd9365_unprepare,
    .get_modes = jd9365_get_modes,
};

/* 3. Probe and DSI Binding */
static int jd9365_probe(struct mipi_dsi_device *dsi) {
    struct jadard_panel *jpanel;
    int ret;

    jpanel = devm_kzalloc(&dsi->dev, sizeof(*jpanel), GFP_KERNEL);
    if (!jpanel)
        return -ENOMEM;

    mipi_dsi_set_drvdata(dsi, jpanel);
    jpanel->dsi = dsi;

    /* Get hardware line hooks from Device Tree */
    jpanel->reset_gpio = devm_gpiod_get(&dsi->dev, "reset", GPIOD_OUT_HIGH);
    if (IS_ERR(jpanel->reset_gpio))
        return PTR_ERR(jpanel->reset_gpio);

    jpanel->supply = devm_regulator_get(&dsi->dev, "power");
    if (IS_ERR(jpanel->supply))
        return PTR_ERR(jpanel->supply);

    /* Enforce 4-lane format defaults for typical JD9365 800x1280 targets */
    dsi->lanes = 4;
    dsi->format = MIPI_DSI_FMT_RGB888;
    dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_MODE_LPM;

    drm_panel_init(&jpanel->panel, &dsi->dev, &jd9365_funcs, DRM_MODE_CONNECTOR_DSI);
    
    ret = drm_panel_add(&jpanel->panel);
    if (ret)
        return ret;

    ret = mipi_dsi_attach(dsi);
    if (ret) {
        drm_panel_remove(&jpanel->panel);
        return ret;
    }

    return 0;
}

static void jd9365_remove(struct mipi_dsi_device *dsi) {
    struct jadard_panel *jpanel = mipi_dsi_get_drvdata(dsi);

    mipi_dsi_detach(dsi);
    drm_panel_remove(&jpanel->panel);
}

static const struct of_device_id jd9365_of_match[] = {
    { .compatible = "custom,jd9365-panel" },
};
MODULE_DEVICE_TABLE(of, jd9365_of_match);

static struct mipi_dsi_driver jd9365_driver = {
    .probe = jd9365_probe,
    .remove = jd9365_remove,
    .driver = {
        .name = "my_mipi_panel",
        .of_match_table = jd9365_of_match,
    },
};
module_mipi_dsi_driver(jd9365_driver);

MODULE_AUTHOR("ShiCheng-Lu");
MODULE_DESCRIPTION("Custom DRM Panel Driver for Jadard JD9365TX");
MODULE_LICENSE("GPL");