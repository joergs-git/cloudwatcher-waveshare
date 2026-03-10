// CloudWatcher Waveshare Display - Main Entry Point
// Displays AAG CloudWatcher Solo sensor data on Waveshare ESP32-P4 Touch LCD 4B
// v0.4.4

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

#include "wifi_manager.h"
#include "cloudwatcher_client.h"
#include "nina_client.h"
#include "display_driver.h"
#include "ui_main.h"

static const char *TAG = "main";

// Graph data allocated in PSRAM (large arrays: ~108KB for 6 series)
static cw_graph_data_t *graph_data = NULL;

// Flag for manual NINA image refresh (set by UI tap, checked by poll task)
static volatile bool nina_refresh_flag = false;

void nina_request_refresh(void)
{
    nina_refresh_flag = true;
}

// Data polling task: fetches sensor data on schedule
static void cw_poll_task(void *arg)
{
    // Wait for WiFi connection before starting
    ESP_LOGI(TAG, "Poll task waiting for WiFi...");
    while (!wifi_manager_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "WiFi connected, starting data polling");

    cw_current_data_t current_data;
    int poll_counter = 0;
    const int current_interval_s = CONFIG_CW_POLL_INTERVAL_S;
    const int graph_interval_s = CONFIG_CW_GRAPH_POLL_INTERVAL_S;
    // Fetch graphs every N current-data polls
    const int graph_every_n = graph_interval_s / current_interval_s;

    // Initial fetch of both endpoints
    esp_err_t err = cw_fetch_current(&current_data);
    if (err == ESP_OK) {
        ui_update_current_data(&current_data);
    }

    if (graph_data) {
        err = cw_fetch_graphs(graph_data);
        if (err == ESP_OK) {
            ui_update_graph_data(graph_data);
        }
    }

    while (1) {
        // Countdown display between polls
        for (int s = current_interval_s; s > 0; s--) {
            ui_update_countdown(s);
            vTaskDelay(pdMS_TO_TICKS(1000));

            // Update WiFi status
            ui_update_wifi_status(wifi_manager_is_connected());
        }

        // Skip if WiFi is disconnected
        if (!wifi_manager_is_connected()) {
            ESP_LOGW(TAG, "WiFi disconnected, skipping poll");
            continue;
        }

        // Fetch current sensor data
        err = cw_fetch_current(&current_data);
        if (err == ESP_OK) {
            ui_update_current_data(&current_data);
        } else {
            ESP_LOGW(TAG, "Failed to fetch current data: %s", esp_err_to_name(err));
        }

        // Periodically fetch graph data
        poll_counter++;
        if (poll_counter >= graph_every_n && graph_data) {
            poll_counter = 0;
            ESP_LOGI(TAG, "Fetching 24h graph data...");
            err = cw_fetch_graphs(graph_data);
            if (err == ESP_OK) {
                ui_update_graph_data(graph_data);
            } else {
                ESP_LOGW(TAG, "Failed to fetch graph data: %s", esp_err_to_name(err));
            }
        }
    }
}

// NINA image polling task: fetches latest sub-exposure every N seconds
static void nina_poll_task(void *arg)
{
    // Wait for WiFi before starting
    ESP_LOGI(TAG, "NINA poll task waiting for WiFi...");
    while (!wifi_manager_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "Starting NINA image polling (%s:%d every %ds)",
             CONFIG_NINA_HOST_IP, CONFIG_NINA_HOST_PORT, CONFIG_NINA_POLL_INTERVAL_S);

    nina_image_data_t image_data;
    nina_dome_status_t dome_status;
    int prev_image_count = -1;  // track previous count to detect active shooting

    while (1) {
        if (wifi_manager_is_connected()) {
            // Fetch latest image from NINA
            esp_err_t err = nina_fetch_image(&image_data);
            if (err == ESP_OK) {
                ui_update_nina_data(&image_data);
                // Only auto-swap when image count is increasing (actively shooting)
                int cur_count = image_data.meta.image_count;
                if (prev_image_count >= 0 && cur_count > prev_image_count) {
                    // Actively capturing new subs
                    ui_set_nina_session_active(true);
                    ui_update_nina_paused(false);
                } else if (prev_image_count >= 0 && cur_count == prev_image_count) {
                    // Count unchanged - sequence paused or finished, show last image with "Paused"
                    ui_set_nina_session_active(false);
                    ui_update_nina_paused(true);
                }
                prev_image_count = cur_count;
            } else if (err == ESP_ERR_NOT_FOUND) {
                ui_update_nina_status("No active session");
                ui_set_nina_session_active(false);
                ui_update_nina_paused(false);
                prev_image_count = -1;
            } else {
                ui_update_nina_status("NINA offline");
                ui_set_nina_session_active(false);
                ui_update_nina_paused(false);
                prev_image_count = -1;
            }

            // Fetch dome status for home screen
            err = nina_fetch_dome_status(&dome_status);
            if (err == ESP_OK) {
                ui_update_dome_status(&dome_status);
            }
        } else {
            ui_update_nina_status("WiFi disconnected");
        }

        // Wait for next poll interval, but check for manual refresh every second
        for (int s = CONFIG_NINA_POLL_INTERVAL_S; s > 0; s--) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            if (nina_refresh_flag) {
                nina_refresh_flag = false;
                ESP_LOGI(TAG, "NINA manual refresh triggered");
                break;
            }
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "CloudWatcher Waveshare Display v0.4.4");
    ESP_LOGI(TAG, "Free heap: %lu, PSRAM: %lu",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // Allocate graph data from PSRAM
    graph_data = heap_caps_calloc(CW_GRAPH_SERIES_COUNT, sizeof(cw_graph_data_t),
                                   MALLOC_CAP_SPIRAM);
    if (!graph_data) {
        ESP_LOGE(TAG, "Failed to allocate graph data in PSRAM!");
    }

    // Initialize display and LVGL (BSP handles LCD + touch setup)
    ESP_ERROR_CHECK(display_driver_init());

    // Initialize the UI screens
    ESP_ERROR_CHECK(ui_init());

    // Initialize WiFi
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(wifi_manager_start());

    // Initialize CloudWatcher HTTP client
    ESP_ERROR_CHECK(cw_client_init());

    // Initialize NINA API client (HW JPEG decoder + buffers)
    ESP_ERROR_CHECK(nina_client_init());

    // Launch the data polling task (runs on any core)
    // Stack needs to be large enough for LVGL chart rendering (float formatting + 400 point updates)
    xTaskCreate(cw_poll_task, "cw_poll", 16384, NULL, 5, NULL);

    // Launch NINA image polling task (needs large stack for HTTP client + JSON parsing)
    xTaskCreate(nina_poll_task, "nina_poll", 16384, NULL, 4, NULL);

    ESP_LOGI(TAG, "Initialization complete. CW polling %s:%d every %ds, NINA polling %s:%d every %ds",
             CONFIG_CW_HOST_IP, CONFIG_CW_HOST_PORT, CONFIG_CW_POLL_INTERVAL_S,
             CONFIG_NINA_HOST_IP, CONFIG_NINA_HOST_PORT, CONFIG_NINA_POLL_INTERVAL_S);
}
