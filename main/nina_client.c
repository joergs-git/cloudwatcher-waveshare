// NINA Advanced API client - fetches latest image + metadata
// Connects to ninaAPI plugin (by Christian Palm) running on the imaging PC
// Uses ESP32-P4 hardware JPEG decoder for image processing
// v0.4.0

#include "nina_client.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "driver/jpeg_decode.h"

static const char *TAG = "nina_client";

// Buffer sizes
#define JPEG_INPUT_BUF_SIZE   (400 * 1024)   // 400KB for compressed JPEG
#define RGB_OUTPUT_BUF_SIZE   (NINA_IMAGE_WIDTH * NINA_IMAGE_HEIGHT * 2 + 8192) // RGB565 + HW alignment padding
#define META_BUF_SIZE         4096           // JSON metadata response

// Hardware JPEG decoder handle
static jpeg_decoder_handle_t jpeg_decoder = NULL;

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
    // Create ESP32-P4 hardware JPEG decoder engine
    jpeg_decode_engine_cfg_t engine_cfg = {
        .intr_priority = 0,
        .timeout_ms = 5000,
    };
    esp_err_t err = jpeg_new_decoder_engine(&engine_cfg, &jpeg_decoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create HW JPEG decoder: %s", esp_err_to_name(err));
        return err;
    }

    // Allocate JPEG input buffer (DMA-aligned for hardware decoder)
    jpeg_decode_memory_alloc_cfg_t in_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER,
    };
    jpeg_input_buf = jpeg_alloc_decoder_mem(JPEG_INPUT_BUF_SIZE, &in_cfg, &jpeg_input_alloc_size);
    if (!jpeg_input_buf) {
        ESP_LOGE(TAG, "Failed to allocate JPEG input buffer");
        return ESP_ERR_NO_MEM;
    }

    // Allocate RGB565 output buffer (DMA-aligned for hardware decoder)
    jpeg_decode_memory_alloc_cfg_t out_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };
    rgb_output_buf = jpeg_alloc_decoder_mem(RGB_OUTPUT_BUF_SIZE, &out_cfg, &rgb_output_alloc_size);
    if (!rgb_output_buf) {
        ESP_LOGE(TAG, "Failed to allocate RGB output buffer");
        return ESP_ERR_NO_MEM;
    }

    // Allocate metadata JSON response buffer
    meta_buf = heap_caps_malloc(META_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!meta_buf) {
        ESP_LOGE(TAG, "Failed to allocate metadata buffer");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "NINA client initialized (HW JPEG, input: %dKB, output: %dKB)",
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

    // ninaAPI wraps all responses in a "Response" object
    cJSON *response = cJSON_GetObjectItem(json, "Response");
    if (!response || !cJSON_IsObject(response)) return;

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

    meta->valid = true;
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
    snprintf(url, sizeof(url),
             "http://%s:%d/v2/api/prepared-image?resize=true&size=%dx%d&quality=70",
             CONFIG_NINA_HOST_IP, CONFIG_NINA_HOST_PORT,
             NINA_IMAGE_WIDTH, NINA_IMAGE_HEIGHT);

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
        // Metadata may still be valid, return OK so overlay can show info
        return ESP_OK;
    }

    ESP_LOGI(TAG, "JPEG received: %d bytes", (int)jpeg_ctx.received);

    // Step 4: Parse JPEG header to get actual image dimensions
    jpeg_decode_picture_info_t pic_info;
    err = jpeg_decoder_get_info(jpeg_input_buf, jpeg_ctx.received, &pic_info);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to parse JPEG header: %s", esp_err_to_name(err));
        return ESP_OK;
    }

    ESP_LOGI(TAG, "JPEG dimensions: %lux%lu",
             (unsigned long)pic_info.width, (unsigned long)pic_info.height);

    // Step 5: Decode JPEG to RGB565 using ESP32-P4 hardware decoder
    jpeg_decode_cfg_t decode_cfg = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
        .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
    };

    uint32_t out_size = 0;
    err = jpeg_decoder_process(jpeg_decoder, &decode_cfg,
                               jpeg_input_buf, jpeg_ctx.received,
                               rgb_output_buf, rgb_output_alloc_size, &out_size);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HW JPEG decode failed: %s", esp_err_to_name(err));
        return ESP_OK;
    }

    out->width = pic_info.width;
    out->height = pic_info.height;
    out->buf_size = out_size;
    out->image_valid = true;

    ESP_LOGI(TAG, "Image decoded: %lux%lu (%lu bytes RGB565)",
             (unsigned long)out->width, (unsigned long)out->height, (unsigned long)out_size);

    return ESP_OK;
}
