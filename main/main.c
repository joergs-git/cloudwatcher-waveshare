// CloudWatcher Waveshare Display - Main Entry Point
// Displays AAG CloudWatcher Solo sensor data on Waveshare ESP32-P4 Touch LCD 4B
// v0.4.0

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_sntp.h"

#include "wifi_manager.h"
#include "cloudwatcher_client.h"
#include "display_driver.h"
#include "ui_main.h"

static const char *TAG = "main";

// Graph data allocated in PSRAM (large arrays: ~108KB for 6 series)
static cw_graph_data_t *graph_data = NULL;

// Initialize SNTP for time synchronization
static void init_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    // Set timezone to Europe/Berlin (CET-1CEST with DST transitions)
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

// Get current time as HH:MM string
static void get_time_str(char *buf, size_t len)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    if (timeinfo.tm_year > (2020 - 1900)) {
        // Time is synced (year > 2020)
        strftime(buf, len, "%H:%M", &timeinfo);
    } else {
        snprintf(buf, len, "--:--");
    }
}

// Data polling task: fetches sensor data and allsky image on schedule
static void cw_poll_task(void *arg)
{
    // Wait for WiFi connection before starting
    ESP_LOGI(TAG, "Poll task waiting for WiFi...");
    while (!wifi_manager_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "WiFi connected, starting data polling");

    // Start SNTP after WiFi is up
    init_sntp();

    cw_current_data_t current_data;
    int poll_counter = 0;
    int allsky_counter = 0;
    const int current_interval_s = CONFIG_CW_POLL_INTERVAL_S;
    const int graph_interval_s = CONFIG_CW_GRAPH_POLL_INTERVAL_S;
    const int allsky_interval_s = CONFIG_CW_ALLSKY_POLL_INTERVAL_S;
    const int graph_every_n = graph_interval_s / current_interval_s;
    const int allsky_every_n = allsky_interval_s / current_interval_s;

    // Initial fetch of all data
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

    // Initial allsky fetch
    ui_update_allsky();

    while (1) {
        // Countdown display between polls, update time every second
        for (int s = current_interval_s; s > 0; s--) {
            // Update clock
            char time_str[16];
            get_time_str(time_str, sizeof(time_str));
            ui_update_time(time_str);

            ui_update_countdown(s);
            vTaskDelay(pdMS_TO_TICKS(1000));
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

        // Periodically fetch allsky keogram
        allsky_counter++;
        if (allsky_counter >= allsky_every_n) {
            allsky_counter = 0;
            ESP_LOGI(TAG, "Fetching allsky keogram...");
            ui_update_allsky();
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "CloudWatcher Waveshare Display v0.4.0");
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

    // Launch the data polling task
    // Stack needs to be large enough for LVGL chart rendering + JPEG decode
    xTaskCreate(cw_poll_task, "cw_poll", 16384, NULL, 5, NULL);

    ESP_LOGI(TAG, "Initialization complete. Polling %s:%d every %ds",
             CONFIG_CW_HOST_IP, CONFIG_CW_HOST_PORT, CONFIG_CW_POLL_INTERVAL_S);
}
