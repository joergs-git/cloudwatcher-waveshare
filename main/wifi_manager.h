#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"
#include "esp_event.h"

// Event base for wifi manager events
ESP_EVENT_DECLARE_BASE(WIFI_MANAGER_EVENT);

typedef enum {
    WIFI_MANAGER_EVENT_CONNECTED,
    WIFI_MANAGER_EVENT_DISCONNECTED,
} wifi_manager_event_t;

// Initialize WiFi in station mode with auto-reconnect
esp_err_t wifi_manager_init(void);

// Start WiFi connection (non-blocking, fires events)
esp_err_t wifi_manager_start(void);

// Check if currently connected
bool wifi_manager_is_connected(void);

#endif // WIFI_MANAGER_H
