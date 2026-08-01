# Architecture

SensorStation3 is ESP-IDF firmware for the CYD ESP32-2432S028 board (an ESP32
with an integrated 240×320 ILI9341 touchscreen). It reads an I2C environmental
sensor (BMP280 / BME280 / BME680), displays live readings and short-term
history on the touchscreen, and publishes telemetry to ThingsBoard and/or
Domoticz over MQTT.

## Component layout

Two top-level source trees:

- `main/` — application entry point, on-device Settings UI, and persistent
  settings storage. Not a reusable component; it wires everything else
  together.
- `components/` — self-contained ESP-IDF components, each owning one piece of
  hardware or one integration.

### main/

- `main.c` — `app_main()`: init order for NVS, display, sensor, WiFi, MQTT
  clients, NTP, OTA, and the LVGL UI.
- `nvs_settings.c` / `.h` — typed get/set wrappers around the `"settings"`
  NVS namespace (see [CONFIGURATION.md](CONFIGURATION.md) for the full key
  list).
- `ui.c` / `.h` + `ui_main.c` — main dashboard screen (time, sensor readings,
  24h charts).
- `ui_settings.c` — PIN-protected settings screen.
- `ui_wifi.c`, `ui_mqtt.c`, `ui_domoticz.c`, `ui_ota.c`, `ui_pin.c` — settings
  sub-screens for WiFi, ThingsBoard, Domoticz, OTA, and PIN change
  respectively.

### components/

| Component | Responsibility |
|---|---|
| `board` | All GPIO pin numbers and peripheral constants for the CYD board; every other component that touches hardware depends on this instead of hardcoding pins. |
| `display` | LCD/LVGL lifecycle: ILI9341 panel init over SPI, LVGL v9 draw buffers and tick source, backlight PWM (LEDC). |
| `bmx280` | Self-contained BMP280/BME280/BME680 driver — I2C communication, register-level calibration, auto-detection, and a background sampling task. |
| `bsec` | Optional (Kconfig-gated) integration of Bosch's proprietary BSEC library for BME680 IAQ/CO2-equivalent processing. Compiles to an empty component when disabled; see the README's "Building with BSEC" section. |
| `photores` | Reads the onboard light-dependent resistor (LDR) over ADC and derives a backlight brightness value, published to a queue that `display` consumes. |
| `wifi_manager` | Thin wrapper around `esp_wifi` in station (STA) mode only — init, connect, and connection-state queries. There is no AP/provisioning mode; WiFi is configured through the on-device Settings UI. |
| `ntp_clock` | SNTP time sync with a configurable POSIX timezone string, resynced hourly. |
| `sensor_history` | Ring-buffer storage of sensor readings used to render the dashboard's 24-hour temperature/humidity charts. |
| `tb_mqtt` | Publishes JSON telemetry to a ThingsBoard MQTT broker on a timer once connected. |
| `domoticz_mqtt` | Publishes sensor readings to Domoticz virtual devices over MQTT. |
| `ota_manager` | Checks a JSON manifest for new firmware, downloads and installs OTA updates, optionally fully automatically. |
| `fonts` | Pre-rendered LVGL bitmap fonts (digit-only, several sizes/bit-depths) used by the dashboard. |

## I2C sensor auto-detection

`bmx280_init()` (called once from `app_main()`) requires no configuration to
identify the connected sensor:

1. It probes I2C address `0x76`, then `0x77` (BMx280 modules can be strapped
   to either), attempting to read the chip ID register (`0xD0`) at each
   address.
2. The chip ID value determines the sensor type:
   - `0x60` → BME280 (adds humidity)
   - `0x56` / `0x57` / `0x58` → BMP280 (no humidity)
   - `0x61` → BME680 (adds humidity + gas resistance, optionally IAQ via BSEC)
3. If no sensor answers at either address, `bmx280_init()` returns
   `ESP_ERR_NOT_FOUND` and the firmware continues running without sensor
   readings rather than failing to boot.

Once identified, a dedicated background task samples the sensor every 500 ms
(x16 oversampling on all channels) and stores the latest reading behind a
mutex; any other task reads a thread-safe copy with `bmx280_read()`.

## Data flow

```
 I2C sensor (BMP280 / BME280 / BME680)
        │  sampled every 500ms by a background task
        ▼
   bmx280_read() ────────────────┐
        │                        │ (BME680 only, if BSEC is enabled)
        │                        ▼
        │                  bsec_sensor_read()  (IAQ / CO2eq / accuracy)
        ▼                        │
 ┌──────┴────────────────────────┘
 │  dashboard's 500ms LVGL timer (ui_main.c)
 ▼
 ├─► on-screen dashboard (current readings, trend, 24h chart via sensor_history)
 ├─► tb_mqtt        → ThingsBoard, JSON telemetry, on a publish-interval timer
 └─► domoticz_mqtt  → Domoticz virtual devices, over MQTT
```

Display brightness follows a separate, independent loop: `photores` samples
the onboard LDR and pushes a brightness value to a queue that
`display_set_backlight()` consumes to drive the LEDC-based backlight PWM.