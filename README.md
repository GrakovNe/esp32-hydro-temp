# HydroTemp

ESP32 firmware that watches the water temperature of a hydroponic
tank with two DS18B20 probes, shows both readings and a mood on a
0.96" OLED, serves a web page with 24-hour charts and a JSON API, and
publishes to MQTT with Home Assistant discovery.

## Hardware

- ESP32 WROOM-32 module with 4 MB flash.
- SSD1306 128x64 OLED on I2C (default SDA GPIO22, SCL GPIO21).
- Two DS18B20 on one 1-Wire line (default GPIO4) with a 4.7 kΩ
  pull-up. The probes can be powered from GPIOs (default VCC GPIO16,
  GND GPIO2).

Pins are in `idf.py menuconfig` under "HydroTemp".

## Install a ready-made firmware

Open <https://grakovne.github.io/esp32-hydro-temp/> in Chrome or Edge,
connect the board over USB, press Install. The page also has the image
and the command for `esptool`. Every tagged release publishes
`hydrotemp.bin` (complete image for offset 0) and `esp32_hydro_temp.bin`
(app only, offset 0x10000) on the Releases page.

## Build from source

ESP-IDF v5.5.x with the `esp32` target.

```bash
source ~/esp/esp-idf/export.sh
idf.py set-target esp32
idf.py build
idf.py -p /dev/cu.usbserial-XXXX -b 460800 flash monitor
```

One image for the web flasher:

```bash
esptool --chip esp32 merge-bin -o build/hydrotemp.bin \
  --flash-mode dio --flash-size 4MB --flash-freq 40m \
  0x1000 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/esp32_hydro_temp.bin
```

A release is a pushed `v*` tag: GitHub Actions builds both images,
attaches them to the release and publishes the flasher page to GitHub
Pages.

The first build downloads `mdns`, `onewire_bus` and `ds18b20` from the
component registry.

Tests:

```bash
cd test/host && make test        # appconf, history ring, freshness, config merge
node test/ui/ui_smoke.js         # page JS against mocked API answers
```

## Use

With no Wi-Fi saved the device opens an access point `HydroTemp-XXXX`
(the last four hex digits of its MAC) and shows those four digits and
`192.168.4.1` on the OLED. Join it from a phone; the setup page opens by
itself on most phones, otherwise open `http://192.168.4.1/`. Pick your
network, enter the password, save. The device restarts and answers at
`http://hydrotemp-xxxx.local/`, or at the IP your router gave it.

Pages: Dashboard (both temperatures with their allowed range, two
24-hour charts), Settings (Wi-Fi, sensor names and ranges, poll
interval, MQTT, device name, NTP server, restart, factory reset),
Device Info.

### Sensors

The two probes are found on the bus at boot and bound to two slots by
their ROM code; the binding is saved. Each slot has a name (default
`left` and `right`) and its own allowed range (default 18–23 °C). The
first letter or digit of the name is what the OLED shows next to the reading,
with a smiling face while the reading is inside the range and a
frowning one outside. A probe that is unplugged shows `--.-` and a
neutral face; a new probe takes the slot of a missing one.

Readings are taken every 5 minutes by default (10 s to 1 h in the
settings). The first reading is 5 s after boot. A reading of exactly
0.0 or 85.0 °C is treated as a probe fault (a shorted line and the
power-on default look like that). The 24-hour history needs the clock,
so it starts once NTP has answered.

### API

JSON over HTTP, no authentication, `Access-Control-Allow-Origin: *`.

| Method | Path | |
| --- | --- | --- |
| GET | `/api/v1/current` | both readings with range and verdict |
| GET | `/api/v1/history` | 288 five-minute averages, oldest first |
| GET | `/api/v1/status` | sensors, bus, Wi-Fi, MQTT, time, chip, firmware |
| GET | `/api/v1/config` | settings; passwords as `*_set` flags only |
| POST | `/api/v1/config` | save and restart; omitted fields keep their value |
| GET | `/api/v1/wifi/scan` | cached scan; `?refresh=1` starts a new one |
| POST | `/api/v1/reboot` | restart |
| POST | `/api/v1/factory-reset` | erase settings and restart |

```json
GET /api/v1/current
{
  "timestamp": 1761400000, "time_synced": true, "uptime_s": 5231, "poll_interval_s": 300,
  "sensors": [
    { "slot": "a", "name": "left", "low": 18, "high": 23, "present": true,
      "temperature": 21.37, "age_s": 40, "out_of_range": false },
    { "slot": "b", "name": "right", "low": 18, "high": 23, "present": true,
      "temperature": 24.5, "age_s": 40, "out_of_range": true }
  ]
}
```

### MQTT

After every poll, one JSON message per sensor to
`<prefix>/<name>` (the name lower-cased, spaces as `_`; the prefix
defaults to `esp32-hydro-temp/<id>`):

```json
{"temperature": 21.37, "out_of_range": false, "low": 18, "high": 23, "timestamp": 1761400000}
```

A probe without a fresh reading publishes `null` for `temperature`,
`out_of_range` and `timestamp`. `<prefix>/availability` is `online`
while the device is connected and `offline` by last will. With
Discovery on, Home Assistant gets one device with, per sensor, a
temperature sensor and an "out of range" binary sensor (device class
`problem`) whose attributes carry the range and the timestamp; both go
unavailable while that probe publishes `null`. The entities follow the
slot, so renaming a sensor renames them in place. The name
`availability` is reserved.

TLS with your own CA certificate or the built-in bundle; "skip
certificate verification" disables the check entirely.

### Factory reset

The red button on the Settings page, or hold the board's BOOT button
for 5 s. Settings, the sensor binding and the history are erased; the
device comes back as the setup access point.

## License

MIT, see `LICENSE`.
