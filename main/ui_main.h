#ifndef UI_MAIN_H
#define UI_MAIN_H

#include "esp_err.h"
#include "cloudwatcher_client.h"
#include "nina_client.h"

// Screen identifiers (order determines swipe navigation sequence)
typedef enum {
    UI_SCREEN_HOME = 0,
    UI_SCREEN_NINA,
    UI_SCREEN_DASHBOARD,
    UI_SCREEN_CHARTS,
    UI_SCREEN_COUNT
} ui_screen_t;

// Initialize the LVGL UI (call after display_driver_init)
esp_err_t ui_init(void);

// Update dashboard with new current data (thread-safe via LVGL lock)
void ui_update_current_data(const cw_current_data_t *data);

// Update charts with new graph data (thread-safe via LVGL lock)
void ui_update_graph_data(const cw_graph_data_t graphs[CW_GRAPH_SERIES_COUNT]);

// Update the refresh countdown timer display
void ui_update_countdown(int seconds_until_refresh);

// Update WiFi connection status indicator
void ui_update_wifi_status(bool connected);

// Update NINA screen with new image data (thread-safe via LVGL lock)
void ui_update_nina_data(const nina_image_data_t *data);

// Update NINA screen status message (thread-safe via LVGL lock)
void ui_update_nina_status(const char *message);

// Update dome status on home screen (thread-safe via LVGL lock)
void ui_update_dome_status(const nina_dome_status_t *dome);

// Request an immediate NINA image refresh (called from UI tap handler)
void nina_request_refresh(void);

#endif // UI_MAIN_H
