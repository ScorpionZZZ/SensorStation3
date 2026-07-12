# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

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