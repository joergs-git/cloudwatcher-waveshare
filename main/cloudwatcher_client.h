#ifndef CLOUDWATCHER_CLIENT_H
#define CLOUDWATCHER_CLIENT_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// Maximum number of data points per graph series
#define CW_GRAPH_MAX_POINTS 750

// Cloud cover thresholds (sky temperature in C)
// Clear < -6, Cloudy < 5, Overcast < 20
#define CW_CLOUD_CLEAR    -6.0f
#define CW_CLOUD_CLOUDY    5.0f

// Rain sensor thresholds (raw ADC values)
// Dry > 2500, Wet > 2000, Rain >= 0
#define CW_RAIN_DRY       2500
#define CW_RAIN_WET       2000
#define CW_RAIN_RAIN         0

// Sky quality thresholds (mag/arcsec^2)
// Dark > 17, Light > 13, Very Light > 10, Reference = 20
#define CW_LIGHT_DARK     17.0f

// Current sensor readings from lastData.pl
typedef struct {
    float clouds;           // Sky temp (C), negative = clear sky
    float temp;             // Ambient temperature (C)
    int   rain;             // Rain sensor raw value
    float light_mpsas;      // Sky quality (mag/arcsec^2)
    int   humidity;         // Relative humidity (%)
    float dew_point;        // Dew point temperature (C)
    float abs_pressure;     // Absolute pressure (hPa)
    float rel_pressure;     // Relative pressure (hPa)
    bool  safe;             // Overall safe for observing
    bool  switch_on;        // Relay switch state
    bool  clouds_safe;      // Individual safety flags
    bool  rain_safe;
    bool  light_safe;
    bool  wind_safe;
    bool  hum_safe;
    bool  pressure_safe;
    char  timestamp[32];    // Data timestamp string
    bool  valid;            // True if data was parsed successfully
} cw_current_data_t;

// Graph data series IDs we display
typedef enum {
    CW_GRAPH_CLOUDS = 0,
    CW_GRAPH_TEMP,
    CW_GRAPH_HUMIDITY,
    CW_GRAPH_RAIN,
    CW_GRAPH_LIGHT,
    CW_GRAPH_PRESSURE,
    CW_GRAPH_SERIES_COUNT
} cw_graph_series_t;

// Single graph series (today + yesterday)
typedef struct {
    float   today[CW_GRAPH_MAX_POINTS];
    float   yesterday[CW_GRAPH_MAX_POINTS];
    int     today_count;
    int     yesterday_count;
    float   today_x[CW_GRAPH_MAX_POINTS];     // X values (minutes from midnight)
    float   yesterday_x[CW_GRAPH_MAX_POINTS];
} cw_graph_data_t;

// Initialize the CloudWatcher HTTP client
esp_err_t cw_client_init(void);

// Fetch and parse current sensor data (blocking)
esp_err_t cw_fetch_current(cw_current_data_t *data);

// Fetch and parse 24h graph data (blocking, allocates from PSRAM)
esp_err_t cw_fetch_graphs(cw_graph_data_t graphs[CW_GRAPH_SERIES_COUNT]);

// Get descriptive string for cloud state
const char *cw_cloud_state_str(float sky_temp);

// Get descriptive string for rain state
const char *cw_rain_state_str(int rain_val);

// Get descriptive string for light state
const char *cw_light_state_str(float mpsas);

#endif // CLOUDWATCHER_CLIENT_H
