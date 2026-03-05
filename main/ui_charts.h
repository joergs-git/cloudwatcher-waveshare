#ifndef UI_CHARTS_H
#define UI_CHARTS_H

#include "lvgl.h"
#include "cloudwatcher_client.h"

// Create the charts screen with tab navigation
lv_obj_t *ui_charts_create(lv_obj_t *parent);

// Update chart data for all series
void ui_charts_update(const cw_graph_data_t graphs[CW_GRAPH_SERIES_COUNT]);

#endif // UI_CHARTS_H
