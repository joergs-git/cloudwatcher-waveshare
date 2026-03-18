// Home screen - primary overview with big status text and sky/ambient temp chart
// Chart shows -12h to +12h centered on "now" with Meteoblue cloud forecast overlay
// v0.5.0

#include "ui_home.h"
#include "ui_main.h"
#include "cloudwatcher_client.h"
#include "meteoblue_client.h"

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
#define COLOR_SKY_LINE  lv_color_hex(0x00e676)  // bright green for sky temp
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
static lv_chart_series_t *ser_cloud = NULL;  // Meteoblue cloud cover forecast

static lv_obj_t *home_parent = NULL;  // screen reference

// "Now" vertical line inside chart
static lv_obj_t *now_line = NULL;
static lv_point_precise_t now_line_pts[2];

// Y-axis labels along left side of chart
#define Y_AXIS_LABELS 7
static lv_obj_t *y_axis_lbls[Y_AXIS_LABELS] = {0};

// X-axis time labels inside chart bottom
#define X_AXIS_LABELS 5
static lv_obj_t *x_axis_lbls[X_AXIS_LABELS] = {0};

#define CHART_POINTS 100  // 50 past + 50 future
#define CHART_X_OFFSET 50   // left margin for Y-axis labels
#define CHART_WIDTH    610
#define CHART_HEIGHT   430
#define CHART_Y_TOP    85

// Left half = past 12h (indices 0..49), right half = future (50..99)
#define CHART_NOW_INDEX 50
#define HALF_WINDOW_MIN 720.0f  // 12 hours in minutes

// CloudWatcher graphData x-value conversion: x values are indices starting at -30
// A full day has 750 points from x=-30 to x=719.
// Convert to minutes from midnight: real_min = (x + 30) * (1440 / 750)
#define CW_X_OFFSET  30.0f
#define CW_X_SCALE   (1440.0f / 750.0f)
#define CW_X_TO_MIN(x) (((x) + CW_X_OFFSET) * CW_X_SCALE)

// Stored Y-range bounds from CloudWatcher data
static float cw_y_min = FLT_MAX, cw_y_max = -FLT_MAX;
static bool cw_y_valid = false;

// Update the chart Y-range and axis labels based on CloudWatcher data only
// (forecast bars use their own 0-100% scale, independent of temp Y-axis)
static void update_chart_range(void)
{
    float min_val = FLT_MAX, max_val = -FLT_MAX;

    if (cw_y_valid) {
        min_val = cw_y_min;
        max_val = cw_y_max;
    }

    // Fallback if no data yet
    if (min_val >= max_val) {
        min_val = -30.0f;
        max_val = 30.0f;
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
}

// Fill a chart series with CW data mapped to the past-12h half (indices 0..99)
// Uses today + yesterday data to cover the full 12h window across midnight
static void fill_series_past_12h(lv_chart_series_t *ser,
                                  const float *today_data, const float *today_x,
                                  int today_count,
                                  const float *yesterday_data, const float *yesterday_x,
                                  int yesterday_count)
{
    // Clear all chart points
    for (int i = 0; i < CHART_POINTS; i++) {
        lv_chart_set_value_by_id(chart, ser, i, LV_CHART_POINT_NONE);
    }

    if (today_count <= 0 && yesterday_count <= 0) return;

    // Get current time as minutes from midnight
    time_t now = time(NULL);
    struct tm *ti = localtime(&now);
    float now_min = ti->tm_hour * 60.0f + ti->tm_min + ti->tm_sec / 60.0f;
    float cutoff_min = now_min - HALF_WINDOW_MIN;  // 12h ago (may be negative)

    // Each chart point in the left half spans 7.2 minutes
    float minutes_per_point = HALF_WINDOW_MIN / (float)CHART_NOW_INDEX;

    // Accumulator bins for averaging multiple data points per chart slot
    float bin_sum[CHART_NOW_INDEX];
    int bin_count[CHART_NOW_INDEX];
    memset(bin_sum, 0, sizeof(bin_sum));
    memset(bin_count, 0, sizeof(bin_count));

    // If cutoff crosses midnight, include yesterday's data (shifted by +1440 min)
    if (cutoff_min < 0 && yesterday_count > 0) {
        float yesterday_cutoff = cutoff_min + 1440.0f;  // e.g. -180 -> 1260 (21:00)
        for (int i = 0; i < yesterday_count; i++) {
            float real_min = CW_X_TO_MIN(yesterday_x[i]);
            if (real_min < yesterday_cutoff) continue;  // before our window

            // Offset relative to cutoff: yesterday's time shifted to negative minutes
            float offset = (real_min - 1440.0f) - cutoff_min;  // map to 0..HALF_WINDOW_MIN
            int idx = (int)(offset / minutes_per_point);
            if (idx < 0) idx = 0;
            if (idx >= CHART_NOW_INDEX) idx = CHART_NOW_INDEX - 1;

            if (isfinite(yesterday_data[i])) {
                bin_sum[idx] += yesterday_data[i];
                bin_count[idx]++;
            }
        }
    }

    // Today's data — convert CW x indices to real minutes from midnight
    for (int i = 0; i < today_count; i++) {
        float real_min = CW_X_TO_MIN(today_x[i]);

        if (real_min > now_min) continue;
        // When cutoff is positive, skip data before cutoff
        if (cutoff_min >= 0 && real_min < cutoff_min) continue;

        float offset = real_min - cutoff_min;  // 0 at -12h, HALF_WINDOW_MIN at now
        int idx = (int)(offset / minutes_per_point);
        if (idx < 0) idx = 0;
        if (idx >= CHART_NOW_INDEX) idx = CHART_NOW_INDEX - 1;

        if (isfinite(today_data[i])) {
            bin_sum[idx] += today_data[i];
            bin_count[idx]++;
        }
    }

    int filled = 0;
    for (int i = 0; i < CHART_NOW_INDEX; i++) {
        if (bin_count[i] > 0) {
            float avg = bin_sum[i] / (float)bin_count[i];
            lv_chart_set_value_by_id(chart, ser, i, (int32_t)(avg * 10.0f));
            filled++;
        }
    }

    ESP_LOGI(TAG, "fill_series: now_min=%.0f cutoff=%.0f filled=%d/%d bins",
             now_min, cutoff_min, filled, CHART_NOW_INDEX);
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

// Navigate to Dome Control screen when dome banner is tapped
static void dome_banner_tap_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Dome banner tapped, navigating to Dome Control");
    ui_navigate_to_screen(UI_SCREEN_DOME);
}

lv_obj_t *ui_home_create(lv_obj_t *parent)
{
    home_parent = parent;

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

    // --- Chart: sky temp + ambient temp + Meteoblue forecast ---
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

    // Sky temp series (bright green)
    ser_sky = lv_chart_add_series(chart, COLOR_SKY_LINE, LV_CHART_AXIS_PRIMARY_Y);

    // Ambient temp series (red)
    ser_amb = lv_chart_add_series(chart, COLOR_AMB_LINE, LV_CHART_AXIS_PRIMARY_Y);

    // Meteoblue cloud cover forecast (grey, plotted in future half of chart)
    ser_cloud = lv_chart_add_series(chart, lv_color_hex(0x888888), LV_CHART_AXIS_PRIMARY_Y);

    // Hide point markers for clean lines
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);

    // Chart non-interactive: touches pass through to parent screen for gesture detection
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scrollbar_mode(chart, LV_SCROLLBAR_MODE_OFF);

    // Forecast bars are created lazily on first data update to avoid startup issues

    // "Now" vertical line at chart midpoint
    now_line_pts[0] = (lv_point_precise_t){0, 0};
    now_line_pts[1] = (lv_point_precise_t){0, CHART_HEIGHT};
    now_line = lv_line_create(chart);
    lv_line_set_points(now_line, now_line_pts, 2);
    lv_obj_set_style_line_color(now_line, lv_color_hex(0x556677), 0);
    lv_obj_set_style_line_width(now_line, 2, 0);
    lv_obj_set_style_line_opa(now_line, LV_OPA_60, 0);
    // Position at horizontal midpoint of chart content area
    lv_obj_align(now_line, LV_ALIGN_LEFT_MID, CHART_WIDTH / 2, 0);

    // X-axis time labels inside chart bottom
    static const char *x_labels[X_AXIS_LABELS] = {"-12h", "-6h", "now", "+6h", "+12h"};
    for (int i = 0; i < X_AXIS_LABELS; i++) {
        x_axis_lbls[i] = lv_label_create(chart);
        lv_label_set_text(x_axis_lbls[i], x_labels[i]);
        lv_obj_set_style_text_font(x_axis_lbls[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(x_axis_lbls[i], COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_opa(x_axis_lbls[i], LV_OPA_70, 0);
        // Distribute evenly across chart width, bottom-aligned
        int x_pos = (i * CHART_WIDTH) / (X_AXIS_LABELS - 1);
        // Center the label on its position (approximate offset for text width)
        int x_offset = (i == 0) ? 2 : (i == X_AXIS_LABELS - 1) ? -30 : -12;
        lv_obj_align(x_axis_lbls[i], LV_ALIGN_BOTTOM_LEFT, x_pos + x_offset, -2);
    }

    // Legend labels below the chart
    lv_obj_t *leg_sky = lv_label_create(parent);
    lv_label_set_text(leg_sky, "-- Sky Temp");
    lv_obj_set_style_text_font(leg_sky, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(leg_sky, COLOR_SKY_LINE, 0);
    lv_obj_align(leg_sky, LV_ALIGN_TOP_LEFT, CHART_X_OFFSET + 10, CHART_Y_TOP + CHART_HEIGHT + 5);

    lv_obj_t *leg_amb = lv_label_create(parent);
    lv_label_set_text(leg_amb, "-- Ambient");
    lv_obj_set_style_text_font(leg_amb, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(leg_amb, COLOR_AMB_LINE, 0);
    lv_obj_align(leg_amb, LV_ALIGN_TOP_LEFT, CHART_X_OFFSET + 130, CHART_Y_TOP + CHART_HEIGHT + 5);

    lv_obj_t *leg_fc = lv_label_create(parent);
    lv_label_set_text(leg_fc, "Cloud %");
    lv_obj_set_style_text_font(leg_fc, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(leg_fc, lv_color_hex(0x888888), 0);
    lv_obj_align(leg_fc, LV_ALIGN_TOP_LEFT, CHART_X_OFFSET + 240, CHART_Y_TOP + CHART_HEIGHT + 5);

    // Time overlay label - chart child, custom 80px font, semi-transparent
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

    // Make dome banner tappable - navigates to Dome Control screen
    lv_obj_add_flag(dome_banner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(dome_banner, dome_banner_tap_cb, LV_EVENT_CLICKED, NULL);

    ESP_LOGI(TAG, "Home screen created (chart: -12h to +12h with forecast)");
    return parent;
}

void ui_home_update_time(void)
{
    if (!lbl_time_overlay) return;

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

    // Update time overlay (also called every second from ui_update_countdown)
    ui_home_update_time();
}

void ui_home_update_graph(const cw_graph_data_t graphs[CW_GRAPH_SERIES_COUNT])
{
    if (!graphs || !chart) return;

    // Skip if NTP hasn't synced yet - time-based mapping would be wrong
    time_t now = time(NULL);
    struct tm *ti = localtime(&now);
    if (ti->tm_year <= (2020 - 1900)) {
        ESP_LOGW(TAG, "NTP not synced yet, skipping graph update");
        return;
    }

    const cw_graph_data_t *sky = &graphs[CW_GRAPH_CLOUDS];
    const cw_graph_data_t *amb = &graphs[CW_GRAPH_TEMP];
    float now_min = ti->tm_hour * 60.0f + ti->tm_min;
    float cutoff_min = now_min - HALF_WINDOW_MIN;

    cw_y_min = FLT_MAX;
    cw_y_max = -FLT_MAX;

    // Today's data in window (convert CW x indices to real minutes)
    for (int i = 0; i < sky->today_count; i++) {
        float real_min = CW_X_TO_MIN(sky->today_x[i]);
        if (real_min > now_min) continue;
        if (cutoff_min >= 0 && real_min < cutoff_min) continue;
        if (isfinite(sky->today[i])) {
            if (sky->today[i] < cw_y_min) cw_y_min = sky->today[i];
            if (sky->today[i] > cw_y_max) cw_y_max = sky->today[i];
        }
    }
    for (int i = 0; i < amb->today_count; i++) {
        float real_min = CW_X_TO_MIN(amb->today_x[i]);
        if (real_min > now_min) continue;
        if (cutoff_min >= 0 && real_min < cutoff_min) continue;
        if (isfinite(amb->today[i])) {
            if (amb->today[i] < cw_y_min) cw_y_min = amb->today[i];
            if (amb->today[i] > cw_y_max) cw_y_max = amb->today[i];
        }
    }

    // Yesterday's data when window crosses midnight
    if (cutoff_min < 0) {
        float yesterday_cutoff = cutoff_min + 1440.0f;
        for (int i = 0; i < sky->yesterday_count; i++) {
            float real_min = CW_X_TO_MIN(sky->yesterday_x[i]);
            if (real_min < yesterday_cutoff) continue;
            if (isfinite(sky->yesterday[i])) {
                if (sky->yesterday[i] < cw_y_min) cw_y_min = sky->yesterday[i];
                if (sky->yesterday[i] > cw_y_max) cw_y_max = sky->yesterday[i];
            }
        }
        for (int i = 0; i < amb->yesterday_count; i++) {
            float real_min = CW_X_TO_MIN(amb->yesterday_x[i]);
            if (real_min < yesterday_cutoff) continue;
            if (isfinite(amb->yesterday[i])) {
                if (amb->yesterday[i] < cw_y_min) cw_y_min = amb->yesterday[i];
                if (amb->yesterday[i] > cw_y_max) cw_y_max = amb->yesterday[i];
            }
        }
    }

    cw_y_valid = (cw_y_min < cw_y_max);

    // Update chart Y-range based on CW data
    update_chart_range();

    // Fill sky and ambient series for past 12h (left half of chart)
    // Include yesterday's data when the 12h window crosses midnight
    fill_series_past_12h(ser_sky, sky->today, sky->today_x, sky->today_count,
                         sky->yesterday, sky->yesterday_x, sky->yesterday_count);
    fill_series_past_12h(ser_amb, amb->today, amb->today_x, amb->today_count,
                         amb->yesterday, amb->yesterday_x, amb->yesterday_count);

    lv_chart_refresh(chart);

    // Diagnostic: check if home screen content overflows (causes scroll, kills gestures)
    lv_obj_update_layout(home_parent);
    int32_t sh = lv_obj_get_height(home_parent);
    int32_t ch = lv_obj_get_content_height(home_parent);
    int32_t st = lv_obj_get_scroll_top(home_parent);
    int32_t sb = lv_obj_get_scroll_bottom(home_parent);
    ESP_LOGW(TAG, "SCROLL CHECK: screen_h=%ld content_h=%ld scroll_top=%ld scroll_bottom=%ld",
             (long)sh, (long)ch, (long)st, (long)sb);
    if (sb > 0) {
        ESP_LOGE(TAG, "CONTENT OVERFLOW by %ld px — this kills gestures!", (long)sb);
    }

    // Diagnostic: log x-value ranges to verify data format
    if (sky->today_count > 0) {
        ESP_LOGI(TAG, "Sky today: %d pts, x[0]=%.1f x[last]=%.1f (expect min from midnight)",
                 sky->today_count, sky->today_x[0], sky->today_x[sky->today_count - 1]);
    }
    if (sky->yesterday_count > 0) {
        ESP_LOGI(TAG, "Sky yesterday: %d pts, x[0]=%.1f x[last]=%.1f",
                 sky->yesterday_count, sky->yesterday_x[0], sky->yesterday_x[sky->yesterday_count - 1]);
    }
    ESP_LOGI(TAG, "Home chart: now_min=%.0f cutoff=%.0f y_range=[%.1f..%.1f]",
             now_min, cutoff_min, cw_y_min, cw_y_max);
}

void ui_home_update_forecast(const mb_forecast_data_t *forecast)
{
    if (!chart || !ser_cloud) return;

    // Clear the cloud series (all points = NONE)
    for (int i = 0; i < CHART_POINTS; i++) {
        lv_chart_set_value_by_id(chart, ser_cloud, i, LV_CHART_POINT_NONE);
    }

    if (!forecast || !forecast->valid || forecast->count < 1) return;

    // Get current chart Y-range for mapping cloud % to temperature scale
    // 0% cloud = chart minimum, 100% cloud = chart maximum
    float range_min, range_max;
    if (cw_y_valid) {
        range_min = floorf((cw_y_min - 2.0f) / 5.0f) * 5.0f;
        range_max = ceilf((cw_y_max + 2.0f) / 5.0f) * 5.0f;
        if (range_max - range_min < 10.0f) range_max = range_min + 10.0f;
    } else {
        range_min = -30.0f;
        range_max = 30.0f;
    }

    // Map forecast to full 24h window: -12h (index 0) to +12h (index CHART_POINTS-1)
    time_t now = time(NULL);
    time_t window_start = now - (time_t)(HALF_WINDOW_MIN * 60);
    float total_secs = HALF_WINDOW_MIN * 60.0f * 2.0f;  // 24h in seconds
    float secs_per_point = total_secs / (float)CHART_POINTS;

    int filled = 0;
    for (int i = 0; i < forecast->count; i++) {
        float offset_secs = (float)(forecast->timestamps[i] - window_start);
        if (offset_secs < 0 || offset_secs >= total_secs) continue;

        int idx = (int)(offset_secs / secs_per_point);
        if (idx < 0 || idx >= CHART_POINTS) continue;

        // Map cloud cover: 0% = range_min, 100% = range_max
        float cloud_val = range_min + (forecast->cloud_cover_pct[i] / 100.0f) * (range_max - range_min);
        lv_chart_set_value_by_id(chart, ser_cloud, idx, (int32_t)(cloud_val * 10.0f));
        filled++;
    }

    lv_chart_refresh(chart);
    ESP_LOGI(TAG, "Forecast line: %d points from %d entries", filled, forecast->count);
}

lv_obj_t *ui_home_get_chart(void)
{
    return chart;
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
