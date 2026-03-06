// NINA image display screen - shows latest captured frame with metadata overlay
// Displays the most recent sub-exposure from N.I.N.A. with target info,
// exposure parameters, and image statistics as a semi-transparent overlay
// v0.4.4

#include "ui_nina.h"
#include "ui_main.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "ui_nina";

// Display constraints
#define SCREEN_WIDTH    720
#define USABLE_HEIGHT   670   // 720 - 50px nav bar

// Color definitions
#define COLOR_OVERLAY_BG    lv_color_hex(0x000000)
#define COLOR_TEXT_WHITE     lv_color_hex(0xffffff)
#define COLOR_TEXT_LIGHT     lv_color_hex(0xcccccc)
#define COLOR_TARGET         lv_color_hex(0x00e5ff)

// Canvas for image display
static lv_obj_t *canvas = NULL;

// Persistent buffer for cropped/centered image (allocated from PSRAM)
static uint8_t *crop_buf = NULL;
#define CROP_BUF_SIZE  (SCREEN_WIDTH * USABLE_HEIGHT * 2) // ~965KB RGB565

// Status label (shown when no image available)
static lv_obj_t *status_label = NULL;

// Overlay: semi-transparent bar at bottom of image with metadata
static lv_obj_t *overlay = NULL;
static lv_obj_t *lbl_target = NULL;
static lv_obj_t *lbl_params = NULL;
static lv_obj_t *lbl_stats = NULL;

// Refresh button
static lv_obj_t *btn_refresh = NULL;

// Refresh button callback
static void refresh_btn_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Manual refresh requested");
    lv_label_set_text(status_label, "Refreshing...");
    lv_obj_clear_flag(status_label, LV_OBJ_FLAG_HIDDEN);
    nina_request_refresh();
}

lv_obj_t *ui_nina_create(lv_obj_t *parent)
{
    // Remove any theme padding so image is truly centered
    lv_obj_set_style_pad_all(parent, 0, 0);

    // Allocate crop buffer from PSRAM
    crop_buf = heap_caps_malloc(CROP_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!crop_buf) {
        ESP_LOGE(TAG, "Failed to allocate crop buffer");
    }

    // Status label - centered, shown when no image is available
    status_label = lv_label_create(parent);
    lv_label_set_text(status_label, "NINA: Waiting for data...");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(status_label, COLOR_TEXT_WHITE, 0);
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, -25);

    // Canvas for displaying the cropped image
    canvas = lv_canvas_create(parent);
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Semi-transparent overlay bar at bottom of image
    overlay = lv_obj_create(parent);
    lv_obj_set_size(overlay, SCREEN_WIDTH, 90);
    lv_obj_set_style_bg_color(overlay, COLOR_OVERLAY_BG, 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_80, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_EVENT_BUBBLE);

    // Line 1: Target name (large, bright accent color)
    lbl_target = lv_label_create(overlay);
    lv_label_set_text(lbl_target, "---");
    lv_obj_set_style_text_font(lbl_target, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_target, COLOR_TARGET, 0);
    lv_obj_set_pos(lbl_target, 14, 6);

    // Line 2: Exposure parameters
    lbl_params = lv_label_create(overlay);
    lv_label_set_text(lbl_params, "---");
    lv_obj_set_style_text_font(lbl_params, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_params, COLOR_TEXT_WHITE, 0);
    lv_obj_set_pos(lbl_params, 14, 36);

    // Line 3: Image statistics
    lbl_stats = lv_label_create(overlay);
    lv_label_set_text(lbl_stats, "---");
    lv_obj_set_style_text_font(lbl_stats, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_stats, COLOR_TEXT_LIGHT, 0);
    lv_obj_set_pos(lbl_stats, 14, 60);

    // Refresh button - top right corner
    btn_refresh = lv_btn_create(parent);
    lv_obj_set_size(btn_refresh, 50, 50);
    lv_obj_align(btn_refresh, LV_ALIGN_TOP_RIGHT, -8, 4);
    lv_obj_set_style_bg_color(btn_refresh, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_bg_opa(btn_refresh, LV_OPA_90, 0);
    lv_obj_set_style_radius(btn_refresh, 25, 0);
    lv_obj_t *lbl_ref = lv_label_create(btn_refresh);
    lv_label_set_text(lbl_ref, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(lbl_ref, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_ref);
    lv_obj_add_event_cb(btn_refresh, refresh_btn_cb, LV_EVENT_CLICKED, NULL);

    ESP_LOGI(TAG, "NINA screen created (crop buf: %dKB)", CROP_BUF_SIZE / 1024);
    return parent;
}

// Crop the center portion of the decoded image to fit on screen
// Copies from src (src_w x src_h) into crop_buf (dst_w x dst_h)
static void crop_center(const uint8_t *src, uint32_t src_w, uint32_t src_h,
                         uint32_t *dst_w, uint32_t *dst_h)
{
    // Determine crop dimensions (fit within screen)
    *dst_w = (src_w > SCREEN_WIDTH) ? SCREEN_WIDTH : src_w;
    *dst_h = (src_h > USABLE_HEIGHT) ? USABLE_HEIGHT : src_h;

    // Calculate source offset to center the crop
    uint32_t x_off = (src_w - *dst_w) / 2;
    uint32_t y_off = (src_h - *dst_h) / 2;

    // Copy row by row (RGB565 = 2 bytes per pixel)
    uint32_t src_stride = src_w * 2;
    uint32_t dst_stride = *dst_w * 2;

    for (uint32_t y = 0; y < *dst_h; y++) {
        const uint8_t *src_row = src + (y + y_off) * src_stride + x_off * 2;
        uint8_t *dst_row = crop_buf + y * dst_stride;
        memcpy(dst_row, src_row, dst_stride);
    }
}

void ui_nina_update(const nina_image_data_t *data)
{
    if (!data || !crop_buf) return;

    // Update canvas if we have a valid decoded image
    if (data->image_valid && data->rgb_buf && data->width > 0 && data->height > 0) {
        // Crop center portion to fit on screen
        uint32_t crop_w, crop_h;
        crop_center(data->rgb_buf, data->width, data->height, &crop_w, &crop_h);

        lv_canvas_set_buffer(canvas, crop_buf, crop_w, crop_h, LV_COLOR_FORMAT_RGB565);

        // Center on screen using explicit pixel position
        int x_pos = ((int)SCREEN_WIDTH - (int)crop_w) / 2;
        int y_pos = ((int)USABLE_HEIGHT - (int)crop_h) / 2;
        if (x_pos < 0) x_pos = 0;
        if (y_pos < 0) y_pos = 0;
        lv_obj_set_pos(canvas, x_pos, y_pos);
        lv_obj_invalidate(canvas);

        lv_obj_clear_flag(canvas, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);

        // Position overlay at bottom of image
        int overlay_y = y_pos + (int)crop_h - 90;
        if (overlay_y > (int)USABLE_HEIGHT - 90) overlay_y = USABLE_HEIGHT - 90;
        lv_obj_set_pos(overlay, 0, overlay_y);
        lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);

        // Ensure overlay and button render on top
        lv_obj_move_foreground(overlay);
        lv_obj_move_foreground(btn_refresh);

        ESP_LOGI(TAG, "Display: %lux%lu cropped to %lux%lu (centered)",
                 (unsigned long)data->width, (unsigned long)data->height,
                 (unsigned long)crop_w, (unsigned long)crop_h);
    }

    // Update overlay text
    char buf[256];

    if (data->meta.valid) {
        lv_label_set_text(lbl_target,
            data->meta.target_name[0] ? data->meta.target_name : "Unknown Target");

        if (data->meta.exposure_time >= 1.0f) {
            snprintf(buf, sizeof(buf), "%s  |  %.0fs  |  Gain %d  |  Offset %d",
                     data->meta.filter[0] ? data->meta.filter : "-",
                     data->meta.exposure_time,
                     data->meta.gain,
                     data->meta.offset);
        } else {
            snprintf(buf, sizeof(buf), "%s  |  %.2fs  |  Gain %d  |  Offset %d",
                     data->meta.filter[0] ? data->meta.filter : "-",
                     data->meta.exposure_time,
                     data->meta.gain,
                     data->meta.offset);
        }
        lv_label_set_text(lbl_params, buf);

        snprintf(buf, sizeof(buf), "Stars: %d  |  HFR: %.2f  |  Subs: %d  |  %s  |  %s",
                 data->meta.stars,
                 data->meta.hfr,
                 data->meta.image_count,
                 data->meta.telescope_name[0] ? data->meta.telescope_name : "",
                 data->meta.timestamp[0] ? data->meta.timestamp : "");
        lv_label_set_text(lbl_stats, buf);
    } else {
        lv_label_set_text(lbl_target, "NINA Image");
        snprintf(buf, sizeof(buf), "Subs: %d  |  Metadata unavailable", data->meta.image_count);
        lv_label_set_text(lbl_params, buf);
        lv_label_set_text(lbl_stats, "");
    }
}

void ui_nina_set_status(const char *message)
{
    if (!message) return;

    lv_label_set_text(status_label, message);
    lv_obj_clear_flag(status_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
}
