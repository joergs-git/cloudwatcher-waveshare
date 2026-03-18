// Meteoblue Forecast API client - fetches hourly cloud cover predictions
// Uses basic-1h + clouds-1h packages for totalcloudcover data
// Supports signed API keys (MD5 signature validation)

#include "meteoblue_client.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "mbedtls/md5.h"

static const char *TAG = "mb_client";

// Meteoblue basic-1h response can be ~50-150KB
#define MB_RESPONSE_BUF_SIZE (192 * 1024)

esp_err_t mb_client_init(void)
{
    ESP_LOGI(TAG, "Meteoblue client initialized (lat=%s, lon=%s, alt=%d)",
             CONFIG_MB_LATITUDE, CONFIG_MB_LONGITUDE, CONFIG_MB_ALTITUDE);
    return ESP_OK;
}

// Parse ISO-like timestamp "YYYY-MM-DD HH:MM" to time_t (UTC)
static time_t parse_mb_time(const char *str)
{
    struct tm t = {0};
    // Format: "2024-01-15 14:00"
    if (sscanf(str, "%d-%d-%d %d:%d",
               &t.tm_year, &t.tm_mon, &t.tm_mday,
               &t.tm_hour, &t.tm_min) != 5) {
        return 0;
    }
    t.tm_year -= 1900;
    t.tm_mon -= 1;
    // mktime interprets as local time, but Meteoblue returns local time for the location
    return mktime(&t);
}

// Map cloud cover percentage to sky-temperature equivalent
static float cloud_cover_to_sky_temp(int cloud_pct)
{
    float pct = (float)cloud_pct;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return MB_SKY_TEMP_MIN + (pct / 100.0f) * (MB_SKY_TEMP_MAX - MB_SKY_TEMP_MIN);
}

esp_err_t mb_fetch_forecast(mb_forecast_data_t *data)
{
    if (!data) return ESP_ERR_INVALID_ARG;
    memset(data, 0, sizeof(*data));

    // Build API path+query (without domain, needed for signature)
    char path_query[256];
    snprintf(path_query, sizeof(path_query),
             "/packages/basic-15min_clouds-15min"
             "?apikey=%s&lat=%s&lon=%s&asl=%d&format=json",
             CONFIG_MB_API_KEY, CONFIG_MB_LATITUDE, CONFIG_MB_LONGITUDE,
             CONFIG_MB_ALTITUDE);

    // Compute MD5 signature: sig = MD5(path_query + "&secret=" + secret)
    char sig_input[384];
    snprintf(sig_input, sizeof(sig_input), "%s&secret=%s", path_query, CONFIG_MB_API_SECRET);

    unsigned char md5_hash[16];
    mbedtls_md5((const unsigned char *)sig_input, strlen(sig_input), md5_hash);

    char sig_hex[33];
    for (int i = 0; i < 16; i++) {
        snprintf(sig_hex + i * 2, 3, "%02x", md5_hash[i]);
    }

    // Build full URL with signature
    char url[384];
    snprintf(url, sizeof(url), "https://my.meteoblue.com%s&sig=%s", path_query, sig_hex);
    ESP_LOGI(TAG, "Fetching: %s", url);

    // Allocate response buffer from PSRAM
    char *buf = heap_caps_malloc(MB_RESPONSE_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate PSRAM buffer for Meteoblue response");
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 15000,
        .buffer_size = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTPS open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        heap_caps_free(buf);
        return err;
    }

    esp_http_client_fetch_headers(client);
    int total_read = 0;

    while (total_read < MB_RESPONSE_BUF_SIZE - 1) {
        int read_len = esp_http_client_read(client, buf + total_read,
                                             MB_RESPONSE_BUF_SIZE - 1 - total_read);
        if (read_len <= 0) break;
        total_read += read_len;
    }
    buf[total_read] = '\0';

    int status = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (status != 200) {
        ESP_LOGE(TAG, "Meteoblue HTTP status %d", status);
        heap_caps_free(buf);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Received %d bytes from Meteoblue", total_read);

    // Parse JSON response
    cJSON *root = cJSON_Parse(buf);
    heap_caps_free(buf);  // Free buffer immediately after parsing

    if (!root) {
        ESP_LOGE(TAG, "Failed to parse Meteoblue JSON");
        return ESP_FAIL;
    }

    // Navigate to data_xmin object (15-minute resolution)
    cJSON *data_1h = cJSON_GetObjectItem(root, "data_xmin");
    if (!data_1h) {
        ESP_LOGE(TAG, "No 'data_xmin' in Meteoblue response");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *time_arr = cJSON_GetObjectItem(data_1h, "time");
    cJSON *cloud_arr = cJSON_GetObjectItem(data_1h, "totalcloudcover");

    if (!cJSON_IsArray(time_arr) || !cJSON_IsArray(cloud_arr)) {
        ESP_LOGE(TAG, "Missing time or totalcloudcover arrays");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    int arr_size = cJSON_GetArraySize(time_arr);
    int cloud_size = cJSON_GetArraySize(cloud_arr);
    if (cloud_size < arr_size) arr_size = cloud_size;

    ESP_LOGI(TAG, "Meteoblue data: %d hourly entries", arr_size);

    // Determine the 24h window: now-12h to now+12h
    time_t now = time(NULL);
    time_t window_start = now - 12 * 3600;
    time_t window_end = now + 12 * 3600;

    // Iterate through all entries, collect those within our window
    data->count = 0;
    for (int i = 0; i < arr_size && data->count < MB_FORECAST_HOURS; i++) {
        cJSON *t_item = cJSON_GetArrayItem(time_arr, i);
        cJSON *c_item = cJSON_GetArrayItem(cloud_arr, i);

        if (!cJSON_IsString(t_item) || !cJSON_IsNumber(c_item)) continue;

        time_t ts = parse_mb_time(t_item->valuestring);
        if (ts == 0) continue;

        // Only include entries within our -12h to +12h window
        if (ts < window_start || ts > window_end) continue;

        int cloud_pct = (int)c_item->valuedouble;
        int idx = data->count;
        data->timestamps[idx] = ts;
        data->cloud_cover_pct[idx] = cloud_pct;
        data->sky_temp_equiv[idx] = cloud_cover_to_sky_temp(cloud_pct);
        data->count++;
    }

    cJSON_Delete(root);

    data->valid = (data->count > 0);

    ESP_LOGI(TAG, "Forecast: %d hours in window (now=%ld, start=%ld, end=%ld)",
             data->count, (long)now, (long)window_start, (long)window_end);

    if (data->count > 0) {
        ESP_LOGI(TAG, "First: cloud=%d%% sky_equiv=%.1fC, Last: cloud=%d%% sky_equiv=%.1fC",
                 data->cloud_cover_pct[0], data->sky_temp_equiv[0],
                 data->cloud_cover_pct[data->count - 1],
                 data->sky_temp_equiv[data->count - 1]);
    }

    return ESP_OK;
}
