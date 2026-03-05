// Charts screen - 24h historical graphs with tabbed navigation
// Displays today (solid) + yesterday (dimmer) line series
// v0.2.0

#include "ui_charts.h"
#include "cloudwatcher_client.h"

#include <stdio.h>
#include <math.h>
#include <float.h>
#include "esp_log.h"

static const char *TAG = "ui_charts";

// Color definitions
#define COLOR_TODAY     lv_color_hex(0x00b4d8)
#define COLOR_YESTERDAY lv_color_hex(0x444466)
#define COLOR_TEXT_DIM   lv_color_hex(0x8899aa)
#define COLOR_TEXT_BRIGHT lv_color_hex(0xe0e0e0)

// Chart tab definitions
typedef struct {
    const char       *label;
    const char       *unit;
    cw_graph_series_t series;
} chart_tab_def_t;

static const chart_tab_def_t CHART_TABS[] = {
    {"Cloud",    "C",       CW_GRAPH_CLOUDS},
    {"Temp",     "C",       CW_GRAPH_TEMP},
    {"Humidity", "%",       CW_GRAPH_HUMIDITY},
    {"Light",    "mpsas",   CW_GRAPH_LIGHT},
    {"Rain",     "raw",     CW_GRAPH_RAIN},
    {"Press",    "hPa",     CW_GRAPH_PRESSURE},
};
#define NUM_TABS (sizeof(CHART_TABS) / sizeof(CHART_TABS[0]))

// Active tab index
static int active_tab = 0;

// Chart widget and series references
static lv_obj_t         *chart = NULL;
static lv_chart_series_t *ser_today = NULL;
static lv_chart_series_t *ser_yesterday = NULL;

// Summary labels below the chart
static lv_obj_t *lbl_current = NULL;
static lv_obj_t *lbl_min_max = NULL;
static lv_obj_t *lbl_chart_title = NULL;

// Tab buttons
static lv_obj_t *tab_btns[NUM_TABS] = {0};

// Y-axis labels
#define Y_AXIS_LABELS 7
static lv_obj_t *y_axis_lbls[Y_AXIS_LABELS] = {0};

// Layout constants
#define CHART_X_OFFSET 50
#define CHART_WIDTH    620
#define CHART_HEIGHT   370
#define CHART_Y_TOP    78

// Local copy of graph data for re-rendering on tab switch
static cw_graph_data_t local_graphs[CW_GRAPH_SERIES_COUNT];
static bool has_graph_data = false;

// Downsample a data series to fit the chart point count
#define CHART_POINTS 200

static void populate_chart_series(lv_chart_series_t *ser,
                                   const float *y_data,
                                   int data_count)
{
    if (data_count <= 0) {
        for (int i = 0; i < CHART_POINTS; i++) {
            lv_chart_set_value_by_id(chart, ser, i, LV_CHART_POINT_NONE);
        }
        return;
    }

    // Downsample by averaging bins
    for (int i = 0; i < CHART_POINTS; i++) {
        int start = (i * data_count) / CHART_POINTS;
        int end = ((i + 1) * data_count) / CHART_POINTS;
        if (end <= start) end = start + 1;
        if (end > data_count) end = data_count;

        float sum = 0;
        int count = 0;
        for (int j = start; j < end; j++) {
            if (isfinite(y_data[j])) {
                sum += y_data[j];
                count++;
            }
        }

        if (count > 0) {
            lv_chart_set_value_by_id(chart, ser, i, (int32_t)(sum / count * 10.0f));
        } else {
            lv_chart_set_value_by_id(chart, ser, i, LV_CHART_POINT_NONE);
        }
    }
}

// Calculate min/max from a data array
static void calc_min_max(const float *data, int count, float *out_min, float *out_max)
{
    *out_min = FLT_MAX;
    *out_max = -FLT_MAX;
    for (int i = 0; i < count; i++) {
        if (isfinite(data[i])) {
            if (data[i] < *out_min) *out_min = data[i];
            if (data[i] > *out_max) *out_max = data[i];
        }
    }
}

// Update Y-axis labels to match current range
static void update_y_axis_labels(float range_min, float range_max)
{
    char buf[16];
    for (int i = 0; i < Y_AXIS_LABELS; i++) {
        if (!y_axis_lbls[i]) continue;
        float val = range_max - (range_max - range_min) * i / (Y_AXIS_LABELS - 1);
        snprintf(buf, sizeof(buf), "%.0f", val);
        lv_label_set_text(y_axis_lbls[i], buf);
    }
}

// Render the currently selected chart tab
static void render_active_chart(void)
{
    if (!has_graph_data || !chart) return;

    const chart_tab_def_t *tab = &CHART_TABS[active_tab];
    const cw_graph_data_t *g = &local_graphs[tab->series];

    // Update title
    lv_label_set_text_fmt(lbl_chart_title, "%s (%s) - 24h", tab->label, tab->unit);

    // Compute range for Y axis scaling
    float min_t, max_t, min_y, max_y;
    calc_min_max(g->today, g->today_count, &min_t, &max_t);
    calc_min_max(g->yesterday, g->yesterday_count, &min_y, &max_y);

    float data_min = fminf(min_t, min_y);
    float data_max = fmaxf(max_t, max_y);

    // Round to nice boundaries
    float step = (data_max - data_min) / (Y_AXIS_LABELS - 1);
    if (step < 1.0f) step = 1.0f;
    // Round step up to a nice number (1, 2, 5, 10, 20, 50...)
    float mag = powf(10.0f, floorf(log10f(step)));
    if (step / mag <= 1.0f) step = mag;
    else if (step / mag <= 2.0f) step = 2.0f * mag;
    else if (step / mag <= 5.0f) step = 5.0f * mag;
    else step = 10.0f * mag;

    float range_min = floorf(data_min / step) * step;
    float range_max = ceilf(data_max / step) * step;
    if (range_max <= range_min) range_max = range_min + step;

    // Set chart Y range (values *10 for precision)
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y,
                       (int32_t)(range_min * 10.0f),
                       (int32_t)(range_max * 10.0f));

    // Update Y-axis labels
    update_y_axis_labels(range_min, range_max);

    // Populate chart series
    populate_chart_series(ser_today, g->today, g->today_count);
    populate_chart_series(ser_yesterday, g->yesterday, g->yesterday_count);

    lv_chart_refresh(chart);

    // Update summary labels (use C snprintf since LVGL's printf doesn't support %f)
    if (g->today_count > 0) {
        float current = g->today[g->today_count - 1];
        char buf[64];
        snprintf(buf, sizeof(buf), "Current: %.1f %s", current, tab->unit);
        lv_label_set_text(lbl_current, buf);
        snprintf(buf, sizeof(buf), "Min: %.1f  |  Max: %.1f", min_t, max_t);
        lv_label_set_text(lbl_min_max, buf);
    } else {
        lv_label_set_text(lbl_current, "No data");
        lv_label_set_text(lbl_min_max, "");
    }

    // Highlight active tab button
    for (int i = 0; i < (int)NUM_TABS; i++) {
        if (tab_btns[i]) {
            lv_obj_set_style_bg_color(tab_btns[i],
                i == active_tab ? lv_color_hex(0x0f3460) : lv_color_hex(0x16213e), 0);
        }
    }
}

static void tab_btn_event_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx == active_tab) return;

    active_tab = idx;
    render_active_chart();
}

lv_obj_t *ui_charts_create(lv_obj_t *parent)
{
    // Tab buttons row
    lv_obj_t *tab_row = lv_obj_create(parent);
    lv_obj_set_size(tab_row, LV_PCT(100), 44);
    lv_obj_align(tab_row, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(tab_row, lv_color_hex(0x0f0f2a), 0);
    lv_obj_set_style_border_width(tab_row, 0, 0);
    lv_obj_set_style_radius(tab_row, 0, 0);
    lv_obj_set_style_pad_all(tab_row, 4, 0);
    lv_obj_set_flex_flow(tab_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(tab_row, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < (int)NUM_TABS; i++) {
        lv_obj_t *btn = lv_btn_create(tab_row);
        lv_obj_set_size(btn, 105, 32);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213e), 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_pad_all(btn, 2, 0);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, CHART_TABS[i].label);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, tab_btn_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        tab_btns[i] = btn;
    }

    // Chart title
    lbl_chart_title = lv_label_create(parent);
    lv_label_set_text(lbl_chart_title, "Cloud (C) - 24h");
    lv_obj_set_style_text_font(lbl_chart_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_chart_title, COLOR_TEXT_BRIGHT, 0);
    lv_obj_align(lbl_chart_title, LV_ALIGN_TOP_MID, 20, 52);

    // Legend
    lv_obj_t *legend = lv_obj_create(parent);
    lv_obj_set_size(legend, 300, 24);
    lv_obj_align(legend, LV_ALIGN_TOP_RIGHT, -20, 50);
    lv_obj_set_style_bg_opa(legend, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(legend, 0, 0);
    lv_obj_set_style_pad_all(legend, 0, 0);
    lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(legend, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(legend, LV_OBJ_FLAG_SCROLLABLE);

    // Today legend swatch
    lv_obj_t *sw_today = lv_obj_create(legend);
    lv_obj_set_size(sw_today, 16, 3);
    lv_obj_set_style_bg_color(sw_today, COLOR_TODAY, 0);
    lv_obj_set_style_border_width(sw_today, 0, 0);
    lv_obj_set_style_radius(sw_today, 0, 0);

    lv_obj_t *lbl_leg_today = lv_label_create(legend);
    lv_label_set_text(lbl_leg_today, " Today  ");
    lv_obj_set_style_text_font(lbl_leg_today, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_leg_today, COLOR_TEXT_DIM, 0);

    // Yesterday legend swatch
    lv_obj_t *sw_yest = lv_obj_create(legend);
    lv_obj_set_size(sw_yest, 16, 3);
    lv_obj_set_style_bg_color(sw_yest, COLOR_YESTERDAY, 0);
    lv_obj_set_style_border_width(sw_yest, 0, 0);
    lv_obj_set_style_radius(sw_yest, 0, 0);

    lv_obj_t *lbl_leg_yest = lv_label_create(legend);
    lv_label_set_text(lbl_leg_yest, " Yesterday");
    lv_obj_set_style_text_font(lbl_leg_yest, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_leg_yest, COLOR_TEXT_DIM, 0);

    // Y-axis labels (left side of chart)
    for (int i = 0; i < Y_AXIS_LABELS; i++) {
        y_axis_lbls[i] = lv_label_create(parent);
        lv_label_set_text(y_axis_lbls[i], "");
        lv_obj_set_style_text_font(y_axis_lbls[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(y_axis_lbls[i], COLOR_TEXT_DIM, 0);
        int y_pos = CHART_Y_TOP + (i * CHART_HEIGHT) / (Y_AXIS_LABELS - 1);
        lv_obj_align(y_axis_lbls[i], LV_ALIGN_TOP_LEFT, 2, y_pos - 6);
    }

    // Chart widget
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

    // Today series (thicker, brighter)
    ser_today = lv_chart_add_series(chart, COLOR_TODAY, LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);

    // Yesterday series (thinner, dimmer)
    ser_yesterday = lv_chart_add_series(chart, COLOR_YESTERDAY, LV_CHART_AXIS_PRIMARY_Y);

    // Hide point markers for cleaner lines
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);

    // Summary labels
    lbl_current = lv_label_create(parent);
    lv_label_set_text(lbl_current, "Waiting for data...");
    lv_obj_set_style_text_font(lbl_current, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_current, COLOR_TEXT_BRIGHT, 0);
    lv_obj_align(lbl_current, LV_ALIGN_TOP_LEFT, CHART_X_OFFSET, CHART_Y_TOP + CHART_HEIGHT + 8);

    lbl_min_max = lv_label_create(parent);
    lv_label_set_text(lbl_min_max, "");
    lv_obj_set_style_text_font(lbl_min_max, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_min_max, COLOR_TEXT_DIM, 0);
    lv_obj_align(lbl_min_max, LV_ALIGN_TOP_LEFT, CHART_X_OFFSET, CHART_Y_TOP + CHART_HEIGHT + 30);

    ESP_LOGI(TAG, "Charts screen created with %d tabs", (int)NUM_TABS);
    return parent;
}

void ui_charts_update(const cw_graph_data_t graphs[CW_GRAPH_SERIES_COUNT])
{
    if (!graphs) return;

    // Store a local copy of graph data for tab switching
    memcpy(local_graphs, graphs, sizeof(local_graphs));
    has_graph_data = true;

    render_active_chart();
}
