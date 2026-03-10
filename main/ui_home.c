// Home screen - primary overview with big status text and sky/ambient temp chart
// v0.4.3

#include "ui_home.h"
#include "cloudwatcher_client.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <float.h>
#include <time.h>
#include "esp_log.h"

// Custom 80px Montserrat font for clock overlay (digits + colon only)
extern const lv_font_t font_montserrat_80;

static const char *TAG = "ui_home";

// Colors
#define COLOR_GREEN     lv_color_hex(0x00c853)
#define COLOR_RED       lv_color_hex(0xff4444)
#define COLOR_YELLOW    lv_color_hex(0xffab00)
#define COLOR_BLUE      lv_color_hex(0x42a5f5)
#define COLOR_TEXT_DIM  lv_color_hex(0x8899aa)
#define COLOR_TEXT_BRIGHT lv_color_hex(0xe0e0e0)
#define COLOR_SKY_LINE  lv_color_hex(0x00e676)  // green for sky temp
#define COLOR_AMB_LINE  lv_color_hex(0xff5252)  // red for ambient temp

// Big status labels
static lv_obj_t *lbl_cloud_state = NULL;
static lv_obj_t *lbl_rain_state = NULL;
static lv_obj_t *lbl_safe_banner = NULL;

// Dome status banner (styled box like dashboard safe/unsafe)
static lv_obj_t *dome_banner = NULL;
static lv_obj_t *lbl_dome = NULL;

// Time overlay on home chart
static lv_obj_t *lbl_time_overlay = NULL;

// Value labels below the big status
static lv_obj_t *lbl_sky_temp = NULL;
static lv_obj_t *lbl_amb_temp = NULL;
static lv_obj_t *lbl_dew_point = NULL;

// Chart and series
static lv_obj_t          *chart = NULL;
static lv_chart_series_t *ser_sky = NULL;
static lv_chart_series_t *ser_amb = NULL;

// Y-axis labels along left side of chart
#define Y_AXIS_LABELS 7
static lv_obj_t *y_axis_lbls[Y_AXIS_LABELS] = {0};

#define CHART_POINTS 200
#define CHART_X_OFFSET 50   // left margin for Y-axis labels
#define CHART_WIDTH    610
#define CHART_HEIGHT   430
#define CHART_Y_TOP    85

// Downsample a data array into the chart series
static void fill_series(lv_chart_series_t *ser, const float *data, int count)
{
    if (count <= 0) {
        for (int i = 0; i < CHART_POINTS; i++) {
            lv_chart_set_value_by_id(chart, ser, i, LV_CHART_POINT_NONE);
        }
        return;
    }

    for (int i = 0; i < CHART_POINTS; i++) {
        int start = (i * count) / CHART_POINTS;
        int end = ((i + 1) * count) / CHART_POINTS;
        if (end <= start) end = start + 1;
        if (end > count) end = count;

        float sum = 0;
        int n = 0;
        for (int j = start; j < end; j++) {
            if (isfinite(data[j])) {
                sum += data[j];
                n++;
            }
        }

        if (n > 0) {
            lv_chart_set_value_by_id(chart, ser, i, (int32_t)(sum / n * 10.0f));
        } else {
            lv_chart_set_value_by_id(chart, ser, i, LV_CHART_POINT_NONE);
        }
    }
}

// Get color for cloud state
static lv_color_t cloud_state_color(float sky_temp)
{
    if (sky_temp <= CW_CLOUD_CLEAR) return COLOR_GREEN;
    if (sky_temp <= CW_CLOUD_CLOUDY) return COLOR_YELLOW;
    return COLOR_RED;
}

// Get color for rain state
static lv_color_t rain_state_color(int rain_val)
{
    if (rain_val > CW_RAIN_DRY) return COLOR_GREEN;
    if (rain_val > CW_RAIN_WET) return COLOR_YELLOW;
    return COLOR_RED;
}

lv_obj_t *ui_home_create(lv_obj_t *parent)
{
    // --- Top section: big status words ---

    // Cloud state (e.g., "CLEAR", "CLOUDY", "OVERCAST")
    lbl_cloud_state = lv_label_create(parent);
    lv_label_set_text(lbl_cloud_state, "---");
    lv_obj_set_style_text_font(lbl_cloud_state, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_cloud_state, COLOR_TEXT_DIM, 0);
    lv_obj_align(lbl_cloud_state, LV_ALIGN_TOP_LEFT, 40, 15);

    // Rain state (e.g., "DRY", "WET", "RAIN")
    lbl_rain_state = lv_label_create(parent);
    lv_label_set_text(lbl_rain_state, "---");
    lv_obj_set_style_text_font(lbl_rain_state, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_rain_state, COLOR_TEXT_DIM, 0);
    lv_obj_align(lbl_rain_state, LV_ALIGN_TOP_RIGHT, -40, 15);

    // Safe/Unsafe indicator between the two
    lbl_safe_banner = lv_label_create(parent);
    lv_label_set_text(lbl_safe_banner, "---");
    lv_obj_set_style_text_font(lbl_safe_banner, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_safe_banner, COLOR_TEXT_DIM, 0);
    lv_obj_align(lbl_safe_banner, LV_ALIGN_TOP_MID, 0, 18);

    // --- Temperature values row ---

    // Sky temperature
    lbl_sky_temp = lv_label_create(parent);
    lv_label_set_text(lbl_sky_temp, "Sky: --.- C");
    lv_obj_set_style_text_font(lbl_sky_temp, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_sky_temp, COLOR_SKY_LINE, 0);
    lv_obj_align(lbl_sky_temp, LV_ALIGN_TOP_LEFT, 40, 55);

    // Ambient temperature
    lbl_amb_temp = lv_label_create(parent);
    lv_label_set_text(lbl_amb_temp, "Amb: --.- C");
    lv_obj_set_style_text_font(lbl_amb_temp, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_amb_temp, COLOR_AMB_LINE, 0);
    lv_obj_align(lbl_amb_temp, LV_ALIGN_TOP_MID, 0, 55);

    // Dew point
    lbl_dew_point = lv_label_create(parent);
    lv_label_set_text(lbl_dew_point, "Dew: --.- C");
    lv_obj_set_style_text_font(lbl_dew_point, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_dew_point, COLOR_TEXT_DIM, 0);
    lv_obj_align(lbl_dew_point, LV_ALIGN_TOP_RIGHT, -40, 55);

    // --- Y-axis temperature labels (left side) ---
    for (int i = 0; i < Y_AXIS_LABELS; i++) {
        y_axis_lbls[i] = lv_label_create(parent);
        lv_label_set_text(y_axis_lbls[i], "");
        lv_obj_set_style_text_font(y_axis_lbls[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(y_axis_lbls[i], COLOR_TEXT_DIM, 0);
        // Position evenly along chart height, top label = index 0
        int y_pos = CHART_Y_TOP + (i * CHART_HEIGHT) / (Y_AXIS_LABELS - 1);
        lv_obj_align(y_axis_lbls[i], LV_ALIGN_TOP_LEFT, 2, y_pos - 6);
    }

    // --- Chart: sky temp (green) + ambient temp (red) ---
    chart = lv_chart_create(parent);
    lv_obj_set_size(chart, CHART_WIDTH, CHART_HEIGHT);
    lv_obj_align(chart, LV_ALIGN_TOP_LEFT, CHART_X_OFFSET, CHART_Y_TOP);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, CHART_POINTS);
    lv_obj_set_style_bg_color(chart, lv_color_hex(0x0d1b2a), 0);
    lv_obj_set_style_border_color(chart, lv_color_hex(0x1f3050), 0);
    lv_obj_set_style_radius(chart, 8, 0);
    lv_obj_set_style_line_color(chart, lv_color_hex(0x1a2a3a), LV_PART_MAIN);
    lv_chart_set_div_line_count(chart, Y_AXIS_LABELS - 1, 6);

    // Sky temp series (green)
    ser_sky = lv_chart_add_series(chart, COLOR_SKY_LINE, LV_CHART_AXIS_PRIMARY_Y);

    // Ambient temp series (red)
    ser_amb = lv_chart_add_series(chart, COLOR_AMB_LINE, LV_CHART_AXIS_PRIMARY_Y);

    // Hide point markers for clean lines
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);

    // Legend labels below the chart
    lv_obj_t *leg_sky = lv_label_create(parent);
    lv_label_set_text(leg_sky, "-- Sky Temp");
    lv_obj_set_style_text_font(leg_sky, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(leg_sky, COLOR_SKY_LINE, 0);
    lv_obj_align(leg_sky, LV_ALIGN_TOP_LEFT, CHART_X_OFFSET + 10, CHART_Y_TOP + CHART_HEIGHT + 5);

    lv_obj_t *leg_amb = lv_label_create(parent);
    lv_label_set_text(leg_amb, "-- Ambient Temp");
    lv_obj_set_style_text_font(leg_amb, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(leg_amb, COLOR_AMB_LINE, 0);
    lv_obj_align(leg_amb, LV_ALIGN_TOP_LEFT, CHART_X_OFFSET + 150, CHART_Y_TOP + CHART_HEIGHT + 5);

    // Time overlay label (white, 80px, 40% opaque / 60% transparent, inside chart)
    lbl_time_overlay = lv_label_create(chart);
    lv_label_set_text(lbl_time_overlay, "--:--");
    lv_obj_set_style_text_font(lbl_time_overlay, &font_montserrat_80, 0);
    lv_obj_set_style_text_color(lbl_time_overlay, lv_color_white(), 0);
    lv_obj_set_style_text_opa(lbl_time_overlay, LV_OPA_40, 0);
    lv_obj_align(lbl_time_overlay, LV_ALIGN_TOP_MID, 0, 5);

    // Dome status banner (styled box like dashboard safe/unsafe banner)
    dome_banner = lv_obj_create(parent);
    lv_obj_set_size(dome_banner, 610, 50);
    lv_obj_align(dome_banner, LV_ALIGN_TOP_MID, 0, CHART_Y_TOP + CHART_HEIGHT + 20);
    lv_obj_set_style_bg_color(dome_banner, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_radius(dome_banner, 12, 0);
    lv_obj_set_style_border_width(dome_banner, 2, 0);
    lv_obj_set_style_border_color(dome_banner, COLOR_TEXT_DIM, 0);
    lv_obj_clear_flag(dome_banner, LV_OBJ_FLAG_SCROLLABLE);

    lbl_dome = lv_label_create(dome_banner);
    lv_label_set_text(lbl_dome, "");
    lv_obj_set_style_text_font(lbl_dome, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_dome, COLOR_TEXT_DIM, 0);
    lv_obj_center(lbl_dome);

    ESP_LOGI(TAG, "Home screen created");
    return parent;
}

void ui_home_update(const cw_current_data_t *data)
{
    if (!data || !data->valid) return;

    char buf[32];

    // Cloud state in big text
    const char *cloud_str = cw_cloud_state_str(data->clouds);
    lv_label_set_text(lbl_cloud_state, cloud_str);
    lv_obj_set_style_text_color(lbl_cloud_state, cloud_state_color(data->clouds), 0);

    // Rain state in big text
    const char *rain_str = cw_rain_state_str(data->rain);
    lv_label_set_text(lbl_rain_state, rain_str);
    lv_obj_set_style_text_color(lbl_rain_state, rain_state_color(data->rain), 0);

    // Safe/Unsafe banner
    lv_label_set_text(lbl_safe_banner, data->safe ? "SAFE" : "UNSAFE");
    lv_obj_set_style_text_color(lbl_safe_banner, data->safe ? COLOR_GREEN : COLOR_RED, 0);

    // Temperature values
    snprintf(buf, sizeof(buf), "Sky: %.1f C", data->clouds);
    lv_label_set_text(lbl_sky_temp, buf);

    snprintf(buf, sizeof(buf), "Amb: %.1f C", data->temp);
    lv_label_set_text(lbl_amb_temp, buf);

    snprintf(buf, sizeof(buf), "Dew: %.1f C", data->dew_point);
    lv_label_set_text(lbl_dew_point, buf);

    // Update time overlay on chart from system clock (NTP synced)
    if (lbl_time_overlay) {
        time_t now = time(NULL);
        struct tm *ti = localtime(&now);
        char tbuf[8];
        if (ti->tm_year > (2020 - 1900)) {
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d", ti->tm_hour, ti->tm_min);
        } else {
            snprintf(tbuf, sizeof(tbuf), "--:--");
        }
        lv_label_set_text(lbl_time_overlay, tbuf);
    }
}

void ui_home_update_graph(const cw_graph_data_t graphs[CW_GRAPH_SERIES_COUNT])
{
    if (!graphs || !chart) return;

    const cw_graph_data_t *sky = &graphs[CW_GRAPH_CLOUDS];
    const cw_graph_data_t *amb = &graphs[CW_GRAPH_TEMP];

    // Compute Y range across both series
    float min_val = FLT_MAX, max_val = -FLT_MAX;

    for (int i = 0; i < sky->today_count; i++) {
        if (isfinite(sky->today[i])) {
            if (sky->today[i] < min_val) min_val = sky->today[i];
            if (sky->today[i] > max_val) max_val = sky->today[i];
        }
    }
    for (int i = 0; i < amb->today_count; i++) {
        if (isfinite(amb->today[i])) {
            if (amb->today[i] < min_val) min_val = amb->today[i];
            if (amb->today[i] > max_val) max_val = amb->today[i];
        }
    }

    // Round range to nice boundaries (multiples of 5)
    float range_min = floorf((min_val - 2.0f) / 5.0f) * 5.0f;
    float range_max = ceilf((max_val + 2.0f) / 5.0f) * 5.0f;
    if (range_max - range_min < 10.0f) range_max = range_min + 10.0f;

    // Values are *10 for chart precision
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y,
                       (int32_t)(range_min * 10.0f),
                       (int32_t)(range_max * 10.0f));

    // Update Y-axis labels with temperature values (top = max, bottom = min)
    char buf[16];
    for (int i = 0; i < Y_AXIS_LABELS; i++) {
        float val = range_max - (range_max - range_min) * i / (Y_AXIS_LABELS - 1);
        snprintf(buf, sizeof(buf), "%+.0f", val);
        lv_label_set_text(y_axis_lbls[i], buf);
    }

    // Fill both series
    fill_series(ser_sky, sky->today, sky->today_count);
    fill_series(ser_amb, amb->today, amb->today_count);

    lv_chart_refresh(chart);

    ESP_LOGI(TAG, "Home chart updated: sky=%d pts, amb=%d pts",
             sky->today_count, amb->today_count);
}

void ui_home_update_dome(const nina_dome_status_t *dome)
{
    if (!dome || !lbl_dome) return;

    if (!dome->valid) {
        lv_label_set_text(lbl_dome, "DOME --");
        lv_obj_set_style_text_color(lbl_dome, COLOR_TEXT_DIM, 0);
        lv_obj_set_style_border_color(dome_banner, COLOR_TEXT_DIM, 0);
        lv_obj_set_style_bg_color(dome_banner, lv_color_hex(0x1a1a2e), 0);
        return;
    }

    ESP_LOGI(TAG, "Dome status: '%s'", dome->shutter_status);

    // AstroShell returns "OPEN" or "CLOSED" — anything else is unexpected
    if (strcasecmp(dome->shutter_status, "CLOSED") == 0) {
        lv_label_set_text(lbl_dome, "CLOSED");
        lv_obj_set_style_text_color(lbl_dome, COLOR_GREEN, 0);
        lv_obj_set_style_border_color(dome_banner, COLOR_GREEN, 0);
        lv_obj_set_style_bg_color(dome_banner, lv_color_hex(0x0a2a1a), 0);
    } else if (strcasecmp(dome->shutter_status, "OPEN") == 0) {
        lv_label_set_text(lbl_dome, "OPEN");
        lv_obj_set_style_text_color(lbl_dome, COLOR_RED, 0);
        lv_obj_set_style_border_color(dome_banner, COLOR_RED, 0);
        lv_obj_set_style_bg_color(dome_banner, lv_color_hex(0x2a0a0a), 0);
    } else {
        lv_label_set_text(lbl_dome, "Domestatus?");
        lv_obj_set_style_text_color(lbl_dome, COLOR_YELLOW, 0);
        lv_obj_set_style_border_color(dome_banner, COLOR_YELLOW, 0);
        lv_obj_set_style_bg_color(dome_banner, lv_color_hex(0x2a2a0a), 0);
    }
}
