// Display driver for Waveshare ESP32-P4 WiFi6 Touch LCD 4B
// Manual init sequence to handle GT911 touch failures gracefully
// v0.1.1

#include "display_driver.h"

#include "esp_log.h"
#include "esp_err.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "display";

esp_err_t display_driver_init(void)
{
    ESP_LOGI(TAG, "Initializing display...");

    // Step 1: Init LVGL port
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    // Step 2: Init backlight
    ESP_ERROR_CHECK(bsp_display_brightness_init());

    // Step 3: Init LCD panel via BSP (MIPI-DSI + ST7703)
    bsp_lcd_handles_t lcd_handles;
    ESP_ERROR_CHECK(bsp_display_new_with_handles(NULL, &lcd_handles));

    // Step 4: Register display with LVGL
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd_handles.io,
        .panel_handle = lcd_handles.panel,
        .control_handle = lcd_handles.control,
        .buffer_size = DISPLAY_H_RES * 50,
        .double_buffer = false,
        .hres = DISPLAY_H_RES,
        .vres = DISPLAY_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_spiram = true,
#if CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR
            .full_refresh = true,
#endif
        },
    };

    const lvgl_port_display_dsi_cfg_t dpi_cfg = {
        .flags = {
#if CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR
            .avoid_tearing = true,
#else
            .avoid_tearing = false,
#endif
        }
    };

    lv_display_t *disp = lvgl_port_add_disp_dsi(&disp_cfg, &dpi_cfg);
    if (!disp) {
        ESP_LOGE(TAG, "Failed to add display to LVGL");
        return ESP_FAIL;
    }

    // Step 5: Try to init touch controller (non-fatal if it fails)
    ESP_LOGI(TAG, "Initializing touch controller...");
    esp_err_t touch_err = bsp_i2c_init();
    if (touch_err == ESP_OK) {
        // Try default GT911 address (0x5D) first, then backup (0x14)
        esp_lcd_touch_handle_t tp = NULL;

        const esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_H_RES,
            .y_max = DISPLAY_V_RES,
            .rst_gpio_num = BSP_LCD_TOUCH_RST,
            .int_gpio_num = BSP_LCD_TOUCH_INT,
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = 0,
                .mirror_x = 0,
                .mirror_y = 0,
            },
        };

        // Try primary address (0x5D)
        esp_lcd_panel_io_handle_t tp_io = NULL;
        esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
        tp_io_cfg.scl_speed_hz = 400000;

        touch_err = esp_lcd_new_panel_io_i2c(bsp_i2c_get_handle(), &tp_io_cfg, &tp_io);
        if (touch_err == ESP_OK) {
            touch_err = esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &tp);
        }

        // If primary address failed, try backup address (0x14)
        if (touch_err != ESP_OK) {
            ESP_LOGW(TAG, "GT911 at 0x5D failed, trying backup address 0x14...");
            if (tp_io) {
                esp_lcd_panel_io_del(tp_io);
                tp_io = NULL;
            }
            tp_io_cfg.dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP;
            touch_err = esp_lcd_new_panel_io_i2c(bsp_i2c_get_handle(), &tp_io_cfg, &tp_io);
            if (touch_err == ESP_OK) {
                touch_err = esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, &tp);
            }
        }

        if (touch_err == ESP_OK && tp) {
            const lvgl_port_touch_cfg_t touch_cfg = {
                .disp = disp,
                .handle = tp,
            };
            lvgl_port_add_touch(&touch_cfg);
            ESP_LOGI(TAG, "Touch controller initialized");
        } else {
            ESP_LOGW(TAG, "Touch controller init failed (err=0x%x), continuing without touch", touch_err);
        }
    } else {
        ESP_LOGW(TAG, "I2C init failed, continuing without touch");
    }

    // Step 6: Turn on backlight
    bsp_display_backlight_on();
    bsp_display_brightness_set(100);

    ESP_LOGI(TAG, "Display initialized: %dx%d", DISPLAY_H_RES, DISPLAY_V_RES);
    return ESP_OK;
}
