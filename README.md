# SensorStation3

An open-source ESP32-based environmental monitoring station — temperature, humidity, atmospheric pressure, CO2 (equivalent), and dew point, with a built-in touchscreen dashboard, MQTT/cloud integration, and OTA firmware updates.

<p align="center">
  <img src="docs/images/device-photo.jpg" alt="SensorStation3 device showing the dashboard with temperature, humidity, dew point, pressure and CO2 readings" width="500">
</p>

## Features

- **Built-in touchscreen dashboard** running on an ESP32 CYD (Cheap Yellow Display) board — both ILI9341 and ST7789 panel variants are supported and auto-detected at boot, no configuration needed:
  - Date, time, and Wi-Fi signal strength
  - Current temperature with a 24-hour graph, trend, and rate of change — displayed in °C or °F (selectable in Settings, default °C)
  - Current humidity with a 24-hour graph, trend, and rate of change
  - The 24-hour graphs are backfilled from ThingsBoard, Domoticz, or Home Assistant right after boot instead of starting empty (see [24h chart history](#24h-chart-history))
  - Calculated dew point
  - Atmospheric pressure
  - CO2 (equivalent) level, when a compatible sensor is connected
  - On-device settings screen
- **Automatic I2C sensor detection** — no manual configuration needed for supported sensors
- **NTP time sync** with configurable timezone, refreshed hourly
- **Temperature offset compensation** for BME680 (-5 °C to +5 °C), useful when the sensor is affected by nearby heat sources
- **MQTT integration** with [ThingsBoard](https://thingsboard.io/), [Domoticz](https://www.domoticz.com/), and [Home Assistant](https://www.home-assistant.io/) (via MQTT Discovery — sensors are auto-detected, no manual entity setup needed)
- **OTA firmware updates** — automatic update check with on-screen notification, and optional automatic install
- **Auto-dimming backlight** driven by the onboard LDR (light sensor)

## Supported sensors

Sensors are detected automatically over I2C — just wire one up and it's picked up without any configuration.

| Sensor | Measures                                                                   | Status |
|---|----------------------------------------------------------------------------|---|
| BMP280 | Temperature, pressure                                                      | ✅ Supported |
| BME280 | Temperature, humidity, pressure                                            | ✅ Supported |
| BME680 (without BSEC) | Temperature, humidity, pressure, gas resistance, CO2-equivalent based on sensor resistance | ✅ Supported |
| BME680 (with BSEC) | Temperature, humidity, pressure, gas resistance, IAQ / CO2-equivalent      | ✅ Supported — requires the proprietary Bosch BSEC library, see [Building with BSEC](#building-with-bsec) |
| SCD40 / SCD41 | CO2 (direct NDIR measurement), temperature, humidity                       | ✅ Supported |
| SHT35 | Temperature, humidity (high precision)                                     | 🚧 Planned |

> **Note on CO2:** the BME680 with BSEC reports an *IAQ-derived CO2-equivalent*, not a direct CO2 measurement. For an actual NDIR CO2 reading, use an SCD40/41 — it reports real CO2 plus temperature and humidity, and (having no barometer) publishes to Domoticz as a Temp+Hum device.

## Hardware

- **Board:** [ESP32 CYD](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) (ESP32 with an integrated TFT touchscreen) — CYD boards ship with either an ILI9341 or ST7789 display controller depending on revision; the firmware detects which one is present automatically
- **Sensors:** any supported I2C sensor from the table above, connected externally via the board's I2C header
- A 3D-printable enclosure that houses the sensor together with the board is planned — see [Roadmap](#roadmap)

## Getting started

### Prerequisites

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html) **v6.0.x** — earlier major versions (5.x) are not supported, the driver/component APIs this project relies on differ significantly
- Git
- A supported I2C sensor wired to the board (I2C pins depend on your CYD variant — check `docs/` for pinout, if available)

### Building with BSEC

If you're using a BME680 and want IAQ / CO2-equivalent output, the project depends on Bosch Sensortec's proprietary **BSEC** library (built against v2.6.1.0). It is **not redistributed** in this repository — you need to download it yourself directly from Bosch and accept their license terms:

1. Go to the [Bosch Sensortec BSEC software page](https://www.bosch-sensortec.com/software-tools/software/bsec/) and download the BSEC library.
2. Extract the archive and copy its `algo` directory into `components/bsec/algo/` in this repo, so that these paths exist:
   - `components/bsec/algo/bsec_IAQ/inc/`
   - `components/bsec/algo/bsec_IAQ/config/bme680/bme680_iaq_33v_3s_4d/`
   - `components/bsec/algo/bsec_IAQ/bin/esp/esp32/libalgobsec.a`
3. Build as usual with `idf.py build` — BSEC is picked up automatically once the library is in place.

If you'd rather not obtain BSEC, disable it before building:

```bash
idf.py menuconfig
# → SensorStation3 → uncheck "Enable BSEC air-quality processing for BME680"
```

With BSEC disabled, BME680 still reports temperature/humidity/pressure/gas resistance and an approximate CO2-equivalent, just without BSEC's IAQ/static_iaq/accuracy outputs. BMP280/BME280 builds are unaffected either way.

### Build and flash

```bash
git clone https://github.com/ScorpionZZZ/SensorStation3.git
cd SensorStation3

idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Replace `/dev/ttyUSB0` with your board's serial port (on Windows, something like `COM3`).

To produce a single merged binary (bootloader + partition table + app, flashable at offset `0x0` — handy for distributing to end users via esptool or a web-based flasher), run `scripts/merge-bin.sh`; it writes `build/SensorStation3-merged.bin`.

### First boot

1. On first boot, open the **Settings** screen on the touchscreen (default PIN `1234`) → **WiFi**. It scans for nearby networks — pick yours and enter the password on the on-screen keyboard.
2. Still in Settings, configure timezone, MQTT/ThingsBoard/Domoticz/Home Assistant connection, and sensor options.
3. **Change the default settings PIN (`1234`) immediately** after your first boot.

## Configuration

All runtime configuration (Wi-Fi, MQTT broker, timezone, temperature offset, display units, etc.) is stored in NVS and managed through the on-device Settings screen — no config file needs to be edited before building. The °C/°F toggle only affects the on-device display; MQTT telemetry (ThingsBoard, Domoticz, Home Assistant) always publishes temperature in °C.

## 24h chart history

By default, the main screen's temperature/humidity graphs start empty and fill in over
the following 24 hours as the device samples its own sensor. Instead, Settings → **24h
History Chart** can pick a backend to pre-fill those graphs from right after boot — Off
(default) / ThingsBoard / Domoticz / Home Assistant, all three implemented.

To use it with Domoticz:

1. In Settings → Configure Domoticz → **History Chart Source**, set:
   - **Base URL** — Domoticz's web/JSON API address, e.g. `http://192.168.1.50:8080` (just
     the base — no path). This is *not* the same as the MQTT broker URI configured
     elsewhere on the Domoticz screen, which is usually a different port.
   - **Username** / **Password** — your Domoticz dashboard login (Setup → Settings →
     Security in Domoticz). This is *not* the MQTT username/password used for telemetry.
2. In Domoticz itself, go to **Setup → Settings → Security → API Protection** and enable
   **"Allow Basic-Auth authentication over plain HTTP (API only)"**. Without this, every
   request from the device is rejected with a 401 error, regardless of whether the
   username/password above are correct.
3. Reboot the device — the fetch only runs once, right after the first WiFi connection
   after boot, so saving these settings alone doesn't trigger it.

To use it with ThingsBoard:

1. In Settings → Configure ThingsBoard → **History Chart Source**, set:
   - **Base URL** — your ThingsBoard instance's REST API address, e.g.
     `https://demo.thingsboard.io` (just the base — no path). This is *not* the same as
     the MQTT broker URI configured elsewhere on the ThingsBoard screen.
   - **Username** / **Password** — a real ThingsBoard **user** login (your dashboard
     email/password), not the device's access token. The device's own MQTT token has no
     permission to read history; the fetch logs in as a user to get a JWT instead.
   - **Device ID** — the device's UUID, found under Devices → your device → Details →
     "Device ID" in the ThingsBoard UI. This is different from the device's access token.
2. Reboot the device — same one-shot-after-boot behavior as Domoticz above.

To use it with Home Assistant:

1. In Home Assistant, go to your profile (bottom-left) → **Security** tab → **Long-Lived
   Access Tokens** → **Create Token**, and copy it — it's only shown once.
2. In Home Assistant, go to **Settings → Devices & Services → Entities** and find the
   temperature (and, if available, humidity) sensor entities this device published via MQTT
   Discovery. Copy their **entity IDs** (e.g. `sensor.ss3_aabbccddeeff_temperature`) — the
   exact ID depends on how the device/entities ended up named in Home Assistant, so it can't
   be guessed automatically.
3. In Settings → Configure Home Assistant → **History Chart Source**, set:
   - **Base URL** — your Home Assistant instance's address, e.g. `http://homeassistant.local:8123`
     (just the base — no path). Most local installs use plain `http://`; this is *not* the same
     as the MQTT broker URI configured elsewhere on the Home Assistant screen.
   - **Long-Lived Access Token** — the token from step 1. This is *not* the MQTT username/password
     used for telemetry.
   - **Temperature entity_id** (required) / **Humidity entity_id** (optional, leave blank for
     sensors with no humidity channel) — from step 2.
4. Reboot the device — same one-shot-after-boot behavior as the other two backends.

All three backends briefly stop and restart all three MQTT clients (ThingsBoard, Domoticz, Home
Assistant) for the ~1-2 s the fetch takes — this ESP32's memory is tight enough that a TLS
handshake alongside 3 already-connected MQTT clients can fail outright, so the clients are
paused, the fetch runs, and they reconnect automatically afterward. This is invisible in
normal use beyond a brief telemetry gap right after boot. (Home Assistant instances reachable
over plain `http://`, the common case for a local install, never open a TLS connection at all
for this fetch, so this precaution mostly matters if pointed at an `https://` instance.)

**ThingsBoard's backfill is best-effort**, not strictly guaranteed every boot: on this device's
memory budget, reading a telemetry response can in principle fail outright due to a TLS buffer
allocation limit right around the edge of available heap. The fetch is split into two smaller
requests specifically to stay clear of that limit, and it hasn't been observed to fail since;
if a fetch ever does fail, the device logs a warning, reconnects to MQTT as normal, and simply
leaves that boot's graphs to fill in the usual way (over the next 24h) — never a crash or a
broken chart. Domoticz's and Home Assistant's backfills haven't shown this failure mode (Home
Assistant, being usually plain HTTP, doesn't hit a TLS ceiling in the first place).

## OTA updates

The device periodically checks for new firmware releases (first at +60 s after boot, then every 6 h) and shows an on-screen indicator when an update is available. The Firmware Update screen ships with a default update URL — `http://iot.scorpionzzz.com/sensorstation3/firmware/sensorstation3.latest.bin`, pointing at this project's own release server — and also checks immediately — showing the latest available version, or "N/A" if the manifest can't be read — whenever you open that screen or save a new firmware URL. Point it at your own `.bin` URL instead if you're building and hosting custom firmware. Automatic installation can be enabled in Settings.

## Roadmap

- [x] SCD40/41 support (direct CO2 measurement)
- [ ] SHT35 support (high-precision temperature/humidity)
- [ ] 3D-printable enclosure integrating the sensor inside the case
- [ ] Web-based flashing (flash firmware from the browser, no local toolchain required)

Track progress and open tasks in [Issues](https://github.com/ScorpionZZZ/SensorStation3/issues).

## Contributing

Issues and pull requests are welcome. If you'd like to contribute, please open an issue first to discuss what you'd like to change.

## License

This project's source code is licensed under the [Apache License 2.0](LICENSE).

This project uses the Bosch Sensortec **BSEC** library for BME680 IAQ processing. BSEC is proprietary and **not included** in this repository — see [NOTICE](NOTICE) and [Building with BSEC](#building-with-bsec) for details.