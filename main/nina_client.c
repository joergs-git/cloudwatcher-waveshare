// NINA Advanced API client - fetches latest image + metadata
// Connects to ninaAPI plugin (by Christian Palm) running on the imaging PC
// Uses software JPEG decoder (TJPGD) with auto-scaling to fit the 720x670 display
// v0.4.2

#include "nina_client.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "jpeg_decoder.h"
#include "driver/jpeg_decode.h"

static const char *TAG = "nina_client";

// Buffer sizes
#define JPEG_INPUT_BUF_SIZE   (512 * 1024)   // 512KB for resized JPEG
#define RGB_OUTPUT_BUF_SIZE   (960 * 660 * 2) // ~1.2MB RGB565, fits NINA's typical resized output
#define META_BUF_SIZE         4096            // JSON metadata response

// Display constraints for auto-scaling
// Allow slightly larger than display - LVGL clips overflow automatically
#define MAX_DISPLAY_WIDTH     960
#define MAX_DISPLAY_HEIGHT    660

// Persistent buffers (allocated once during init, reused for each fetch)
static uint8_t *jpeg_input_buf = NULL;
static size_t   jpeg_input_alloc_size = 0;
static uint8_t *rgb_output_buf = NULL;
static size_t   rgb_output_alloc_size = 0;
static char    *meta_buf = NULL;

// Context for accumulating HTTP response data into a buffer
typedef struct {
    uint8_t *buf;
    size_t   buf_size;
    size_t   received;
} http_recv_ctx_t;

// HTTP event handler - accumulates response body chunks into context buffer
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_recv_ctx_t *ctx = (http_recv_ctx_t *)evt->user_data;
    if (!ctx) return ESP_OK;

    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (ctx->received + evt->data_len <= ctx->buf_size) {
            memcpy(ctx->buf + ctx->received, evt->data, evt->data_len);
            ctx->received += evt->data_len;
        } else {
            ESP_LOGW(TAG, "Response buffer overflow: %d + %d > %d",
                     (int)ctx->received, evt->data_len, (int)ctx->buf_size);
        }
    }
    return ESP_OK;
}

esp_err_t nina_client_init(void)
{
    // Allocate JPEG input buffer from PSRAM
    jpeg_input_buf = heap_caps_malloc(JPEG_INPUT_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!jpeg_input_buf) {
        ESP_LOGE(TAG, "Failed to allocate JPEG input buffer");
        return ESP_ERR_NO_MEM;
    }
    jpeg_input_alloc_size = JPEG_INPUT_BUF_SIZE;

    // Allocate RGB565 output buffer from PSRAM
    rgb_output_buf = heap_caps_malloc(RGB_OUTPUT_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!rgb_output_buf) {
        ESP_LOGE(TAG, "Failed to allocate RGB output buffer");
        return ESP_ERR_NO_MEM;
    }
    rgb_output_alloc_size = RGB_OUTPUT_BUF_SIZE;

    // Allocate metadata JSON response buffer
    meta_buf = heap_caps_malloc(META_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!meta_buf) {
        ESP_LOGE(TAG, "Failed to allocate metadata buffer");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "NINA client initialized (SW JPEG, input: %dKB, output: %dKB)",
             (int)(jpeg_input_alloc_size / 1024), (int)(rgb_output_alloc_size / 1024));
    return ESP_OK;
}

// Fetch JSON from a NINA API endpoint, returns parsed cJSON object or NULL
static cJSON *fetch_json(const char *url)
{
    http_recv_ctx_t ctx = {
        .buf = (uint8_t *)meta_buf,
        .buf_size = META_BUF_SIZE - 1,
        .received = 0,
    };

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &ctx,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "JSON fetch failed: err=%s status=%d", esp_err_to_name(err), status);
        return NULL;
    }

    meta_buf[ctx.received] = '\0';
    cJSON *json = cJSON_Parse(meta_buf);
    if (!json) {
        ESP_LOGW(TAG, "JSON parse failed for: %.100s", meta_buf);
    }
    return json;
}

// Extract image metadata fields from the ninaAPI JSON response
static void parse_metadata(cJSON *json, nina_image_meta_t *meta)
{
    memset(meta, 0, sizeof(*meta));

    // ninaAPI wraps responses in "Response" - can be an object or array
    cJSON *response = cJSON_GetObjectItem(json, "Response");
    if (!response) {
        ESP_LOGW(TAG, "No 'Response' field in metadata JSON");
        return;
    }

    // If Response is an array, use the first (latest) element
    if (cJSON_IsArray(response)) {
        response = cJSON_GetArrayItem(response, 0);
        if (!response) {
            ESP_LOGW(TAG, "Response array is empty");
            return;
        }
    }

    if (!cJSON_IsObject(response)) {
        ESP_LOGW(TAG, "Response is not an object (type: %d)", response->type);
        return;
    }

    cJSON *item;
    if ((item = cJSON_GetObjectItem(response, "TargetName")) && cJSON_IsString(item)) {
        strncpy(meta->target_name, item->valuestring, sizeof(meta->target_name) - 1);
    }
    if ((item = cJSON_GetObjectItem(response, "Filter")) && cJSON_IsString(item)) {
        strncpy(meta->filter, item->valuestring, sizeof(meta->filter) - 1);
    }
    if ((item = cJSON_GetObjectItem(response, "CameraName")) && cJSON_IsString(item)) {
        strncpy(meta->camera_name, item->valuestring, sizeof(meta->camera_name) - 1);
    }
    if ((item = cJSON_GetObjectItem(response, "TelescopeName")) && cJSON_IsString(item)) {
        strncpy(meta->telescope_name, item->valuestring, sizeof(meta->telescope_name) - 1);
    }
    if ((item = cJSON_GetObjectItem(response, "ExposureTime")) && cJSON_IsNumber(item)) {
        meta->exposure_time = (float)item->valuedouble;
    }
    if ((item = cJSON_GetObjectItem(response, "Stars")) && cJSON_IsNumber(item)) {
        meta->stars = item->valueint;
    }
    if ((item = cJSON_GetObjectItem(response, "HFR")) && cJSON_IsNumber(item)) {
        meta->hfr = (float)item->valuedouble;
    }
    if ((item = cJSON_GetObjectItem(response, "Gain")) && cJSON_IsNumber(item)) {
        meta->gain = item->valueint;
    }
    if ((item = cJSON_GetObjectItem(response, "Offset")) && cJSON_IsNumber(item)) {
        meta->offset = item->valueint;
    }
    // Date field - extract readable time portion (e.g. "2025-03-06T22:15:30" -> "22:15:30")
    if ((item = cJSON_GetObjectItem(response, "Date")) && cJSON_IsString(item)) {
        const char *t = strchr(item->valuestring, 'T');
        if (t) {
            // Copy time portion after 'T', truncate at fractional seconds or timezone
            snprintf(meta->timestamp, sizeof(meta->timestamp), "%.*s",
                     8, t + 1); // "HH:MM:SS"
        } else {
            strncpy(meta->timestamp, item->valuestring, sizeof(meta->timestamp) - 1);
        }
    }

    meta->valid = true;
}

// Determine the best TJPGD scale factor to fit the image on display
static int pick_jpeg_scale(uint32_t width, uint32_t height)
{
    // TJPGD scales: 0=1:1, 1=1:2, 2=1:4, 3=1:8
    for (int scale = 0; scale <= 3; scale++) {
        uint32_t div = (1 << scale);
        uint32_t sw = width / div;
        uint32_t sh = height / div;
        if (sw <= MAX_DISPLAY_WIDTH && sh <= MAX_DISPLAY_HEIGHT) {
            return scale;
        }
    }
    return 3; // 1/8 as fallback
}

esp_err_t nina_fetch_image(nina_image_data_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;

    memset(out, 0, sizeof(*out));
    out->rgb_buf = rgb_output_buf;

    char url[256];

    // Step 1: Check if NINA has any captured images
    snprintf(url, sizeof(url), "http://%s:%d/v2/api/image-history?count=true",
             CONFIG_NINA_HOST_IP, CONFIG_NINA_HOST_PORT);

    cJSON *count_json = fetch_json(url);
    if (!count_json) {
        ESP_LOGW(TAG, "NINA API not reachable at %s:%d",
                 CONFIG_NINA_HOST_IP, CONFIG_NINA_HOST_PORT);
        return ESP_FAIL;
    }

    cJSON *count_resp = cJSON_GetObjectItem(count_json, "Response");
    int image_count = 0;
    if (count_resp && cJSON_IsNumber(count_resp)) {
        image_count = count_resp->valueint;
    }
    cJSON_Delete(count_json);

    if (image_count <= 0) {
        ESP_LOGI(TAG, "No images in NINA history");
        return ESP_ERR_NOT_FOUND;
    }

    // Step 2: Fetch metadata for the latest image (omitting index returns last)
    snprintf(url, sizeof(url), "http://%s:%d/v2/api/image-history",
             CONFIG_NINA_HOST_IP, CONFIG_NINA_HOST_PORT);

    cJSON *meta_json = fetch_json(url);
    if (meta_json) {
        parse_metadata(meta_json, &out->meta);
        out->meta.image_count = image_count;
        cJSON_Delete(meta_json);
    }

    // Step 3: Download the latest image as resized JPEG
    // NINA scales maintaining aspect ratio within the given size bounds
    snprintf(url, sizeof(url),
             "http://%s:%d/v2/api/prepared-image?resize=true&size=%dx%d&quality=70",
             CONFIG_NINA_HOST_IP, CONFIG_NINA_HOST_PORT,
             MAX_DISPLAY_WIDTH, MAX_DISPLAY_HEIGHT);

    http_recv_ctx_t jpeg_ctx = {
        .buf = jpeg_input_buf,
        .buf_size = jpeg_input_alloc_size,
        .received = 0,
    };

    esp_http_client_config_t img_config = {
        .url = url,
        .event_handler = http_event_handler,
        .user_data = &jpeg_ctx,
        .timeout_ms = 15000,
        .buffer_size = 16384,
    };

    esp_http_client_handle_t client = esp_http_client_init(&img_config);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200 || jpeg_ctx.received == 0) {
        ESP_LOGW(TAG, "Image fetch failed: err=%s status=%d received=%d",
                 esp_err_to_name(err), status, (int)jpeg_ctx.received);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "JPEG received: %d bytes", (int)jpeg_ctx.received);

    // Step 4: Parse JPEG header to get dimensions for scale calculation
    jpeg_decode_picture_info_t pic_info;
    err = jpeg_decoder_get_info(jpeg_input_buf, jpeg_ctx.received, &pic_info);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to parse JPEG header: %s", esp_err_to_name(err));
        return ESP_OK;
    }

    int scale = pick_jpeg_scale(pic_info.width, pic_info.height);
    uint32_t scaled_w = pic_info.width / (1 << scale);
    uint32_t scaled_h = pic_info.height / (1 << scale);

    ESP_LOGI(TAG, "JPEG %lux%lu -> scale 1/%d -> %lux%lu",
             (unsigned long)pic_info.width, (unsigned long)pic_info.height,
             (1 << scale), (unsigned long)scaled_w, (unsigned long)scaled_h);

    // Check output buffer is large enough
    size_t needed = scaled_w * scaled_h * 2;
    if (needed > rgb_output_alloc_size) {
        ESP_LOGW(TAG, "Output buffer too small: need %d, have %d",
                 (int)needed, (int)rgb_output_alloc_size);
        return ESP_OK;
    }

    // Step 5: Decode JPEG to RGB565 using software decoder (TJPGD) with scaling
    esp_jpeg_image_cfg_t jpeg_cfg = {
        .indata = jpeg_input_buf,
        .indata_size = jpeg_ctx.received,
        .outbuf = rgb_output_buf,
        .outbuf_size = rgb_output_alloc_size,
        .out_format = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale = scale,
        .flags = {
            .swap_color_bytes = 0,  // MIPI-DSI display uses native byte order
        },
    };

    esp_jpeg_image_output_t jpeg_out;
    err = esp_jpeg_decode(&jpeg_cfg, &jpeg_out);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SW JPEG decode failed: %s", esp_err_to_name(err));
        return ESP_OK;
    }

    out->width = jpeg_out.width;
    out->height = jpeg_out.height;
    out->buf_size = jpeg_out.width * jpeg_out.height * 2;
    out->image_valid = true;

    ESP_LOGI(TAG, "Image decoded: %lux%lu (%lu bytes RGB565)",
             (unsigned long)out->width, (unsigned long)out->height, (unsigned long)out->buf_size);

    return ESP_OK;
}

esp_err_t nina_fetch_dome_status(nina_dome_status_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;

    memset(out, 0, sizeof(*out));

    // Query AstroShell dome controller directly via /?$S
    // Returns plain text: "OPEN" or "CLOSED"
    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/?$S",
             CONFIG_DOME_CONTROLLER_IP, CONFIG_DOME_CONTROLLER_PORT);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    // Disable redirect following (Arduino returns 303 for commands)
    esp_http_client_set_redirection(client);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Dome controller unreachable: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    esp_http_client_fetch_headers(client);
    char buf[64] = {0};
    int read_len = esp_http_client_read(client, buf, sizeof(buf) - 1);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (read_len <= 0) {
        ESP_LOGW(TAG, "Dome controller: empty response");
        return ESP_FAIL;
    }
    buf[read_len] = '\0';

    // Trim whitespace/newlines from response
    int len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' ')) {
        buf[--len] = '\0';
    }

    ESP_LOGI(TAG, "Dome controller status: '%s'", buf);

    snprintf(out->shutter_status, sizeof(out->shutter_status), "%s", buf);
    out->shutter_open = (strcasecmp(buf, "OPEN") == 0);
    out->connected = true;
    out->valid = true;

    return ESP_OK;
}
