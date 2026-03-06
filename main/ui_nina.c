// NINA image display screen - shows latest captured frame with metadata overlay
// Displays the most recent sub-exposure from N.I.N.A. with target info,
// exposure parameters, and image statistics as a semi-transparent overlay
// v0.4.0

#include "ui_nina.h"
#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "ui_nina";

// Color definitions
#define COLOR_OVERLAY_BG    lv_color_hex(0x000000)
#define COLOR_TEXT_BRIGHT    lv_color_hex(0xe0e0e0)
#define COLOR_TEXT_DIM       lv_color_hex(0x8899aa)
#define COLOR_TARGET         lv_color_hex(0x00b4d8)

// Canvas for image display
static lv_obj_t *canvas = NULL;

// Status label (shown when no image available)
static lv_obj_t *status_label = NULL;

// Overlay: semi-transparent bar at bottom of image with metadata
static lv_obj_t *overlay = NULL;
static lv_obj_t *lbl_target = NULL;
static lv_obj_t *lbl_params = NULL;
static lv_obj_t *lbl_stats = NULL;

lv_obj_t *ui_nina_create(lv_obj_t *parent)
{
    // Status label - centered, shown when no image is available
    status_label = lv_label_create(parent);
    lv_label_set_text(status_label, "NINA: Waiting for data...");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(status_label, COLOR_TEXT_DIM, 0);
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, -25);

    // Canvas for displaying the decoded image (hidden until first image arrives)
    canvas = lv_canvas_create(parent);
    lv_obj_align(canvas, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);

    // Semi-transparent overlay bar at bottom of image area
    overlay = lv_obj_create(parent);
    lv_obj_set_size(overlay, NINA_IMAGE_WIDTH, 80);
    lv_obj_set_style_bg_color(overlay, COLOR_OVERLAY_BG, 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_pad_left(overlay, 12, 0);
    lv_obj_set_style_pad_right(overlay, 12, 0);
    lv_obj_set_style_pad_top(overlay, 6, 0);
    lv_obj_set_style_pad_bottom(overlay, 4, 0);
    lv_obj_set_flex_flow(overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(overlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);

    // Line 1: Target name (prominent, accent color)
    lbl_target = lv_label_create(overlay);
    lv_label_set_text(lbl_target, "");
    lv_obj_set_style_text_font(lbl_target, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_target, COLOR_TARGET, 0);

    // Line 2: Exposure parameters (filter, time, gain)
    lbl_params = lv_label_create(overlay);
    lv_label_set_text(lbl_params, "");
    lv_obj_set_style_text_font(lbl_params, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_params, COLOR_TEXT_BRIGHT, 0);

    // Line 3: Image statistics (stars, HFR, count, camera)
    lbl_stats = lv_label_create(overlay);
    lv_label_set_text(lbl_stats, "");
    lv_obj_set_style_text_font(lbl_stats, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_stats, COLOR_TEXT_DIM, 0);

    ESP_LOGI(TAG, "NINA screen created");
    return parent;
}

void ui_nina_update(const nina_image_data_t *data)
{
    if (!data) return;

    // Update canvas if we have a valid decoded image
    if (data->image_valid && data->rgb_buf && data->width > 0 && data->height > 0) {
        lv_canvas_set_buffer(canvas, data->rgb_buf,
                             data->width, data->height, LV_COLOR_FORMAT_RGB565);

        // Center image vertically in the available area (720 - 50px nav bar = 670px)
        int y_offset = (670 - (int)data->height) / 2;
        if (y_offset < 0) y_offset = 0;
        lv_obj_align(canvas, LV_ALIGN_TOP_MID, 0, y_offset);

        // Force redraw even though buffer pointer hasn't changed
        lv_obj_invalidate(canvas);

        lv_obj_clear_flag(canvas, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);

        // Position overlay at bottom of image
        int overlay_y = y_offset + (int)data->height - 80;
        lv_obj_set_size(overlay, data->width, 80);
        lv_obj_align(overlay, LV_ALIGN_TOP_MID, 0, overlay_y);
        lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    }

    // Update metadata overlay labels
    if (data->meta.valid) {
        char buf[256];

        // Line 1: Target name
        lv_label_set_text(lbl_target,
            data->meta.target_name[0] ? data->meta.target_name : "Unknown Target");

        // Line 2: Filter | Exposure | Gain
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

        // Line 3: Stars | HFR | Image count | Camera
        snprintf(buf, sizeof(buf), "Stars: %d  |  HFR: %.2f  |  Subs: %d  |  %s",
                 data->meta.stars,
                 data->meta.hfr,
                 data->meta.image_count,
                 data->meta.telescope_name[0] ? data->meta.telescope_name : "");
        lv_label_set_text(lbl_stats, buf);
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
