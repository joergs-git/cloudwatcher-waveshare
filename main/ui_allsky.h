#ifndef UI_ALLSKY_H
#define UI_ALLSKY_H

#include "lvgl.h"
#include "esp_err.h"

// Create the allsky keogram display screen
lv_obj_t *ui_allsky_create(lv_obj_t *parent);

// Fetch keogram image from indi-allsky and update display (call with LVGL lock held)
// Returns ESP_OK on success, error otherwise
esp_err_t ui_allsky_fetch_and_update(void);

#endif // UI_ALLSKY_H
