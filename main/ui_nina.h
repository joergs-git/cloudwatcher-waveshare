// NINA image display screen - shows latest captured frame with metadata overlay
// v0.4.2

#ifndef UI_NINA_H
#define UI_NINA_H

#include "lvgl.h"
#include "nina_client.h"

// Create the NINA image screen UI elements on the given parent
lv_obj_t *ui_nina_create(lv_obj_t *parent);

// Update screen with new image data and metadata overlay
void ui_nina_update(const nina_image_data_t *data);

// Show a status message (e.g. "NINA offline", "No active session")
void ui_nina_set_status(const char *message);

#endif // UI_NINA_H
