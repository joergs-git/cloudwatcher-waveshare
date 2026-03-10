// NINA Advanced API client - fetches latest image + metadata
// Uses ESP32-P4 hardware JPEG decoder for fast image decoding
// v0.4.2

#ifndef NINA_CLIENT_H
#define NINA_CLIENT_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// Maximum image dimensions requested from NINA API
#define NINA_IMAGE_WIDTH  640
#define NINA_IMAGE_HEIGHT 640

// Metadata from the latest captured image
typedef struct {
    char     target_name[64];
    char     filter[32];
    char     camera_name[64];
    char     telescope_name[64];
    float    exposure_time;   // seconds
    int      stars;
    float    hfr;
    int      gain;
    int      offset;
    int      image_count;     // total images in history
    char     timestamp[32];   // capture date/time string
    bool     valid;           // metadata parsed successfully
} nina_image_meta_t;

// Full image data: decoded pixels + metadata
typedef struct {
    uint8_t          *rgb_buf;      // RGB565 decoded pixels (persistent buffer in PSRAM)
    uint32_t          width;
    uint32_t          height;
    uint32_t          buf_size;     // decoded buffer size in bytes
    nina_image_meta_t meta;
    bool              image_valid;  // image decoded successfully
} nina_image_data_t;

// Dome status
typedef struct {
    bool connected;
    bool shutter_open;      // true = open, false = closed/unknown
    char shutter_status[16]; // "Open", "Closed", "Opening", "Closing" etc.
    bool at_park;
    bool valid;
} nina_dome_status_t;

// Initialize NINA client (allocates buffers)
esp_err_t nina_client_init(void);

// Fetch latest image + metadata from NINA API
// Returns ESP_OK on success (check image_valid and meta.valid for details)
// Returns ESP_ERR_NOT_FOUND if NINA is reachable but has no images
// Returns ESP_FAIL if NINA API is unreachable
esp_err_t nina_fetch_image(nina_image_data_t *out);

// Fetch dome status from NINA API
// Returns ESP_OK on success, ESP_FAIL if unreachable
esp_err_t nina_fetch_dome_status(nina_dome_status_t *out);

// Send dome open or close command to AstroShell controller
// is_open: true = open all shutters, false = close all shutters
// Returns ESP_OK if the HTTP request was sent successfully
esp_err_t dome_send_command(bool is_open);

// Send a Pushover push notification (requires valid config)
// Returns ESP_OK on success
esp_err_t pushover_send(const char *title, const char *message);

#endif // NINA_CLIENT_H
