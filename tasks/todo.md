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

## Results
- ESP-IDF v5.4 installed at ~/esp/esp-idf
- Project builds successfully for ESP32-P4 (0 errors, 0 warnings)
- Binary: ~1.3MB / 3MB partition (58% free)
- All managed components resolved: Waveshare BSP, ST7703, GT911, LVGL 9.1, esp_hosted
- Hardware tested: display working, touch working (backup address 0x14), WiFi connected, data polling operational
- 3 screens: Home (overview + chart), Details (sensor cards), Charts (6 tabbed metrics)
- Swipe + button navigation between screens
