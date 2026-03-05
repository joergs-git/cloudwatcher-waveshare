# Lessons Learned

## [2026-03-05] - Waveshare ESP32-P4 board uses MIPI-DSI, not RGB
- **Mistake:** Initial plan assumed RGB parallel display interface
- **Root cause:** Common assumption for ESP32 boards, but ESP32-P4 supports MIPI-DSI
- **Rule:** Always verify board hardware interface before writing driver code. The Waveshare ESP32-P4 WiFi6 Touch LCD 4B uses ST7703 over MIPI-DSI (2-lane)
- **Applies to:** Display driver initialization, BSP selection

## [2026-03-05] - Waveshare provides official BSP component
- **Mistake:** Planned to write manual LCD/touch driver code
- **Root cause:** Didn't check ESP component registry first
- **Rule:** Always check components.espressif.com for official BSP before writing display drivers. Component: `waveshare/esp32_p4_wifi6_touch_lcd_4b`
- **Applies to:** Any Waveshare board ESP-IDF project

## [2026-03-05] - ESP32-P4 has no native WiFi
- **Mistake:** Used `CONFIG_ESP_WIFI_ENABLED=y` and standard `WIFI_INIT_CONFIG_DEFAULT()` which references undefined config macros
- **Root cause:** ESP32-P4 uses ESP32-C6 co-processor for WiFi via SDIO (esp_hosted)
- **Rule:** For ESP32-P4 WiFi: add `espressif/esp_wifi_remote` + `espressif/esp_hosted` deps, init hosted transport first with `esp_hosted_init()` + `esp_hosted_connect_to_slave()`, wait for transport UP, then use standard esp_wifi API
- **Applies to:** Any ESP32-P4 project needing WiFi

## [2026-03-05] - LVGL fonts must be explicitly enabled
- **Mistake:** Used `lv_font_montserrat_12`, `_16`, `_24`, `_28` without enabling them
- **Root cause:** LVGL only enables `lv_font_montserrat_14` by default
- **Rule:** Add `CONFIG_LV_FONT_MONTSERRAT_XX=y` to sdkconfig.defaults for each font size needed
- **Applies to:** Any ESP-IDF project using LVGL

## [2026-03-05] - BSP lock API vs lvgl_port
- **Mistake:** Used `lvgl_port_lock()` / `lvgl_port_unlock()` directly
- **Root cause:** The Waveshare BSP wraps these as `bsp_display_lock()` / `bsp_display_unlock()`
- **Rule:** When using a BSP, use its lock API rather than raw esp_lvgl_port functions
- **Applies to:** Any BSP-based LVGL project

## [2026-03-06] - LVGL built-in printf does not support %f
- **Mistake:** Used `lv_label_set_text_fmt()` with `%.1f` format specifier for floats
- **Root cause:** LVGL 9's built-in `_lv_vsnprintf` doesn't support float formatting by default; displays literal "f" instead of values
- **Rule:** Never use `%f`/`%.Xf` with `lv_label_set_text_fmt()`. Use C `snprintf()` into a buffer first, then `lv_label_set_text()`.
- **Applies to:** Any LVGL 9.x project on ESP-IDF

## [2026-03-06] - LVGL chart rendering needs large stack
- **Mistake:** Used 8KB stack for cw_poll_task that also calls LVGL chart update functions
- **Root cause:** LVGL chart rendering with float formatting + 400 point updates consumes significant stack; vsnprintf with floats is stack-heavy
- **Rule:** Tasks that trigger LVGL chart rendering need at least 16KB stack. The LVGL lock runs the code on the caller's stack, not the LVGL task's stack.
- **Applies to:** Any FreeRTOS task updating LVGL charts

## [2026-03-06] - GT911 touch controller needs graceful failure handling
- **Mistake:** BSP's `bsp_display_start_with_config()` uses `ESP_ERROR_CHECK` on touch init, crashing on I2C NACK
- **Root cause:** GT911 sometimes fails at default address 0x5D, needs backup address 0x14
- **Rule:** Init display and touch separately. Try GT911 at both I2C addresses (0x5D then 0x14). Make touch failure non-fatal so display still works.
- **Applies to:** Any ESP32-P4 project with GT911 touch controller

## [2026-03-06] - lv_chart_set_axis_tick doesn't exist in LVGL 9
- **Mistake:** Called `lv_chart_set_axis_tick()` which was an LVGL 8 API
- **Root cause:** LVGL 9 removed built-in axis tick labels from chart widget
- **Rule:** For Y-axis labels in LVGL 9, create manual `lv_label` objects positioned alongside the chart and update them when the range changes.
- **Applies to:** Any LVGL 9.x chart with axis labels
