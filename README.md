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

## Development Setup (macOS)

### 1. Install Prerequisites

You need **ESP-IDF v5.4+** (the ESP32-P4 is only supported from v5.4 onwards).

```bash
# Install system dependencies via Homebrew
brew install cmake ninja dfu-util python3

# Clone ESP-IDF v5.4 (or newer) into ~/esp
mkdir -p ~/esp && cd ~/esp
git clone -b v5.4 --recursive https://github.com/espressif/esp-idf.git

# Run the ESP-IDF install script (downloads toolchains for ESP32-P4)
cd ~/esp/esp-idf
./install.sh esp32p4
```

This takes a few minutes on first run. It installs the RISC-V toolchain, Python packages, and build tools into `~/.espressif/`.

### 2. Clone This Project

```bash
cd ~/Desktop  # or wherever you keep your projects
git clone https://github.com/joergsflow/cloudwatcher-waveshare.git
cd cloudwatcher-waveshare
```

### 3. Activate the ESP-IDF Environment

You need to source this **once per terminal session** before building:

```bash
source ~/esp/esp-idf/export.sh
```

> **Tip:** Add an alias to your `~/.zshrc` for convenience:
> ```bash
> alias get_idf='source ~/esp/esp-idf/export.sh'
> ```
> Then just type `get_idf` in any new terminal.

### 4. Configure WiFi and CloudWatcher IP

```bash
idf.py menuconfig
```

Navigate to **CloudWatcher Configuration** and set:

| Setting | Default | Description |
|---------|---------|-------------|
| WiFi SSID | MyNetwork | Your WiFi network name |
| WiFi Password | MyPassword | Your WiFi password |
| CloudWatcher IP | 192.168.1.151 | CloudWatcher Solo IP address |
| CloudWatcher Port | 80 | CloudWatcher HTTP port |
| Poll Interval | 60s | Current data refresh interval |
| Graph Poll Interval | 240s | Graph data refresh interval |

Press `S` to save, then `Q` to quit. The settings are stored in `sdkconfig`.

### 5. Build

```bash
idf.py build
```

First build takes a few minutes (compiles LVGL, ESP-Hosted, etc.). Subsequent builds are incremental and much faster.

### 6. Find the USB Port

Connect the board via USB-C. The Waveshare ESP32-P4 board uses a **CH340 USB-serial chip**. On macOS the port name looks like `/dev/cu.usbmodemXXXX` (the exact number changes on reconnect).

```bash
# List available serial ports
ls /dev/cu.usbmodem*
```

If nothing shows up, make sure you're using the correct USB-C port on the board (the one labeled **USB** or **UART**, not the one labeled **USB-OTG**). You may also need to install the [CH340 driver](https://www.wch-ic.com/downloads/CH341SER_MAC_ZIP.html) if macOS doesn't recognize it.

### 7. Flash and Monitor

```bash
# Flash firmware and open serial monitor (replace port if needed)
idf.py -p /dev/cu.usbmodem* flash monitor
```

The serial monitor shows boot logs and WiFi connection status. Press `Ctrl+]` to exit the monitor.

### Quick Reference

```bash
# Full workflow in one go
source ~/esp/esp-idf/export.sh
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

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
