# CloudWatcher Waveshare Display - TODO

## Phase 0: Environment Setup
- [x] Install ESP-IDF v5.4+ for ESP32-P4 support
- [x] Verify `idf.py --version` works

## Phase 1: Project Scaffold
- [x] CMakeLists.txt (root + main)
- [x] sdkconfig.defaults for ESP32-P4
- [x] partitions.csv
- [x] Kconfig.projbuild (WiFi/CW settings)
- [x] idf_component.yml with Waveshare BSP dependency
- [x] .gitignore

## Phase 2: Data Layer
- [x] cloudwatcher_client.h - data structures and API
- [x] cloudwatcher_client.c - HTTP fetch + parse for lastData.pl
- [x] cloudwatcher_client.c - HTTP fetch + parse for graphData.pl
- [x] Threshold constants matched to actual CloudWatcher config

## Phase 3: WiFi
- [x] wifi_manager.h/c - STA mode with auto-reconnect (ESP-Hosted)

## Phase 4: Display Driver
- [x] display_driver.h/c - Manual init with graceful GT911 touch handling

## Phase 5: UI
- [x] ui_main.h/c - screen management, nav bar, swipe navigation
- [x] ui_home.h/c - overview screen with big status + dual temp chart
- [x] ui_dashboard.h/c - 3x2 sensor card grid + safe banner
- [x] ui_charts.h/c - 24h line charts with 6 tabs + Y-axis labels

## Phase 6: Integration
- [x] main.c - init sequence + FreeRTOS task architecture

## Phase 7: Build & Test
- [x] Build for esp32p4 target (0 errors, 0 warnings)
- [x] Flash and test on hardware
- [x] Verify WiFi connection
- [x] Verify lastData.pl parsing
- [x] Verify dashboard display with correct values
- [x] Verify graphData.pl parsing (729-750 pts per series)
- [x] Verify charts display with Y-axis labels
- [x] Verify touch navigation (buttons + swipe)
- [x] Verify home screen with dual temp chart

## Phase 8: v0.5.0 - Dome Control
- [x] ui_dome.c/h - Dome Control screen with OPEN/CLOSE buttons
- [x] Tap dome banner on Home → navigates to Dome Control
- [x] Pushover HTTPS notifications
- [x] TLS cert bundle configured

## Phase 9: v0.5.2 - Meteoblue Cloud Forecast + Bug Fixes
- [x] meteoblue_client.c/h - HTTPS fetch + JSON parse (15-min resolution via clouds-15min)
- [x] MD5 signature validation for signed API key (mbedtls)
- [x] Kconfig: MB_API_KEY, MB_API_SECRET, lat/lon/alt, poll interval
- [x] API package changed: basic-15min_clouds-15min (was basic-1h_basic-day)
- [x] JSON key: data_xmin (not data_1h)
- [x] Removed espressif/usb dependency (incompatible with ESP-IDF v5.4 HAL)
- [x] Meteoblue HTTPS fetch works (50KB response, 673 entries)
- [x] **Fixed CW graph x-value mapping** — indices not minutes, added CW_X_TO_MIN() macro
- [x] **Fixed swipe/touch** — replaced lv_obj bar overlays with chart line series (bars killed touch)
- [x] **Fixed clock overlay** — restored 80px custom font as chart child, 40% opacity
- [x] **Fixed screen navigation** — lv_scr_load() + lv_indev_reset() (animation caused render lockup)
- [x] Forecast shown as grey line series spanning full -12h to +12h
- [x] NTP guard: skip graph mapping if time not synced
- [x] Navigation debounce (500ms)
- [x] WDT timeout increased to 60s
- [x] Chart reduced to 100 points (50 past + 50 future)

## Results
- ESP-IDF v5.4 installed at ~/esp/esp-idf
- Project builds successfully for ESP32-P4 (0 errors, 0 warnings)
- Binary: ~1.5MB / 3MB partition (50% free)
- All managed components resolved: Waveshare BSP, ST7703, GT911, LVGL 9.1, esp_hosted
- Hardware tested: display working, touch working (backup address 0x14), WiFi connected, data polling operational
- 5 screens: Home (overview + forecast line), NINA, Details (sensor cards), Charts (6 tabbed metrics), Dome Control
- Swipe + button navigation working, instant screen switch (no animation)
