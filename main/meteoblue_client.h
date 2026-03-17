#ifndef METEOBLUE_CLIENT_H
#define METEOBLUE_CLIENT_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

// Forecast window: 24 hourly points covering -12h to +12h from now
#define MB_FORECAST_HOURS 25

// Cloud cover to sky-temp mapping constants
// 0% cloud -> -25C (clear), 100% cloud -> +5C (overcast)
#define MB_SKY_TEMP_MIN  -25.0f
#define MB_SKY_TEMP_MAX    5.0f

typedef struct {
    float  sky_temp_equiv[MB_FORECAST_HOURS];  // Cloud cover mapped to sky temp scale
    int    cloud_cover_pct[MB_FORECAST_HOURS];  // Raw 0-100%
    time_t timestamps[MB_FORECAST_HOURS];       // Unix timestamp per hour
    int    count;                               // Number of valid data points
    bool   valid;
} mb_forecast_data_t;

// Initialize the Meteoblue client
esp_err_t mb_client_init(void);

// Fetch hourly cloud cover forecast (blocking, uses PSRAM buffer)
// Populates data with 24h of hourly cloud cover centered on now (-12h to +12h)
esp_err_t mb_fetch_forecast(mb_forecast_data_t *data);

#endif // METEOBLUE_CLIENT_H
