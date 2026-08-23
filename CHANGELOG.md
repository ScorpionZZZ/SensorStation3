# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [1.6.9]

### Added
- Possibility to switch Celsius / Fahrenheit degrees

## [1.6.8]

### Added
- The Firmware Update screen now points at a default OTA URL out of the box
  (`http://iot.scorpionzzz.com/sensorstation3/firmware/sensorstation3.latest.bin`)
  instead of requiring one to be entered manually before update checks work.
- The Firmware Update screen now triggers an immediate version check — instead
  of waiting for the next scheduled check (first at +60 s, then every 6 h) —
  both when the screen is opened and when the firmware URL is edited and
  saved. The result is shown right away: the latest version if the `.json`
  manifest was read successfully, or "N/A" if it's unreachable or malformed.
- `ota_manager_check_now()` / `ota_manager_derive_check_url()` in
  `ota_manager`, backing the above; `main.c`'s boot-time check now reuses the
  same derivation helper instead of duplicating the `.bin`→`.json` swap.

## [1.6.7]

### Fixed
- Fixed a boot loop on devices with no BME280/BME680/SCD4x sensor connected.
  `sensor_hub_init()`/`sensor_history_init()` were only called when a sensor
  was detected, leaving `sensor_history`'s internal mutex uninitialized; the
  ThingsBoard, Domoticz, and Home Assistant MQTT modules all call
  `sensor_history_get_snapshot()` unconditionally on their first publish tick
  after connecting, which asserted on the NULL mutex and rebooted the device.
  Both are now initialized unconditionally — the history task already no-ops
  gracefully when no sensor data is available.

## [1.6.6]

### Changed
- LVGL's timer task is now pinned to core 1 (`xTaskCreatePinnedToCore`), instead
  of floating free on either core, so it doesn't contend with WiFi/LWIP on
  core 0.
- The Domoticz Temp+Hum(+Baro) IDX field's placeholder text now reflects the
  connected sensor's actual capabilities ("Temp+Hum+Baro IDX", "Temp+Hum IDX",
  or "Temperature IDX") instead of always reading "Temp+Hum+Baro IDX",
  matching the device type `domoticz_mqtt` actually publishes.

### Fixed
- The Domoticz settings screen's Username and Password fields (and the Save
  button, on sensors with a CO2 IDX field) could end up hidden behind the
  on-screen keyboard once focused. The content area is now a scrollable
  container that shrinks to the space above the keyboard while it's open and
  scrolls the focused field into view, then returns to full height when the
  keyboard closes.

## [1.6.4]

### Added
- New `mqtt_base` component — a shared MQTT lifecycle layer (service-namespaced
  client-id generation, IP-event-gated first start, publish timer, the
  connect/disconnect/published/error event handling, and teardown) now used by
  all three telemetry integrations. Each module supplies only its NVS settings
  and payload builders through callbacks; topics, payloads, QoS, retain, LWT,
  auth, and cadence stay under module control.
- `mqtt_diag_fields()` in `sensor_hub`, single-sourcing the rssi/uptime/
  backlight/ldr_mv diagnostic fields shared by the ThingsBoard and Home
  Assistant payloads.
- MQTT connection-refused failures (bad token/credentials) are now logged with
  the broker return code instead of failing silently behind a reconnect loop.

### Changed
- The ThingsBoard, Domoticz, and Home Assistant MQTT modules were refactored
  onto `mqtt_base`, removing the duplicated lifecycle scaffolding (net −492/+270
  lines). Their on-the-wire output — topics, payloads, client IDs, QoS, retain,
  discovery, availability — is unchanged.

### Fixed
- Domoticz now publishes temperature and humidity for sensors without a
  barometer (e.g. an SCD41-only setup). The Domoticz device format is chosen
  from the sensor's capabilities — Temp+Hum+Baro, Temp+Hum, or Temperature —
  where previously the Temp/Hum/Baro device required a pressure reading and so
  published nothing for the SCD41. The virtual device configured for the
  `dz_thp_idx` IDX must be of the matching type (create a "Temp+Hum" device for
  an SCD41).

## [1.6.3]

### Added
- SCD41 sensor support
- New `sensor_hub` component — a common layer over the individual sensor
  drivers. Each driver (BMx280/BME680, SCD41, and BSEC as a decorator over
  the BME680) now exposes a uniform interface and registers with the hub,
  which aggregates their capabilities and merges them into a single live
  snapshot. Display and all telemetry integrations (ThingsBoard, Domoticz,
  Home Assistant) read that one snapshot with per-field capability flags
  instead of querying each driver directly.
- CO2 now has its own 30-second rolling average (15 samples × 2 s), matching
  the smoothing already applied to temperature, humidity, and pressure.
- Settings → Device Info lists every connected sensor joined together (e.g.
  `BME680 + SCD41`, up to three) instead of only the BMx280 chip.

### Changed
- `sensor_history` now sources its data from `sensor_hub` rather than reading
  the BMx280/SCD41 drivers directly, and feeds the display numbers, charts,
  and MQTT telemetry from its loop-average buffers.
- The Domoticz CO2 (Air Quality) device IDX can now be configured whenever a
  CO2-capable sensor is present, including an SCD41 — previously it was
  offered only for the BME680.

### Fixed
- An SCD41-only setup no longer reads "Sensor: not connected" on the settings
  screen; the device-info row now recognises the SCD41 (and any combination
  of connected sensors).

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