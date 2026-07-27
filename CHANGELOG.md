# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [1.5.10]

### Fixed
- Device rebooted in a loop when no BMx280 sensor was detected at boot:
  `sensor_history_init()` was skipped in that case, leaving its mutex
  uninitialized, while the UI's 500 ms refresh timer still queried
  `sensor_history_get_current()`/`get_24h()` unconditionally.

### Changed
- When no sensor is detected, the temperature, humidity, dew point, and
  pressure panels are replaced by a "Connect sensor and reboot the device"
  message instead of attempting to show readings.

## [1.5.7]

### Changed
- Temperature/humidity trend arrows now compare the momentary reading
  against its own rolling 30-second mean and recompute every 500 ms, instead
  of comparing two successive 30-second means and holding the result fixed
  for 30 seconds. The delta is rounded to one decimal place, and that
  rounded value decides the arrow (up/down/equal).

## [1.5.0]

### Added
- ST7789 display panel support, alongside the existing ILI9341. The panel
  controller is auto-detected at boot by reading its display ID register
  (RDDID) over the existing SPI wiring — both panel families use identical
  pinout on the CYD board, so no configuration is needed. A manual override
  (`CONFIG_SS3_LCD_PANEL` in `idf.py menuconfig`) is available in case
  auto-detection ever picks the wrong driver for a given module.

## [1.4.4]

### Added
- Home Assistant integration via MQTT Discovery: sensors are now
  auto-discovered in Home Assistant — temperature, humidity, pressure, dew
  point, gas resistance, and (on BME680 with BSEC) IAQ / CO2eq / VOC — plus
  diagnostic entities for WiFi signal, uptime, backlight, and light level.
  Configurable from a new Home Assistant section in Settings.

### Fixed
- ThingsBoard, Domoticz, and Home Assistant MQTT clients could repeatedly
  disconnect each other when two of them shared the same broker, because
  none set an explicit client ID and the default was identical for all
  three. Each now uses a unique client ID.
- Home Assistant was receiving sensor updates at QoS 0 instead of QoS 1
  because the discovery config didn't request QoS 1 for its subscriptions.

## [1.3.6]

### Added
- ThingsBoard telemetry: BME680 payloads now include `gas_resistance` (raw
  compensated gas sensor resistance, in Ohms) — sent both when BSEC is active
  and in the non-BSEC BME680 fallback path.

## [1.3.2]
- Last tagged release prior to this changelog's introduction.