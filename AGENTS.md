# AGENTS.md

Notes for coding agents: the bench, the recipes, and the constraints
that are not visible in the code.

## Bench

- Board on `/dev/cu.usbserial-210` (CH340): ESP32-D0WD-V3 WROOM-32,
  4 MB flash, no PSRAM. MAC `28:05:a5:e1:93:90`, device id `9390`.
- OLED 0.96" SSD1306 128x64 on I2C 0x3C, **SDA = GPIO22, SCL = GPIO21**
  (swapped against the ESP32 default; the Kconfig defaults match the
  wiring). 100 kHz; the first transaction at 400 kHz failed.
- Two DS18B20 on GPIO4 with an external pull-up. They are powered from
  GPIOs: VCC = GPIO16 driven high, GND = GPIO2 driven low. ROM codes
  `28216531054AC79B` (the one on the heater in the bench test) and
  `282167313A7ED4F0`. The broker from the reference bench,
  `192.168.1.102:1883`, takes anonymous connections.
- ESP-IDF v5.5.5 in `~/esp/esp-idf`. `esptool` v5 via pipx; the same
  venv has pyserial: `~/.local/pipx/venvs/esptool/bin/python`.
- Flash writes at 460800 are fine. **Flash reads over this CH340 drop
  bytes at every baud rate**: `read-flash` fails, so there is no way to
  back the device up. The factory ESP-AT firmware is gone.

## Recipes

Build:

```bash
source ~/esp/esp-idf/export.sh
IDF_SKIP_CHECK_SUBMODULES=1 idf.py build
```

`IDF_SKIP_CHECK_SUBMODULES=1` skips a git check that needs the network.
After editing `main/Kconfig.projbuild` or adding a component:
`idf.py reconfigure`.

Flash the app only (settings in `nvs` and the history partition
survive): `idf.py -p /dev/cu.usbserial-210 -b 460800 app-flash`.
Full image with `flash`, factory state with `erase-flash`.

Releases: `gh workflow run release.yml` (named `v1.0.<run number>`) or a
pushed `v*` tag runs `.github/workflows/release.yml`, which
builds `hydrotemp.bin` (merged, offset 0; the ESP32 bootloader sits at
0x1000 inside it) and the app image, attaches both to the release and
deploys `flasher/` (ESP Web Tools) to GitHub Pages. GitHub Pages must
be set to "GitHub Actions" in the repository settings.

Console capture without `esp_idf_monitor` (it needs a TTY and the port
is simple enough):

```bash
~/.local/pipx/venvs/esptool/bin/python - <<'EOF'
import serial, time, re
s = serial.Serial('/dev/cu.usbserial-210', 115200, timeout=0.5)
s.dtr = False; s.rts = True; time.sleep(0.1); s.rts = False   # reset
t0 = time.time(); buf = b''
while time.time() - t0 < 20:
    buf += s.read(8192)
print(re.sub(r'\x1b\[[0-9;]*m', '', buf.decode('utf-8', 'replace')))
EOF
```

Opening the port resets the board through the CH340 auto-reset
circuit no matter what DTR/RTS are set to, so there is no way to watch
a running device over USB: every capture starts at boot. For a running
device use the API (`curl http://hydrotemp-9390.local/api/v1/status`)
and `mosquitto_sub -h 192.168.1.102 -v -t 'esp32-hydro-temp/#'`.

Host tests: `cd test/host && make test` (appconf, history ring, the
freshness rule, the config POST merge, which compiles cJSON from the
ESP-IDF checkout) and `node test/ui/ui_smoke.js`.

Font and faces: `tools/gen_glyphs.py` rewrites `main/glyphs.h`; with
`--preview out.png "M 22.1 happy" "P 33.1 sad"` it renders the screen
as a PNG for a look before flashing. Faces are drawn from circles in
the script.

The device without Wi-Fi settings is the AP `HydroTemp-9390`; the Mac
cannot join it from an agent session, so first provisioning is done from
a phone. After that the device answers at `http://hydrotemp-9390.local/`
and `curl` reaches the API.

## Constraints

- Structure, style and most modules come from
  `../esp_32_holtek-usb-zy-temp-driver`; keep them in sync in spirit.
  SPDX header in every source file, C11, 4 spaces, `snake_case`.
  Comments only for what the code does not say. Commits in English,
  body says why.
- One poll tick drives everything: `sensors` polls, writes history,
  bumps `seq`; `display` and the publish task in `mqtt_mgr` act on a
  changed `seq`. The first poll is 5 s after boot (`SETTLE_S` in sensors.c),
  not configurable. The history ring in RAM is updated every poll and
  written to flash once per five-minute window.
- Sensors are bound to slots by ROM code. The sensor module writes only
  the `s<i>_rom` keys (`settings_save_rom`); everything else in NVS
  belongs to the settings POST. Users rename slots; they never pick
  ROMs. A replacement sensor takes the slot whose sensor is missing. A
  probe that stops answering is dropped at the next poll (`valid`
  cleared), not after the freshness timeout.
- MQTT: one JSON per sensor on `<prefix>/<slug(name)>` after every
  poll, nulls for a probe without a fresh reading; retried every second
  until both messages are accepted, and re-sent after every discovery
  publish so Home Assistant has state right after the entities.
  `<prefix>/availability` says only whether the device is connected
  (online on connect, offline by last will); per-probe availability is
  a template on the state topic. Discovery per slot: a temperature
  sensor and an "out of range" binary_sensor, `unique_id` bound to the
  slot (`a`/`b`), never to the name, so renaming keeps the HA entities.
- `esp_mqtt_client_publish/subscribe` only from the publish task in
  `mqtt_mgr`. Only `wifi_mgr` calls `esp_wifi_*`. `settings_load()` never fails.
  `reset_early_init()` runs before anything opens NVS. `fresh.h` is the
  only freshness rule: two poll periods plus 30 s. The range verdict is
  taken on the value rounded to a tenth, i.e. on what is displayed.
- The config POST merge lives in `config_merge.c` (pure cJSON +
  appconf, host-tested); `web_server.c` only moves bytes. The project's
  own modules use `configASSERT` for task creation; the copied ones keep
  whatever the reference does.
- The OLED driver mirrors both axes (`APP_OLED_FLIP`); this module needs
  it for an upright picture. The font has digits, `.`, `-`, `?`, space
  and A–Z only; names are shown by their first letter or digit. Layout
  constants are generated into `glyphs.h` by `tools/gen_glyphs.py`.
- The API has no authentication and CORS `*` by decision. Secrets are
  reported only as `*_set` booleans.
- Pages are self-contained HTML, no CDN: the captive portal has no
  internet. Style: flat, one accent `#FF6F3F`, hairlines, uppercase
  section labels, no cards, no explanatory copy.
- `managed_components/` is downloaded; do not edit it.
