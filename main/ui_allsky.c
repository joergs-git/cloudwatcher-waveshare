// Allsky screen - fetches and displays indi-allsky keogram image
// Decodes JPEG from HTTPS endpoint and renders via LVGL image widget
// v0.4.0

#include "ui_allsky.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "jpeg_decoder.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

static const char *TAG = "ui_allsky";

// Max JPEG download size (500KB should be plenty for a keogram)
#define MAX_JPEG_SIZE (500 * 1024)

// Max decoded image size (720px wide * 500px tall * 2 bytes RGB565)
#define MAX_DECODED_SIZE (720 * 500 * 2)

// LVGL image widget and descriptor
static lv_obj_t *img_widget = NULL;
static lv_obj_t *lbl_status = NULL;
static lv_obj_t *lbl_title = NULL;

// Persistent pixel buffer and image descriptor in PSRAM
static lv_image_dsc_t img_dsc = {0};
static uint8_t *decoded_buf = NULL;

lv_obj_t *ui_allsky_create(lv_obj_t *parent)
{
    // Title
    lbl_title = lv_label_create(parent);
    lv_label_set_text(lbl_title, "Allsky Keogram");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xe0e0e0), 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 10);

    // Status label (shown while loading or on error)
    lbl_status = lv_label_create(parent);
    lv_label_set_text(lbl_status, "Waiting for image...");
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x8899aa), 0);
    lv_obj_align(lbl_status, LV_ALIGN_CENTER, 0, 0);

    // Image widget (centered, hidden until first successful fetch)
    img_widget = lv_image_create(parent);
    lv_obj_align(img_widget, LV_ALIGN_CENTER, 0, 10);
    lv_obj_add_flag(img_widget, LV_OBJ_FLAG_HIDDEN);

    // Pre-allocate decoded pixel buffer in PSRAM
    decoded_buf = heap_caps_malloc(MAX_DECODED_SIZE, MALLOC_CAP_SPIRAM);
    if (!decoded_buf) {
        ESP_LOGE(TAG, "Failed to allocate decoded image buffer in PSRAM");
    }

    ESP_LOGI(TAG, "Allsky screen created");
    return parent;
}

esp_err_t ui_allsky_fetch_and_update(void)
{
    if (!img_widget || !decoded_buf) return ESP_ERR_INVALID_STATE;

    const char *url = CONFIG_CW_ALLSKY_KEOGRAM_URL;
    ESP_LOGI(TAG, "Fetching keogram from: %s", url);

    // Show loading status (need LVGL lock)
    if (bsp_display_lock(100)) {
        lv_label_set_text(lbl_status, "Loading image...");
        lv_obj_clear_flag(lbl_status, LV_OBJ_FLAG_HIDDEN);
        bsp_display_unlock();
    }

    // Allocate JPEG download buffer in PSRAM
    uint8_t *jpeg_buf = heap_caps_malloc(MAX_JPEG_SIZE, MALLOC_CAP_SPIRAM);
    if (!jpeg_buf) {
        ESP_LOGE(TAG, "Failed to allocate JPEG buffer");
        return ESP_ERR_NO_MEM;
    }

    // Fetch image via HTTPS (no LVGL lock held during network I/O)
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 15000,
        .buffer_size = 4096,
        .skip_cert_common_name_check = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        heap_caps_free(jpeg_buf);
        if (bsp_display_lock(100)) {
            lv_label_set_text(lbl_status, "Connection failed");
            bsp_display_unlock();
        }
        return err;
    }

    esp_http_client_fetch_headers(client);
    int total_read = 0;

    while (total_read < MAX_JPEG_SIZE) {
        int read_len = esp_http_client_read(client, (char *)jpeg_buf + total_read,
                                             MAX_JPEG_SIZE - total_read);
        if (read_len <= 0) break;
        total_read += read_len;
    }

    int status = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (status != 200 || total_read < 100) {
        ESP_LOGE(TAG, "HTTP status %d, read %d bytes", status, total_read);
        heap_caps_free(jpeg_buf);
        if (bsp_display_lock(100)) {
            char msg[64];
            snprintf(msg, sizeof(msg), "HTTP error %d", status);
            lv_label_set_text(lbl_status, msg);
            bsp_display_unlock();
        }
        return ESP_FAIL;
    }

    // Verify JPEG magic bytes (FFD8)
    if (jpeg_buf[0] != 0xFF || jpeg_buf[1] != 0xD8) {
        ESP_LOGE(TAG, "Response is not a JPEG (got 0x%02x%02x). URL may point to HTML page.",
                 jpeg_buf[0], jpeg_buf[1]);
        heap_caps_free(jpeg_buf);
        if (bsp_display_lock(100)) {
            lv_label_set_text(lbl_status, "Not a JPEG - check URL");
            bsp_display_unlock();
        }
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Downloaded %d bytes JPEG", total_read);

    // Decode JPEG to RGB565 (no LVGL lock needed for decode)
    esp_jpeg_image_cfg_t jpeg_cfg = {
        .indata = jpeg_buf,
        .indata_size = total_read,
        .outbuf = decoded_buf,
        .outbuf_size = MAX_DECODED_SIZE,
        .out_format = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale = JPEG_IMAGE_SCALE_0,
    };

    esp_jpeg_image_output_t output = {0};
    err = esp_jpeg_decode(&jpeg_cfg, &output);

    if (err != ESP_OK) {
        // Try with 1/2 scale if image too large for buffer
        ESP_LOGW(TAG, "Full-scale decode failed, trying 1/2 scale...");
        jpeg_cfg.out_scale = JPEG_IMAGE_SCALE_1_2;
        err = esp_jpeg_decode(&jpeg_cfg, &output);
    }

    heap_caps_free(jpeg_buf);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "JPEG decode failed: %s", esp_err_to_name(err));
        if (bsp_display_lock(100)) {
            lv_label_set_text(lbl_status, "Decode failed");
            bsp_display_unlock();
        }
        return err;
    }

    ESP_LOGI(TAG, "Decoded image: %lux%lu", (unsigned long)output.width, (unsigned long)output.height);

    // Update LVGL image (need lock for UI operations)
    if (bsp_display_lock(500)) {
        img_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        img_dsc.header.w = output.width;
        img_dsc.header.h = output.height;
        img_dsc.data = decoded_buf;
        img_dsc.data_size = output.width * output.height * 2;

        lv_image_set_src(img_widget, &img_dsc);

        // Scale to fit screen width if needed (max 700px, leave margins)
        if (output.width > 700) {
            uint32_t scale = (700 * 256) / output.width;
            lv_image_set_scale(img_widget, scale);
        } else {
            lv_image_set_scale(img_widget, 256);
        }

        lv_obj_clear_flag(img_widget, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_status, LV_OBJ_FLAG_HIDDEN);

        char title[64];
        snprintf(title, sizeof(title), "Allsky Keogram (%lux%lu)",
                 (unsigned long)output.width, (unsigned long)output.height);
        lv_label_set_text(lbl_title, title);

        bsp_display_unlock();
    }

    return ESP_OK;
}
