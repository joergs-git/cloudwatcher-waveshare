# CloudWatcher Waveshare Display

Native touch display for the AAG CloudWatcher Solo weather station, built on the Waveshare ESP32-P4 WiFi6 Touch LCD 4B (720x720).

![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.4-blue) ![Target](https://img.shields.io/badge/target-ESP32--P4-green) ![LVGL](https://img.shields.io/badge/LVGL-9.1-orange)

## Features

- **Home Screen** — Big status text (CLEAR/CLOUDY/DRY/WET) with dual-line temperature chart (sky temp in green, ambient in red)
- **Details Screen** — 6 sensor cards showing sky temperature, ambient temperature, humidity, rain, sky quality, and pressure with color-coded safety indicators
- **Charts Screen** — 6 tabbed 24h line charts (today + yesterday overlay) with dynamic Y-axis labels
- **Swipe Navigation** — Swipe left/right between screens or use bottom nav buttons
- **Auto-Refresh** — Current data every 60s, graph data every 4 minutes
- **WiFi Auto-Reconnect** — Handles ESP32-P4's ESP-Hosted WiFi via ESP32-C6 co-processor

## Hardware

- **Board:** [Waveshare ESP32-P4 WiFi6 Touch LCD 4B](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-4b.htm)
- **Display:** 4" 720x720 IPS, ST7703 MIPI-DSI
- **Touch:** GT911 capacitive
- **Weather Station:** AAG CloudWatcher Solo

## Building

### Prerequisites

- ESP-IDF v5.4+ (required for ESP32-P4 support)
- CloudWatcher Solo accessible on your local network

### Build & Flash

```bash
# Source ESP-IDF environment
source ~/esp/esp-idf/export.sh

# Configure WiFi and CloudWatcher IP
idf.py menuconfig
# -> CloudWatcher Settings: set WiFi SSID, password, and CloudWatcher IP

# Build
idf.py build

# Flash (check your USB port name)
idf.py -p /dev/cu.usbmodem* flash monitor
```

### Configuration (menuconfig)

| Setting | Default | Description |
|---------|---------|-------------|
| WiFi SSID | MyNetwork | Your WiFi network name |
| WiFi Password | MyPassword | Your WiFi password |
| CloudWatcher IP | 192.168.1.151 | CloudWatcher Solo IP address |
| CloudWatcher Port | 80 | CloudWatcher HTTP port |
| Poll Interval | 60s | Current data refresh interval |
| Graph Poll Interval | 240s | Graph data refresh interval |

## Architecture

```
FreeRTOS Tasks:
├── LVGL task (display rendering, ~30fps)
└── cw_poll_task (HTTP fetch + UI updates)

Screens: Home ↔ Details ↔ Charts (swipe or tap)
```

The ESP32-P4 has no native WiFi — it uses an onboard ESP32-C6 co-processor connected via SDIO (ESP-Hosted).

## CloudWatcher Endpoints

- `/cgi-bin/lastData.pl` — Current sensor readings (key=value pairs, ~300 bytes)
- `/cgi-bin/graphData.pl` — 24h historical data (~160KB JavaScript arrays, ~750 points/series)

## License

MIT
