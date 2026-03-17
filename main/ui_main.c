// LVGL UI manager - screen management, swipe navigation, and data updates
// v0.5.0

#include "ui_main.h"
#include "ui_home.h"
#include "ui_nina.h"
#include "ui_dashboard.h"
#include "ui_charts.h"
#include "ui_dome.h"

#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

static const char *TAG = "ui_main";

// Screen objects
static lv_obj_t *scr_home = NULL;
static lv_obj_t *scr_nina = NULL;
static lv_obj_t *scr_dashboard = NULL;
static lv_obj_t *scr_charts = NULL;
static lv_obj_t *scr_dome = NULL;
static ui_screen_t current_screen = UI_SCREEN_HOME;

// Bottom navigation bar widgets (shared across screens)
static lv_obj_t *lbl_countdown = NULL;
static lv_obj_t *lbl_wifi_status = NULL;

// Auto-swap state: cycles between Home and NINA every 120s when NINA session is active
#define AUTO_SWAP_INTERVAL_S 120
static bool nina_session_active = false;    // set from nina_poll_task
static int  auto_swap_counter = 0;          // counts up each second
static bool manual_override = false;        // true if user navigated to Dashboard/Charts
static lv_timer_t *auto_swap_timer = NULL;

// Navigate to a specific screen with appropriate animation direction
static void navigate_to(ui_screen_t target)
{
    if (target == current_screen || target < 0 || target >= UI_SCREEN_COUNT) return;

    lv_scr_load_anim_t anim = (target > current_screen)
        ? LV_SCR_LOAD_ANIM_MOVE_LEFT
        : LV_SCR_LOAD_ANIM_MOVE_RIGHT;

    current_screen = target;
    lv_obj_t *target_screens[] = {scr_home, scr_nina, scr_dashboard, scr_charts, scr_dome};
    lv_scr_load_anim(target_screens[target], anim, 300, 0, false);
}

// Handle manual navigation: reset auto-swap counter and track override state
static void on_manual_navigate(ui_screen_t target)
{
    auto_swap_counter = 0;
    // If user navigates to Dashboard or Charts, pause auto-swap
    // If user returns to Home or NINA, resume auto-swap
    manual_override = (target != UI_SCREEN_HOME && target != UI_SCREEN_NINA);
    navigate_to(target);
}

// Navigation button callback
static void nav_btn_event_cb(lv_event_t *e)
{
    ui_screen_t target = (ui_screen_t)(intptr_t)lv_event_get_user_data(e);
    on_manual_navigate(target);
}

// Swipe gesture callback - detect horizontal swipes to switch screens
static void swipe_event_cb(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());

    if (dir == LV_DIR_LEFT && current_screen < UI_SCREEN_COUNT - 1) {
        on_manual_navigate((ui_screen_t)(current_screen + 1));
    } else if (dir == LV_DIR_RIGHT && current_screen > 0) {
        on_manual_navigate((ui_screen_t)(current_screen - 1));
    }
}

// Navigation button definitions
static const struct {
    const char  *label;
    ui_screen_t  screen;
} nav_buttons[] = {
    {"Home",    UI_SCREEN_HOME},
    {"NINA",    UI_SCREEN_NINA},
    {"Details", UI_SCREEN_DASHBOARD},
    {"Charts",  UI_SCREEN_CHARTS},
    {"Dome",    UI_SCREEN_DOME},
};
#define NAV_BTN_COUNT (sizeof(nav_buttons) / sizeof(nav_buttons[0]))

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

    // Create navigation buttons
    for (int i = 0; i < (int)NAV_BTN_COUNT; i++) {
        lv_obj_t *btn = lv_btn_create(bar);
        lv_obj_set_size(btn, 85, 36);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213e), 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, nav_buttons[i].label);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, nav_btn_event_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)nav_buttons[i].screen);
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

// Auto-swap timer callback - fires every 1s, swaps Home<->NINA after 120s
static void auto_swap_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (!nina_session_active || manual_override) {
        auto_swap_counter = 0;
        return;
    }

    auto_swap_counter++;
    if (auto_swap_counter >= AUTO_SWAP_INTERVAL_S) {
        auto_swap_counter = 0;
        // Toggle between Home and NINA
        if (current_screen == UI_SCREEN_HOME) {
            navigate_to(UI_SCREEN_NINA);
        } else {
            navigate_to(UI_SCREEN_HOME);
        }
    }
}

void ui_set_nina_session_active(bool active)
{
    bool was_active = nina_session_active;
    nina_session_active = active;

    // When session ends, return to Home screen if currently on NINA
    if (was_active && !active && current_screen == UI_SCREEN_NINA && !manual_override) {
        if (bsp_display_lock(100)) {
            navigate_to(UI_SCREEN_HOME);
            bsp_display_unlock();
        }
    }

    // Reset counter when state changes
    if (was_active != active) {
        auto_swap_counter = 0;
    }
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

        // Create home screen (default)
        scr_home = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(scr_home, lv_color_hex(0x0a0a1a), 0);
        ui_home_create(scr_home);
        lv_obj_t *nav0 = create_nav_bar(scr_home);
        lv_obj_add_event_cb(scr_home, swipe_event_cb, LV_EVENT_GESTURE, NULL);

        // Create NINA image screen (screen 2)
        // Disable scrolling - canvas is wider than screen, would eat swipe gestures
        scr_nina = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(scr_nina, lv_color_hex(0x0a0a1a), 0);
        lv_obj_clear_flag(scr_nina, LV_OBJ_FLAG_SCROLLABLE);
        ui_nina_create(scr_nina);
        create_nav_bar(scr_nina);
        lv_obj_add_event_cb(scr_nina, swipe_event_cb, LV_EVENT_GESTURE, NULL);

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

        // Create dome control screen
        scr_dome = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(scr_dome, lv_color_hex(0x0a0a1a), 0);
        ui_dome_create(scr_dome);
        create_nav_bar(scr_dome);
        lv_obj_add_event_cb(scr_dome, swipe_event_cb, LV_EVENT_GESTURE, NULL);

        // Store references to shared widgets from the home nav bar
        // WiFi icon is child index NAV_BTN_COUNT (after all buttons)
        lbl_wifi_status = lv_obj_get_child(nav0, NAV_BTN_COUNT);
        lbl_countdown = lv_obj_get_child(nav0, NAV_BTN_COUNT + 1);

        // Load home as the default screen
        lv_scr_load(scr_home);

        // Create auto-swap timer (1s tick, handles Home<->NINA cycling)
        auto_swap_timer = lv_timer_create(auto_swap_timer_cb, 1000, NULL);

        bsp_display_unlock();
    } else {
        ESP_LOGE(TAG, "Failed to acquire LVGL lock");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "UI initialized (%d screens, swipe enabled)", UI_SCREEN_COUNT);
    return ESP_OK;
}

void ui_update_current_data(const cw_current_data_t *data)
{
    if (!data || !data->valid) return;

    if (bsp_display_lock(100)) {
        ui_home_update(data);
        ui_dashboard_update(data);
        ui_charts_update_time();
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

void ui_update_nina_data(const nina_image_data_t *data)
{
    if (!data) return;

    if (bsp_display_lock(200)) {
        ui_nina_update(data);
        bsp_display_unlock();
    }
}

void ui_update_dome_status(const nina_dome_status_t *dome)
{
    if (!dome) return;

    if (bsp_display_lock(100)) {
        ui_home_update_dome(dome);
        ui_dome_update(dome);
        bsp_display_unlock();
    }
}

void ui_update_nina_status(const char *message)
{
    if (!message) return;

    if (bsp_display_lock(100)) {
        ui_nina_set_status(message);
        bsp_display_unlock();
    }
}

void ui_update_nina_paused(bool paused)
{
    if (bsp_display_lock(100)) {
        ui_nina_set_paused(paused);
        bsp_display_unlock();
    }
}

void ui_navigate_to_screen(ui_screen_t screen)
{
    // Callable from UI event handlers (LVGL lock already held)
    on_manual_navigate(screen);
}

void ui_update_dome_control(const nina_dome_status_t *dome)
{
    if (!dome) return;

    if (bsp_display_lock(100)) {
        ui_dome_update(dome);
        bsp_display_unlock();
    }
}

void ui_update_forecast_data(const mb_forecast_data_t *forecast)
{
    if (!forecast) return;

    if (bsp_display_lock(500)) {
        ui_home_update_forecast(forecast);
        bsp_display_unlock();
    }
}
