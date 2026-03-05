// LVGL UI manager - screen management, swipe navigation, and data updates
// v0.4.0

#include "ui_main.h"
#include "ui_home.h"
#include "ui_dashboard.h"
#include "ui_charts.h"
#include "ui_allsky.h"

#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

static const char *TAG = "ui_main";

// Screen objects
static lv_obj_t *screens[UI_SCREEN_COUNT] = {0};
static ui_screen_t current_screen = UI_SCREEN_HOME;

// Bottom navigation bar widgets (from home screen's nav bar)
static lv_obj_t *lbl_countdown = NULL;
static lv_obj_t *lbl_wifi_status = NULL;

// Navigate to a specific screen with appropriate animation direction
static void navigate_to(ui_screen_t target)
{
    if (target == current_screen || target < 0 || target >= UI_SCREEN_COUNT) return;

    lv_scr_load_anim_t anim = (target > current_screen)
        ? LV_SCR_LOAD_ANIM_MOVE_LEFT
        : LV_SCR_LOAD_ANIM_MOVE_RIGHT;

    current_screen = target;
    lv_scr_load_anim(screens[target], anim, 300, 0, false);
}

// Navigation button callback
static void nav_btn_event_cb(lv_event_t *e)
{
    ui_screen_t target = (ui_screen_t)(intptr_t)lv_event_get_user_data(e);
    navigate_to(target);
}

// Swipe gesture callback
static void swipe_event_cb(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());

    if (dir == LV_DIR_LEFT && current_screen < UI_SCREEN_COUNT - 1) {
        navigate_to((ui_screen_t)(current_screen + 1));
    } else if (dir == LV_DIR_RIGHT && current_screen > 0) {
        navigate_to((ui_screen_t)(current_screen - 1));
    }
}

// Nav button definitions
typedef struct {
    const char  *label;
    ui_screen_t  screen;
} nav_btn_def_t;

static const nav_btn_def_t NAV_BUTTONS[] = {
    {"Home",    UI_SCREEN_HOME},
    {"Details", UI_SCREEN_DASHBOARD},
    {"Charts",  UI_SCREEN_CHARTS},
    {"Allsky",  UI_SCREEN_ALLSKY},
};
#define NUM_NAV_BUTTONS (sizeof(NAV_BUTTONS) / sizeof(NAV_BUTTONS[0]))

// Create the bottom navigation bar on a screen
static lv_obj_t *create_nav_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), 50);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 5, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (int i = 0; i < (int)NUM_NAV_BUTTONS; i++) {
        lv_obj_t *btn = lv_btn_create(bar);
        lv_obj_set_size(btn, 105, 36);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213e), 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, NAV_BUTTONS[i].label);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, nav_btn_event_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)NAV_BUTTONS[i].screen);
    }

    // WiFi status indicator
    lv_obj_t *wifi_lbl = lv_label_create(bar);
    lv_label_set_text(wifi_lbl, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(wifi_lbl, lv_color_hex(0x555555), 0);

    // Countdown label
    lv_obj_t *cd_lbl = lv_label_create(bar);
    lv_label_set_text(cd_lbl, "-- s");
    lv_obj_set_style_text_color(cd_lbl, lv_color_hex(0x888888), 0);

    return bar;
}

esp_err_t ui_init(void)
{
    ESP_LOGI(TAG, "Initializing UI...");

    if (bsp_display_lock(1000)) {
        // Set dark theme
        lv_theme_t *th = lv_theme_default_init(
            lv_display_get_default(),
            lv_color_hex(0x0f3460),
            lv_color_hex(0xe94560),
            true,
            LV_FONT_DEFAULT
        );
        lv_display_set_theme(lv_display_get_default(), th);

        // Create all screens
        for (int i = 0; i < UI_SCREEN_COUNT; i++) {
            screens[i] = lv_obj_create(NULL);
            lv_obj_set_style_bg_color(screens[i], lv_color_hex(0x0a0a1a), 0);
            lv_obj_add_event_cb(screens[i], swipe_event_cb, LV_EVENT_GESTURE, NULL);
        }

        // Populate each screen
        ui_home_create(screens[UI_SCREEN_HOME]);
        lv_obj_t *nav0 = create_nav_bar(screens[UI_SCREEN_HOME]);

        ui_dashboard_create(screens[UI_SCREEN_DASHBOARD]);
        create_nav_bar(screens[UI_SCREEN_DASHBOARD]);

        ui_charts_create(screens[UI_SCREEN_CHARTS]);
        create_nav_bar(screens[UI_SCREEN_CHARTS]);

        ui_allsky_create(screens[UI_SCREEN_ALLSKY]);
        create_nav_bar(screens[UI_SCREEN_ALLSKY]);

        // Store references to shared widgets from the home nav bar
        // Nav bar children: [btn0, btn1, btn2, btn3, wifi_lbl, cd_lbl]
        lbl_wifi_status = lv_obj_get_child(nav0, NUM_NAV_BUTTONS);
        lbl_countdown = lv_obj_get_child(nav0, NUM_NAV_BUTTONS + 1);

        lv_scr_load(screens[UI_SCREEN_HOME]);

        bsp_display_unlock();
    } else {
        ESP_LOGE(TAG, "Failed to acquire LVGL lock");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "UI initialized (4 screens, swipe enabled)");
    return ESP_OK;
}

void ui_update_current_data(const cw_current_data_t *data)
{
    if (!data || !data->valid) return;

    if (bsp_display_lock(100)) {
        ui_home_update(data);
        ui_dashboard_update(data);
        bsp_display_unlock();
    }
}

void ui_update_graph_data(const cw_graph_data_t graphs[CW_GRAPH_SERIES_COUNT])
{
    if (!graphs) return;

    if (bsp_display_lock(500)) {
        ui_home_update_graph(graphs);
        ui_charts_update(graphs);
        bsp_display_unlock();
    }
}

void ui_update_countdown(int seconds_until_refresh)
{
    if (!lbl_countdown) return;

    if (bsp_display_lock(50)) {
        if (seconds_until_refresh >= 60) {
            lv_label_set_text_fmt(lbl_countdown, "%d:%02d",
                                  seconds_until_refresh / 60,
                                  seconds_until_refresh % 60);
        } else {
            lv_label_set_text_fmt(lbl_countdown, "%ds", seconds_until_refresh);
        }
        bsp_display_unlock();
    }
}

void ui_update_wifi_status(bool connected)
{
    if (!lbl_wifi_status) return;

    if (bsp_display_lock(50)) {
        lv_obj_set_style_text_color(lbl_wifi_status,
            connected ? lv_color_hex(0x00ff88) : lv_color_hex(0xff3333), 0);
        bsp_display_unlock();
    }
}

void ui_update_time(const char *time_str)
{
    if (!time_str) return;

    if (bsp_display_lock(50)) {
        ui_home_update_time(time_str);
        bsp_display_unlock();
    }
}

void ui_update_allsky(void)
{
    // The fetch function handles its own LVGL locking — it only holds the lock
    // for brief UI updates, not during the HTTP download or JPEG decode.
    ui_allsky_fetch_and_update();
}
