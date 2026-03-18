#ifndef UI_HOME_H
#define UI_HOME_H

#include "lvgl.h"
#include "cloudwatcher_client.h"
#include "nina_client.h"
#include "meteoblue_client.h"

// Create the home overview screen on the given parent
lv_obj_t *ui_home_create(lv_obj_t *parent);

// Update clock overlay (call every second for responsive NTP display)
void ui_home_update_time(void);

// Update home screen with current sensor readings
void ui_home_update(const cw_current_data_t *data);

// Update home screen chart with 24h graph data (filtered to past 12h)
void ui_home_update_graph(const cw_graph_data_t graphs[CW_GRAPH_SERIES_COUNT]);

// Update home screen chart with Meteoblue forecast data (-12h to +12h)
void ui_home_update_forecast(const mb_forecast_data_t *forecast);

// Update dome status display on home screen
void ui_home_update_dome(const nina_dome_status_t *dome);

// Get the chart object (for registering gesture handlers from ui_main)
lv_obj_t *ui_home_get_chart(void);

#endif // UI_HOME_H
