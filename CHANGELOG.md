# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/), and this project adheres to [Semantic Versioning](https://semver.org/).

## [0.5.1] - 2026-03-17

### Added
- Meteoblue cloud cover forecast overlay on home screen chart
- New `meteoblue_client.c/h` module: HTTPS fetch + cJSON parse of Meteoblue basic-1h API
- Dark green forecast line showing cloud cover prediction (-12h to +12h)
- Cloud cover % mapped to sky-temperature equivalent (-25C = clear, +5C = overcast)
- Vertical "now" marker line at chart midpoint
- X-axis time labels: -12h, -6h, now, +6h, +12h
- Linear interpolation between hourly forecast points for smooth curve
- Kconfig: Meteoblue API key, latitude, longitude, altitude, poll interval (default 1h)

### Changed
- Home chart X-axis reworked from "midnight to now" to "-12h to +12h" centered on current time
- CloudWatcher actual data (sky temp + ambient) now fills left half only (past 12h)
- Y-axis auto-scales across both CloudWatcher and forecast data sources
- `cw_poll_task` stack increased to 24KB for TLS handshake overhead
- Legend updated: "Sky Temp", "Ambient", "Forecast"

## [0.5.0] - 2026-03-15

### Added
- Dome Control screen (screen 5) with OPEN/CLOSE buttons
- Tap dome banner on Home screen navigates to Dome Control
- CLOSE ALL button (immediate), OPEN ALL (2s delayed confirmation)
- Pushover HTTPS notifications for dome commands
- TLS certificate bundle in sdkconfig.defaults
- Auto-swap Home/NINA every 120s when NINA is actively shooting
- Clock overlay: custom 80px Montserrat font, 40% opacity
- Image count overlay on NINA screen
- "Inactive Session" amber indicator

## [0.4.2] - 2026-03-10

### Added
- NTP time sync via pool.ntp.org (CET/CEST)
- 48px clock overlay inside home chart
- Direct AstroShell dome query at /?$S (returns "OPEN" or "CLOSED")
- Dome status banner on home screen: CLOSED=green, OPEN=red

### Changed
- NINA poll interval reduced to 120s (was 300s)

## [0.4.1] - 2026-03-09

### Added
- NINA image display with SW JPEG decoder
- Dome status indicator
- Manual refresh button on NINA screen

## [0.4.0] - 2026-03-08

### Added
- NINA image screen with HW JPEG decoder
- Fixed chart Y-axis scales

## [0.1.0] - 2026-03-05

### Added
- Initial release: CloudWatcher sensor display on Waveshare ESP32-P4
- Home screen with sky/ambient temp chart
- Dashboard with 3x2 sensor card grid
- Charts screen with 6 tabbed 24h line charts
- WiFi via ESP32-C6 co-processor (esp_hosted)
- Swipe + button navigation between screens
