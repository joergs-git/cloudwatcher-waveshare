#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include "esp_err.h"

// Waveshare ESP32-P4 WiFi6 Touch LCD 4B display parameters
#define DISPLAY_H_RES  720
#define DISPLAY_V_RES  720

// Initialize the LCD panel and touch controller, register with LVGL
esp_err_t display_driver_init(void);

#endif // DISPLAY_DRIVER_H
