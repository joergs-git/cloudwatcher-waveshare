#ifndef UI_HOME_H
#define UI_HOME_H

#include "lvgl.h"
#include "cloudwatcher_client.h"
#include "nina_client.h"

// Create the home overview screen on the given parent
lv_obj_t *ui_home_create(lv_obj_t *parent);

// Update home screen with current sensor readings
void ui_home_update(const cw_current_data_t *data);

// Update home screen chart with 24h graph data
void ui_home_update_graph(const cw_graph_data_t graphs[CW_GRAPH_SERIES_COUNT]);

// Update dome status display on home screen
void ui_home_update_dome(const nina_dome_status_t *dome);

#endif // UI_HOME_H
