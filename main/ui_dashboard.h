#ifndef UI_DASHBOARD_H
#define UI_DASHBOARD_H

#include "lvgl.h"
#include "cloudwatcher_client.h"

// Create the dashboard screen and all its widgets
lv_obj_t *ui_dashboard_create(lv_obj_t *parent);

// Update dashboard widgets with new data
void ui_dashboard_update(const cw_current_data_t *data);

#endif // UI_DASHBOARD_H
