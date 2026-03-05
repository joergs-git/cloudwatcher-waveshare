// Dashboard screen - displays current CloudWatcher sensor readings
// Layout: title bar + 3x2 sensor card grid + safe/unsafe banner
// v0.1.0

#include "ui_dashboard.h"
#include "cloudwatcher_client.h"

#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "ui_dash";

// Color definitions
#define COLOR_SAFE      lv_color_hex(0x00c853)
#define COLOR_WARNING   lv_color_hex(0xffab00)
#define COLOR_UNSAFE    lv_color_hex(0xff1744)
#define COLOR_CARD_BG   lv_color_hex(0x16213e)
#define COLOR_TEXT_DIM   lv_color_hex(0x8899aa)
#define COLOR_TEXT_BRIGHT lv_color_hex(0xe0e0e0)

// Widget references for updates
static lv_obj_t *lbl_title_time = NULL;
static lv_obj_t *lbl_safe_indicator = NULL;

// Sensor card references (value + status labels)
typedef struct {
    lv_obj_t *lbl_value;
    lv_obj_t *lbl_status;
    lv_obj_t *card;
} sensor_card_t;

static sensor_card_t card_clouds;
static sensor_card_t card_temp;
static sensor_card_t card_humidity;
static sensor_card_t card_rain;
static sensor_card_t card_light;
static sensor_card_t card_pressure;

// Safe/unsafe banner
static lv_obj_t *banner_safe = NULL;
static lv_obj_t *lbl_banner_text = NULL;

// Create a single sensor card widget
static sensor_card_t create_sensor_card(lv_obj_t *parent,
                                         const char *icon,
                                         const char *title,
                                         const char *initial_value,
                                         const char *initial_status)
{
    sensor_card_t sc = {0};

    sc.card = lv_obj_create(parent);
    lv_obj_set_size(sc.card, 215, 140);
    lv_obj_set_style_bg_color(sc.card, COLOR_CARD_BG, 0);
    lv_obj_set_style_border_width(sc.card, 1, 0);
    lv_obj_set_style_border_color(sc.card, lv_color_hex(0x1f3050), 0);
    lv_obj_set_style_radius(sc.card, 12, 0);
    lv_obj_set_style_pad_all(sc.card, 10, 0);
    lv_obj_set_flex_flow(sc.card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sc.card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(sc.card, LV_OBJ_FLAG_SCROLLABLE);

    // Title row (icon + name)
    lv_obj_t *title_row = lv_obj_create(sc.card);
    lv_obj_set_size(title_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(title_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(title_row, 0, 0);
    lv_obj_set_style_pad_all(title_row, 0, 0);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_icon = lv_label_create(title_row);
    lv_label_set_text(lbl_icon, icon);
    lv_obj_set_style_text_font(lbl_icon, &lv_font_montserrat_16, 0);

    lv_obj_t *lbl_title = lv_label_create(title_row);
    lv_label_set_text(lbl_title, title);
    lv_obj_set_style_text_color(lbl_title, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);

    // Value (large text)
    sc.lbl_value = lv_label_create(sc.card);
    lv_label_set_text(sc.lbl_value, initial_value);
    lv_obj_set_style_text_color(sc.lbl_value, COLOR_TEXT_BRIGHT, 0);
    lv_obj_set_style_text_font(sc.lbl_value, &lv_font_montserrat_28, 0);

    // Status badge
    sc.lbl_status = lv_label_create(sc.card);
    lv_label_set_text(sc.lbl_status, initial_status);
    lv_obj_set_style_text_font(sc.lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sc.lbl_status, COLOR_TEXT_DIM, 0);

    return sc;
}

// Set status label with appropriate color
static void set_status(sensor_card_t *sc, const char *text, bool safe)
{
    lv_label_set_text(sc->lbl_status, text);
    lv_obj_set_style_text_color(sc->lbl_status, safe ? COLOR_SAFE : COLOR_UNSAFE, 0);
    lv_obj_set_style_border_color(sc->card,
        safe ? lv_color_hex(0x1f3050) : lv_color_hex(0x4a1020), 0);
}

lv_obj_t *ui_dashboard_create(lv_obj_t *parent)
{
    // Title bar
    lv_obj_t *title_bar = lv_obj_create(parent);
    lv_obj_set_size(title_bar, LV_PCT(100), 44);
    lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x0f0f2a), 0);
    lv_obj_set_style_border_width(title_bar, 0, 0);
    lv_obj_set_style_radius(title_bar, 0, 0);
    lv_obj_set_style_pad_hor(title_bar, 15, 0);
    lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_title = lv_label_create(title_bar);
    lv_label_set_text(lbl_title, "CloudWatcher Solo");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_title, COLOR_TEXT_BRIGHT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 0, 0);

    lbl_title_time = lv_label_create(title_bar);
    lv_label_set_text(lbl_title_time, "--:--");
    lv_obj_set_style_text_color(lbl_title_time, COLOR_TEXT_DIM, 0);
    lv_obj_align(lbl_title_time, LV_ALIGN_CENTER, 0, 0);

    lbl_safe_indicator = lv_label_create(title_bar);
    lv_label_set_text(lbl_safe_indicator, "---");
    lv_obj_set_style_text_font(lbl_safe_indicator, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_safe_indicator, COLOR_TEXT_DIM, 0);
    lv_obj_align(lbl_safe_indicator, LV_ALIGN_RIGHT_MID, 0, 0);

    // Sensor cards grid container
    lv_obj_t *grid = lv_obj_create(parent);
    lv_obj_set_size(grid, 690, 440);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 5, 0);
    lv_obj_set_style_pad_gap(grid, 10, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

    // Create 6 sensor cards in 3x2 grid
    card_clouds   = create_sensor_card(grid, LV_SYMBOL_EYE_OPEN, " SKY TEMP",    "--.-  C",  "---");
    card_temp     = create_sensor_card(grid, LV_SYMBOL_CHARGE,   " AMBIENT",     "--.- C",   "---");
    card_humidity = create_sensor_card(grid, LV_SYMBOL_WARNING,  " HUMIDITY",    "--%",      "---");
    card_rain     = create_sensor_card(grid, LV_SYMBOL_DOWNLOAD, " RAIN",        "---",      "---");
    card_light    = create_sensor_card(grid, LV_SYMBOL_IMAGE,    " SKY QUAL",    "-- mpsas", "---");
    card_pressure = create_sensor_card(grid, LV_SYMBOL_SETTINGS, " PRESSURE",    "---- hPa", "---");

    // Safe/Unsafe banner
    banner_safe = lv_obj_create(parent);
    lv_obj_set_size(banner_safe, 690, 70);
    lv_obj_align(banner_safe, LV_ALIGN_TOP_MID, 0, 500);
    lv_obj_set_style_bg_color(banner_safe, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_radius(banner_safe, 12, 0);
    lv_obj_set_style_border_width(banner_safe, 2, 0);
    lv_obj_set_style_border_color(banner_safe, COLOR_TEXT_DIM, 0);
    lv_obj_clear_flag(banner_safe, LV_OBJ_FLAG_SCROLLABLE);

    lbl_banner_text = lv_label_create(banner_safe);
    lv_label_set_text(lbl_banner_text, "WAITING FOR DATA...");
    lv_obj_set_style_text_font(lbl_banner_text, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_banner_text, COLOR_TEXT_DIM, 0);
    lv_obj_center(lbl_banner_text);

    ESP_LOGI(TAG, "Dashboard screen created");
    return parent;
}

void ui_dashboard_update(const cw_current_data_t *data)
{
    if (!data || !data->valid) return;

    // LVGL's built-in printf doesn't support %f, so use C snprintf for floats
    char buf[32];

    // Update title bar timestamp
    if (lbl_title_time && data->timestamp[0]) {
        lv_label_set_text(lbl_title_time, data->timestamp);
    }

    // Sky temperature / clouds
    snprintf(buf, sizeof(buf), "%.1f C", data->clouds);
    lv_label_set_text(card_clouds.lbl_value, buf);
    set_status(&card_clouds, cw_cloud_state_str(data->clouds), data->clouds_safe);

    // Ambient temperature + dew point info
    snprintf(buf, sizeof(buf), "%.1f C", data->temp);
    lv_label_set_text(card_temp.lbl_value, buf);
    snprintf(buf, sizeof(buf), "Dew: %.1f C", data->dew_point);
    lv_label_set_text(card_temp.lbl_status, buf);
    lv_obj_set_style_text_color(card_temp.lbl_status, COLOR_TEXT_DIM, 0);

    // Humidity
    lv_label_set_text_fmt(card_humidity.lbl_value, "%d%%", data->humidity);
    set_status(&card_humidity, data->hum_safe ? "OK" : "HIGH", data->hum_safe);

    // Rain
    lv_label_set_text(card_rain.lbl_value, cw_rain_state_str(data->rain));
    set_status(&card_rain, data->rain_safe ? "SAFE" : "UNSAFE", data->rain_safe);

    // Sky quality
    snprintf(buf, sizeof(buf), "%.2f", data->light_mpsas);
    lv_label_set_text(card_light.lbl_value, buf);
    const char *light_str = cw_light_state_str(data->light_mpsas);
    set_status(&card_light, light_str, data->light_safe);

    // Pressure
    snprintf(buf, sizeof(buf), "%.1f", data->rel_pressure);
    lv_label_set_text(card_pressure.lbl_value, buf);
    set_status(&card_pressure, "hPa", data->pressure_safe);

    // Safe indicator in title bar
    if (lbl_safe_indicator) {
        lv_label_set_text(lbl_safe_indicator, data->safe ? "SAFE" : "UNSAFE");
        lv_obj_set_style_text_color(lbl_safe_indicator,
            data->safe ? COLOR_SAFE : COLOR_UNSAFE, 0);
    }

    // Large banner
    if (data->safe) {
        lv_label_set_text(lbl_banner_text, "SAFE FOR OBSERVING");
        lv_obj_set_style_text_color(lbl_banner_text, COLOR_SAFE, 0);
        lv_obj_set_style_border_color(banner_safe, COLOR_SAFE, 0);
        lv_obj_set_style_bg_color(banner_safe, lv_color_hex(0x0a2a1a), 0);
    } else {
        lv_label_set_text(lbl_banner_text, "UNSAFE FOR OBSERVING");
        lv_obj_set_style_text_color(lbl_banner_text, COLOR_UNSAFE, 0);
        lv_obj_set_style_border_color(banner_safe, COLOR_UNSAFE, 0);
        lv_obj_set_style_bg_color(banner_safe, lv_color_hex(0x2a0a0a), 0);
    }
}
