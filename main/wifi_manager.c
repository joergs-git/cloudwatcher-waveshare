// WiFi station manager for ESP32-P4 with ESP-Hosted co-processor
// WiFi is provided by ESP32-C6 slave chip via SDIO
// v0.1.0

#include "wifi_manager.h"

#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

static const char *TAG = "wifi_mgr";

ESP_EVENT_DEFINE_BASE(WIFI_MANAGER_EVENT);

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static SemaphoreHandle_t s_hosted_ready;

static int s_retry_count = 0;
#define MAX_RETRY 10
#define RETRY_DELAY_MS 5000

// ESP-Hosted event handler: tracks co-processor transport state
static void hosted_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    if (event_id == ESP_HOSTED_EVENT_TRANSPORT_UP) {
        ESP_LOGI(TAG, "ESP-Hosted transport UP");
        xSemaphoreGive(s_hosted_ready);
    } else if (event_id == ESP_HOSTED_EVENT_TRANSPORT_DOWN) {
        ESP_LOGW(TAG, "ESP-Hosted transport DOWN");
    } else if (event_id == ESP_HOSTED_EVENT_CP_INIT) {
        ESP_LOGI(TAG, "Co-processor initialized");
    }
}

// WiFi + IP event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi STA started, connecting...");
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
                esp_event_post(WIFI_MANAGER_EVENT, WIFI_MANAGER_EVENT_DISCONNECTED,
                               NULL, 0, pdMS_TO_TICKS(100));

                if (s_retry_count < MAX_RETRY) {
                    s_retry_count++;
                    ESP_LOGW(TAG, "Disconnected, retry %d/%d in %dms",
                             s_retry_count, MAX_RETRY, RETRY_DELAY_MS);
                    vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
                    esp_wifi_connect();
                } else {
                    ESP_LOGE(TAG, "Max retries reached, will keep trying every 30s");
                    vTaskDelay(pdMS_TO_TICKS(30000));
                    s_retry_count = 0;
                    esp_wifi_connect();
                }
                break;

            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        esp_event_post(WIFI_MANAGER_EVENT, WIFI_MANAGER_EVENT_CONNECTED,
                       NULL, 0, pdMS_TO_TICKS(100));
    }
}

esp_err_t wifi_manager_init(void)
{
    // Initialize NVS (required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_wifi_event_group = xEventGroupCreate();
    s_hosted_ready = xSemaphoreCreateBinary();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Register ESP-Hosted event handler to know when transport is up
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        ESP_HOSTED_EVENT, ESP_EVENT_ANY_ID, &hosted_event_handler, NULL, NULL));

    // Initialize ESP-Hosted: sets up SDIO communication with ESP32-C6
    ESP_LOGI(TAG, "Initializing ESP-Hosted...");
    esp_hosted_init();
    esp_hosted_connect_to_slave();

    // Wait for the co-processor transport to come up
    ESP_LOGI(TAG, "Waiting for ESP-Hosted transport...");
    if (xSemaphoreTake(s_hosted_ready, pdMS_TO_TICKS(30000)) != pdTRUE) {
        ESP_LOGE(TAG, "Timeout waiting for ESP-Hosted transport!");
        return ESP_ERR_TIMEOUT;
    }

    // Create default station network interface
    esp_netif_create_default_wifi_sta();

    // Register WiFi and IP event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // Initialize WiFi (via hosted remote)
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Configure station mode with credentials from menuconfig
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_CW_WIFI_SSID,
            .password = CONFIG_CW_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_LOGI(TAG, "WiFi manager initialized (SSID: %s)", CONFIG_CW_WIFI_SSID);
    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    ESP_LOGI(TAG, "Starting WiFi...");
    return esp_wifi_start();
}

bool wifi_manager_is_connected(void)
{
    if (!s_wifi_event_group) return false;
    return (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) != 0;
}
