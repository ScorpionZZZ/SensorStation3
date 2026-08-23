# Configuration

All persistent settings are stored in a single NVS namespace, `"settings"`,
managed by `main/nvs_settings.c`. `nvs_settings_init()` runs once at boot and
writes defaults for the keys that must always have a value (PIN, ThingsBoard
URI, publish interval); the rest fall back to an in-code default at read time
if absent from NVS.

## Settings

| Key | Type | Default | Description |
|---|---|---|---|
| `pin` | string (4 digits) | `1234` | PIN protecting the on-device Settings screen. |
| `wifi_ssid` | string (≤32 chars) | *(unset)* | WiFi network name. |
| `wifi_pass` | string (≤64 chars) | *(unset)* | WiFi password. |
| `tz` | string (≤64 chars) | `CET-1CEST,M3.5.0,M10.5.0/3` | POSIX TZ string used for NTP-synced local time. |
| `tb_uri` | string (≤127 chars) | `mqtt://demo.thingsboard.io` | ThingsBoard MQTT broker URI. |
| `tb_token` | string (≤63 chars) | *(empty → MQTT disabled)* | ThingsBoard device access token (used as MQTT username). |
| `tb_interval` | uint16 | `30` | Telemetry publish interval, in seconds. |
| `tb_enabled` | bool (u8) | `true` (absent key reads as enabled) | Master on/off switch for ThingsBoard publishing. |
| `ota_url` | string (≤255 chars) | *(empty → OTA checks disabled)* | URL of the firmware `.bin`; the update-check URL is derived by replacing `.bin` with `.json`. |
| `ota_auto` | bool (u8) | `false` | Automatically download and install an update as soon as one is found. |
| `bme680_toff` | int16, tenths of °C | `-5` (-0.5 °C) | BME680 temperature offset, compensating self-heating from the sensor's own gas-heater element. |
| `dz_uri` | string (≤127 chars) | *(empty → disabled)* | Domoticz MQTT broker URI. |
| `dz_enabled` | bool (u8) | `false` | Master on/off switch for Domoticz publishing. |
| `dz_thp_idx` | uint16 | `0` (unset) | Domoticz virtual device IDX for the combined Temperature+Humidity+Barometer device. |
| `dz_co2_idx` | uint16 | `0` (skip) | Domoticz virtual device IDX for the Air Quality/CO2-equivalent device; `0` disables this specific publish. |
| `dz_user` | string (≤63 chars) | *(empty → anonymous)* | Domoticz MQTT username. |
| `dz_pass` | string (≤63 chars) | *(empty → anonymous)* | Domoticz MQTT password. |

Adding a new setting means adding a key, a get/set pair, and (if it needs
one) a default written in `nvs_settings_init()` — all in
`main/nvs_settings.c` / `.h`. The one exception is `tb_mqtt`, which reads the
`tb_*` keys directly via its own `nvs_open()` call (same namespace and key
names) to avoid a component → `main` dependency.

## Exposure through the Settings UI

The on-device Settings screen (`main/ui_settings.c`) is PIN-protected
(`pin`, default `1234`) and opens into sub-screens, each backed by the
settings above:

- **WiFi** (`ui_wifi.c`) — scans nearby access points, lets you pick one and
  enter a password; writes `wifi_ssid` / `wifi_pass` and connects
  immediately.
- **ThingsBoard** (`ui_mqtt.c`) — edits `tb_uri`, `tb_token`, `tb_interval`,
  and the `tb_enabled` checkbox; saving restarts the MQTT client with the new
  values.
- **Domoticz** (`ui_domoticz.c`) — edits `dz_uri`, `dz_user`, `dz_pass`,
  `dz_thp_idx`, `dz_co2_idx`, and the `dz_enabled` checkbox.
- **OTA** (`ui_ota.c`) — edits `ota_url` and the `ota_auto` checkbox; also
  shows the current firmware version and update status.
- **PIN** (`ui_pin.c`) — changes the `pin` value that gates the Settings
  screen itself.
- A **BME680 temperature offset** control (only shown when a BME680 is
  detected) adjusts `bme680_toff` in ±0.1 °C steps, clamped to ±5.0 °C.
- Timezone (`tz`) is also configured from the main Settings screen.

Any code that mutates settings from outside the LVGL timer task must hold
`lv_lock()` / `lv_unlock()` around the corresponding UI update.