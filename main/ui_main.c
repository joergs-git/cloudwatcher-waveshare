// LVGL UI manager - screen management, swipe navigation, and data updates
// v0.3.0

#include "ui_main.h"
#include "ui_home.h"
#include "ui_dashboard.h"
#include "ui_charts.h"

#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

static const char *TAG = "ui_main";

// Screen objects
static lv_obj_t *scr_home = NULL;
static lv_obj_t *scr_dashboard = NULL;
static lv_obj_t *scr_charts = NULL;
static ui_screen_t current_screen = UI_SCREEN_HOME;

// Bottom navigation bar widgets (shared across screens)
static lv_obj_t *lbl_countdown = NULL;
static lv_obj_t *lbl_wifi_status = NULL;

// Screen array for indexed access
static lv_obj_t **screens_arr[UI_SCREEN_COUNT];

// Navigate to a specific screen with appropriate animation direction
static void navigate_to(ui_screen_t target)
{
    if (target == current_screen || target < 0 || target >= UI_SCREEN_COUNT) return;

    lv_scr_load_anim_t anim = (target > current_screen)
        ? LV_SCR_LOAD_ANIM_MOVE_LEFT
        : LV_SCR_LOAD_ANIM_MOVE_RIGHT;

    current_screen = target;
    lv_obj_t *target_screens[] = {scr_home, scr_dashboard, scr_charts};
    lv_scr_load_anim(target_screens[target], anim, 300, 0, false);
}

// Navigation button callback
static void nav_btn_event_cb(lv_event_t *e)
{
    ui_screen_t target = (ui_screen_t)(intptr_t)lv_event_get_user_data(e);
    navigate_to(target);
}

// Swipe gesture callback - detect horizontal swipes to switch screens
static void swipe_event_cb(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());

    if (dir == LV_DIR_LEFT && current_screen < UI_SCREEN_COUNT - 1) {
        navigate_to((ui_screen_t)(current_screen + 1));
    } else if (dir == LV_DIR_RIGHT && current_screen > 0) {
        navigate_to((ui_screen_t)(current_screen - 1));
    }
}

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

    // Home button
    lv_obj_t *btn_home = lv_btn_create(bar);
    lv_obj_set_size(btn_home, 130, 36);
    lv_obj_set_style_bg_color(btn_home, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(btn_home, 8, 0);
    lv_obj_t *lbl = lv_label_create(btn_home);
    lv_label_set_text(lbl, "Home");
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn_home, nav_btn_event_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)UI_SCREEN_HOME);

    // Dashboard button
    lv_obj_t *btn_dash = lv_btn_create(bar);
    lv_obj_set_size(btn_dash, 130, 36);
    lv_obj_set_style_bg_color(btn_dash, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(btn_dash, 8, 0);
    lbl = lv_label_create(btn_dash);
    lv_label_set_text(lbl, "Details");
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn_dash, nav_btn_event_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)UI_SCREEN_DASHBOARD);

    // Charts button
    lv_obj_t *btn_charts = lv_btn_create(bar);
    lv_obj_set_size(btn_charts, 130, 36);
    lv_obj_set_style_bg_color(btn_charts, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_radius(btn_charts, 8, 0);
    lbl = lv_label_create(btn_charts);
    lv_label_set_text(lbl, "Charts");
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn_charts, nav_btn_event_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)UI_SCREEN_CHARTS);

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

    // Lock LVGL for thread-safe creation
    if (bsp_display_lock(1000)) {
        // Set dark theme
        lv_theme_t *th = lv_theme_default_init(
            lv_display_get_default(),
            lv_color_hex(0x0f3460),   // primary color
            lv_color_hex(0xe94560),   // secondary color
            true,                      // dark mode
            LV_FONT_DEFAULT
        );
        lv_display_set_theme(lv_display_get_default(), th);

        // Create home screen (new default)
        scr_home = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(scr_home, lv_color_hex(0x0a0a1a), 0);
        ui_home_create(scr_home);
        lv_obj_t *nav0 = create_nav_bar(scr_home);
        lv_obj_add_event_cb(scr_home, swipe_event_cb, LV_EVENT_GESTURE, NULL);

        // Create dashboard screen
        scr_dashboard = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(scr_dashboard, lv_color_hex(0x0a0a1a), 0);
        ui_dashboard_create(scr_dashboard);
        create_nav_bar(scr_dashboard);
        lv_obj_add_event_cb(scr_dashboard, swipe_event_cb, LV_EVENT_GESTURE, NULL);

        // Create charts screen
        scr_charts = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(scr_charts, lv_color_hex(0x0a0a1a), 0);
        ui_charts_create(scr_charts);
        create_nav_bar(scr_charts);
        lv_obj_add_event_cb(scr_charts, swipe_event_cb, LV_EVENT_GESTURE, NULL);

        // Store references to shared widgets from the home nav bar
        lbl_wifi_status = lv_obj_get_child(nav0, 3);  // WiFi icon
        lbl_countdown = lv_obj_get_child(nav0, 4);    // Countdown label

        // Load home as the default screen
        lv_scr_load(scr_home);

        bsp_display_unlock();
    } else {
        ESP_LOGE(TAG, "Failed to acquire LVGL lock");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "UI initialized (swipe enabled)");
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
