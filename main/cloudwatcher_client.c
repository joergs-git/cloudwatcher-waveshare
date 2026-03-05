// CloudWatcher Solo HTTP client - fetches and parses sensor data
// v0.1.0

#include "cloudwatcher_client.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "cw_client";

// Buffer for lastData.pl response (~500 bytes typical)
#define CURRENT_DATA_BUF_SIZE 2048

// Buffer for graphData.pl response (~200KB, allocated in PSRAM)
#define GRAPH_DATA_BUF_SIZE (256 * 1024)

// Series name mapping table: JavaScript name -> our enum
typedef struct {
    const char *today_name;
    const char *yesterday_name;
    cw_graph_series_t series;
} series_map_entry_t;

static const series_map_entry_t SERIES_MAP[] = {
    {"hoyNubes",        "ayerNubes",        CW_GRAPH_CLOUDS},
    {"hoyTemp",         "ayerTemp",         CW_GRAPH_TEMP},
    {"hoyHumedad",      "ayerHumedad",      CW_GRAPH_HUMIDITY},
    {"hoyLluvia",       "ayerLluvia",       CW_GRAPH_RAIN},
    {"hoyLuminosidad",  "ayerLuminosidad",  CW_GRAPH_LIGHT},
    {"hoyPresion",      "ayerPresion",      CW_GRAPH_PRESSURE},
};
#define SERIES_MAP_COUNT (sizeof(SERIES_MAP) / sizeof(SERIES_MAP[0]))

esp_err_t cw_client_init(void)
{
    ESP_LOGI(TAG, "CloudWatcher client initialized (host: %s:%d)",
             CONFIG_CW_HOST_IP, CONFIG_CW_HOST_PORT);
    return ESP_OK;
}

// Parse a key=value line into the data struct
static void parse_kv_line(const char *line, cw_current_data_t *data)
{
    char key[32];
    char val[64];

    // Split on '=' character
    const char *eq = strchr(line, '=');
    if (!eq) return;

    int key_len = eq - line;
    if (key_len <= 0 || key_len >= (int)sizeof(key)) return;

    strncpy(key, line, key_len);
    key[key_len] = '\0';

    const char *val_start = eq + 1;
    // Trim trailing whitespace/newline
    int val_len = strlen(val_start);
    while (val_len > 0 && (val_start[val_len - 1] == '\n' ||
                            val_start[val_len - 1] == '\r' ||
                            val_start[val_len - 1] == ' ')) {
        val_len--;
    }
    if (val_len <= 0 || val_len >= (int)sizeof(val)) return;
    strncpy(val, val_start, val_len);
    val[val_len] = '\0';

    // Match known keys
    if (strcmp(key, "clouds") == 0)        data->clouds = strtof(val, NULL);
    else if (strcmp(key, "temp") == 0)     data->temp = strtof(val, NULL);
    else if (strcmp(key, "rain") == 0)     data->rain = atoi(val);
    else if (strcmp(key, "lightmpsas") == 0) data->light_mpsas = strtof(val, NULL);
    else if (strcmp(key, "hum") == 0)      data->humidity = atoi(val);
    else if (strcmp(key, "dewp") == 0)     data->dew_point = strtof(val, NULL);
    else if (strcmp(key, "abspress") == 0) data->abs_pressure = strtof(val, NULL);
    else if (strcmp(key, "relpress") == 0) data->rel_pressure = strtof(val, NULL);
    else if (strcmp(key, "safe") == 0)     data->safe = (atoi(val) == 1);
    else if (strcmp(key, "switch") == 0)   data->switch_on = (atoi(val) == 1);
    else if (strcmp(key, "cloudsSafe") == 0)   data->clouds_safe = (atoi(val) == 1);
    else if (strcmp(key, "rainSafe") == 0)     data->rain_safe = (atoi(val) == 1);
    else if (strcmp(key, "lightSafe") == 0)    data->light_safe = (atoi(val) == 1);
    else if (strcmp(key, "windSafe") == 0)     data->wind_safe = (atoi(val) == 1);
    else if (strcmp(key, "humSafe") == 0)      data->hum_safe = (atoi(val) == 1);
    else if (strcmp(key, "pressureSafe") == 0) data->pressure_safe = (atoi(val) == 1);
}

esp_err_t cw_fetch_current(cw_current_data_t *data)
{
    if (!data) return ESP_ERR_INVALID_ARG;
    memset(data, 0, sizeof(*data));

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/cgi-bin/lastData.pl",
             CONFIG_CW_HOST_IP, CONFIG_CW_HOST_PORT);

    char *buf = malloc(CURRENT_DATA_BUF_SIZE);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate response buffer");
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        free(buf);
        return err;
    }

    esp_http_client_fetch_headers(client);
    int total_read = 0;

    // Read entire response
    while (total_read < CURRENT_DATA_BUF_SIZE - 1) {
        int read_len = esp_http_client_read(client, buf + total_read,
                                             CURRENT_DATA_BUF_SIZE - 1 - total_read);
        if (read_len <= 0) break;
        total_read += read_len;
    }
    buf[total_read] = '\0';

    int status = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (status != 200) {
        ESP_LOGE(TAG, "HTTP status %d for lastData.pl", status);
        free(buf);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Received %d bytes from lastData.pl", total_read);

    // Parse line by line
    char *line = buf;
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next) *next = '\0';
        parse_kv_line(line, data);
        line = next ? next + 1 : NULL;
    }

    data->valid = true;
    free(buf);

    ESP_LOGI(TAG, "Current: clouds=%.1f temp=%.1f rain=%d safe=%d",
             data->clouds, data->temp, data->rain, data->safe);

    return ESP_OK;
}

// Find a series in our map by name, returns -1 if not found, sets *is_yesterday
static int find_series(const char *name, bool *is_yesterday)
{
    for (int i = 0; i < (int)SERIES_MAP_COUNT; i++) {
        if (strcmp(name, SERIES_MAP[i].today_name) == 0) {
            *is_yesterday = false;
            return SERIES_MAP[i].series;
        }
        if (strcmp(name, SERIES_MAP[i].yesterday_name) == 0) {
            *is_yesterday = true;
            return SERIES_MAP[i].series;
        }
    }
    return -1;
}

// Parse a single series block: { name:'seriesName', values:[[x,y],[x,y],...] }
// Returns pointer past the parsed block, or NULL on error
static const char *parse_series_block(const char *p, cw_graph_data_t graphs[CW_GRAPH_SERIES_COUNT])
{
    // Find name field: name:'...'
    const char *name_start = strstr(p, "name:'");
    if (!name_start) return NULL;
    name_start += 6; // skip "name:'"

    const char *name_end = strchr(name_start, '\'');
    if (!name_end || (name_end - name_start) > 30) return NULL;

    char name[32];
    int name_len = name_end - name_start;
    memcpy(name, name_start, name_len);
    name[name_len] = '\0';

    // Check if this is a series we care about
    bool is_yesterday = false;
    int series_idx = find_series(name, &is_yesterday);

    // Find values array start
    const char *vals = strstr(name_end, "values:[");
    if (!vals) return NULL;
    vals += 8; // skip "values:["

    // Find end of this values array - the matching ']'
    // We need to handle nested brackets [[x,y],[x,y],...]
    const char *vals_end = vals;
    int bracket_depth = 1;
    while (*vals_end && bracket_depth > 0) {
        if (*vals_end == '[') bracket_depth++;
        else if (*vals_end == ']') bracket_depth--;
        if (bracket_depth > 0) vals_end++;
    }

    // Parse data points only if we track this series
    if (series_idx >= 0 && series_idx < CW_GRAPH_SERIES_COUNT) {
        cw_graph_data_t *g = &graphs[series_idx];
        float *x_arr = is_yesterday ? g->yesterday_x : g->today_x;
        float *y_arr = is_yesterday ? g->yesterday : g->today;
        int *count = is_yesterday ? &g->yesterday_count : &g->today_count;
        *count = 0;

        const char *cursor = vals;
        while (cursor < vals_end && *count < CW_GRAPH_MAX_POINTS) {
            // Find next inner array [x,y]
            const char *inner_start = strchr(cursor, '[');
            if (!inner_start || inner_start >= vals_end) break;

            float x = strtof(inner_start + 1, (char **)&cursor);
            // Skip comma
            while (cursor < vals_end && (*cursor == ',' || *cursor == ' ')) cursor++;
            float y = strtof(cursor, (char **)&cursor);

            x_arr[*count] = x;
            y_arr[*count] = y;
            (*count)++;

            // Advance past closing bracket
            cursor = strchr(cursor, ']');
            if (!cursor) break;
            cursor++;
        }

        ESP_LOGD(TAG, "Parsed series '%s': %d points", name, *count);
    }

    // Return pointer past this block's closing brace
    const char *block_end = strchr(vals_end, '}');
    return block_end ? block_end + 1 : vals_end;
}

esp_err_t cw_fetch_graphs(cw_graph_data_t graphs[CW_GRAPH_SERIES_COUNT])
{
    if (!graphs) return ESP_ERR_INVALID_ARG;
    memset(graphs, 0, sizeof(cw_graph_data_t) * CW_GRAPH_SERIES_COUNT);

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/cgi-bin/graphData.pl",
             CONFIG_CW_HOST_IP, CONFIG_CW_HOST_PORT);

    // Allocate large buffer from PSRAM for the ~200KB response
    char *buf = heap_caps_malloc(GRAPH_DATA_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate PSRAM buffer for graph data");
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 30000,
        .buffer_size = 4096,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed for graphData: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        heap_caps_free(buf);
        return err;
    }

    esp_http_client_fetch_headers(client);
    int total_read = 0;

    while (total_read < GRAPH_DATA_BUF_SIZE - 1) {
        int read_len = esp_http_client_read(client, buf + total_read,
                                             GRAPH_DATA_BUF_SIZE - 1 - total_read);
        if (read_len <= 0) break;
        total_read += read_len;
    }
    buf[total_read] = '\0';

    int status = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (status != 200) {
        ESP_LOGE(TAG, "HTTP status %d for graphData.pl", status);
        heap_caps_free(buf);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Received %d bytes from graphData.pl", total_read);

    // Log first 200 chars of response for debugging
    char preview[201];
    int preview_len = total_read < 200 ? total_read : 200;
    memcpy(preview, buf, preview_len);
    preview[preview_len] = '\0';
    ESP_LOGI(TAG, "Graph response starts with: %.200s", preview);

    // Parse: response format varies, look for first series block
    // Formats seen: "allValues= [{...}]" or "allValues=[{...}]" or just "[{...}]"
    const char *p = strstr(buf, "name:");
    if (!p) {
        // Try alternate format: look for "name:" anywhere
        p = strstr(buf, "name :");
    }
    if (!p) {
        ESP_LOGE(TAG, "Could not find any series data in graphData response");
        heap_caps_free(buf);
        return ESP_FAIL;
    }
    // Back up to find the opening '{' of this block
    while (p > buf && *p != '{') p--;
    if (*p != '{') {
        ESP_LOGE(TAG, "Could not find start of first series block");
        heap_caps_free(buf);
        return ESP_FAIL;
    }

    // Iterate through each series block
    while (p && *p) {
        const char *block_start = strchr(p, '{');
        if (!block_start) break;

        p = parse_series_block(block_start, graphs);
        if (!p) break;
    }

    heap_caps_free(buf);

    // Log parsed series counts
    const char *series_names[] = {"clouds", "temp", "humidity", "rain", "light", "pressure"};
    for (int i = 0; i < CW_GRAPH_SERIES_COUNT; i++) {
        ESP_LOGI(TAG, "Graph %s: today=%d pts, yesterday=%d pts",
                 series_names[i], graphs[i].today_count, graphs[i].yesterday_count);
    }

    return ESP_OK;
}

const char *cw_cloud_state_str(float sky_temp)
{
    if (sky_temp < CW_CLOUD_CLEAR) return "CLEAR";
    if (sky_temp < CW_CLOUD_CLOUDY) return "CLOUDY";
    return "OVERCAST";
}

const char *cw_rain_state_str(int rain_val)
{
    if (rain_val > CW_RAIN_DRY) return "DRY";
    if (rain_val > CW_RAIN_WET) return "WET";
    return "RAIN";
}

const char *cw_light_state_str(float mpsas)
{
    // Dark > 17, Light > 13, Very Light > 10
    if (mpsas > CW_LIGHT_DARK) return "DARK";
    if (mpsas > 13.0f) return "LIGHT";
    return "BRIGHT";
}
