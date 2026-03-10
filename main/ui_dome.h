#ifndef UI_DOME_H
#define UI_DOME_H

#include "lvgl.h"
#include "nina_client.h"

// Create the dome control screen on the given parent
lv_obj_t *ui_dome_create(lv_obj_t *parent);

// Update dome status display on the dome control screen
void ui_dome_update(const nina_dome_status_t *dome);

#endif // UI_DOME_H
