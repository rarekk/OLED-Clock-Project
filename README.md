# OLED Clock (ESP32)

A small but polished ESP32 desk clock with a 128x64 I2C OLED display. It syncs
time over Wi-Fi with SNTP, shows an animated mascot, has a stopwatch, and lets
you pick between three procedurally drawn mascots — all remembered across
reboots.

The firmware is built with the [ESP-IDF](https://docs.espressif.com/projects/esp-idf/)
(v5.x) and can be built entirely inside Docker, so you don't need a local toolchain.

---

## Features

- **Wi-Fi (STA)** with exponential-backoff reconnect and offline fallback.
- **SNTP time sync** with a configurable POSIX timezone; shows `--:--` until
  the clock is actually valid instead of lying with 1970.
- **Big, readable clock** with date + weekday, seconds and a live status line.
- **Stopwatch** measured with `esp_timer`, so it stays accurate under load
  (start/stop and reset).
- **Month calendar** with a 7-column grid, leap-year-aware date math and today
  highlighted in inverse video, shown next to the mascot.
- **Event reminders**: keep up to 10 events in NVS, see a dot marker under days
  that have one, and get an on-screen popup at the exact minute it is due.
- **Phone web UI** (over Wi-Fi) to add and delete events — no app required.
- **Optional push notifications** to Telegram and/or LINE Notify when an event
  fires (compiled in only if you enable it in `menuconfig`).
- **Three mascots** (cat, robot, ghost) drawn from primitives with a blinking
  animation — no giant bitmap arrays to maintain.
- **Settings persisted in NVS**: the selected mascot and the brightness survive
  power cycles.
- **Brightness control** with the UP/DOWN buttons on the clock screen.
- **Configurable everything** via `menuconfig`: pins, I2C address/speed,
  timezone, SNTP server, debounce time. Wi-Fi credentials are never committed.
- **SSD1306 and SH1106** support (SH1106 is common on 1.3" modules and needs a
  132-column offset handled transparently).

## Screens

```
   CLOCK                     STOPWATCH                 SELECT MASCOT
+----------------------+   +----------------------+   +----------------------+
| MON 12/10   +----+   |   |       STOPWATCH      |   |    SELECT MASCOT     |
|             | /\ |   |   |                      |   |  +--+   +--+  +--+  |
| 12:34       |(oo)|   |   |      12:34           |   |  |  |   |[]|  |~~|  |
|             | \/ |   |   |                      |   |  |  |   |  |  |  |  |
| :45         +----+   |   |      .45  RUN        |   |  +--+   +--+  +--+  |
| NTP OK               |   | [OK]S/S [DN]RST      |   |      CAT  1/3        |
+----------------------+   +----------------------+   +----------------------+

   CALENDAR                                    EVENT POPUP
+----------------------+                   +----------------------+
|      OCT 2026        |                   |  ! REMINDER !        |
|  S  M  T  W  T  F  S |                   |----------------------|
|              1  2  3 |    1. month/year  |                      |
|  4  5  6  7  8  9 10 |    2. weekday    |   DENTIST            |
| 11 [12] 13 14 15 16 17|    3. day grid   |                      |
| 18 19 20 21 22 23 24 |    4. today box  |        MODE = OK     |
| 25 26 27 28 29 30 31 |    5. mascot     +----------------------+
+----------------------+
   ^ dot under a day = that day has an event
```

## Hardware

| Part                     | Notes                                            |
| ------------------------ | ------------------------------------------------ |
| ESP32 devkit (ESP32-WROOM)| Any ESP32 board works.                          |
| 0.96" SSD1306 **or** 1.3" SH1106 OLED | 128x64, I2C, usually address `0x3C`. |
| 4x push buttons          | Active-low, one side to GPIO, other side to GND. |

### Default wiring

| Signal            | ESP32 GPIO | Notes                       |
| ----------------- | ---------- | --------------------------- |
| OLED SDA          | GPIO 21    | I2C data                    |
| OLED SCL          | GPIO 22    | I2C clock                   |
| OLED VCC          | 3V3        | Do **not** use 5V            |
| OLED GND          | GND        |                             |
| Button `MODE`     | GPIO 13    | Cycle screens               |
| Button `UP`       | GPIO 12    | Brightness / mascot next    |
| Button `DOWN`     | GPIO 14    | Brightness / mascot prev / reset |
| Button `SELECT`   | GPIO 27    | Start / stop stopwatch      |

All button pins are pulled up internally, so the buttons just connect the pin
to GND when pressed. Every GPIO can be changed in `menuconfig`.

## Software requirements

- **Docker** (recommended for building) — Docker Desktop on Windows/macOS or
  Docker Engine on Linux.
- **Only for flashing on Windows:** Python 3 with `esptool`
  (`pip install esptool`) or a native ESP-IDF install.
- Optional: a native [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)
  installation if you prefer not to use Docker.

## Getting started

### 1. Configure Wi-Fi and the timezone

Wi-Fi credentials are **not** stored in the repository. Put them in your local
`sdkconfig` via menuconfig:

```bash
# Linux/macOS
make menuconfig

# Windows (PowerShell)
pwsh scripts/clock.ps1 menuconfig
```

Then go to **OLED Clock configuration → Wi-Fi credentials** and set
**Wi-Fi SSID** / **Wi-Fi password**. Optionally tweak the timezone under
**Time (SNTP)** (default `ICT-7`, i.e. UTC+7).

> **Important — `sdkconfig.defaults` is only a template.** It is read **only
> when `sdkconfig` does not exist**. Once `sdkconfig` has been generated, editing
> `sdkconfig.defaults` has no effect. So either:
> - use `menuconfig` (recommended) — it edits the real `sdkconfig`, or
> - edit `sdkconfig` directly (`CONFIG_OLED_CLOCK_WIFI_SSID="MyNetwork"`), or
> - delete `sdkconfig` and rebuild so `sdkconfig.defaults` is re-applied.
>
> If the firmware was built with an empty SSID it will print
> *"No Wi-Fi SSID configured - running offline"* on the serial log and show
> `NO WIFI` on the screen — that means the credential never reached the build.

> **ESP32 is 2.4 GHz only.** A 5 GHz-only network will never connect. For a
> phone hotspot, enable **"Maximize Compatibility"** (iPhone) or set the AP band
> to **2.4 GHz** (Android). The serial log now reports the disconnect reason
> code if association fails.

> **1.3" OLED users:** also select **Component config → SSD1306 / SH1106 OLED
> driver → OLED panel controller → SH1106**. Or set `CONFIG_SSD1306_PANEL_SH1106=y`
> in `sdkconfig.defaults` / `sdkconfig`.

### 2. Build

```bash
# Linux / macOS (Make)
make build

# Windows (PowerShell)
pwsh scripts/clock.ps1 build

# Or directly with Docker Compose
docker compose -f docker/docker-compose.yml run --rm idf idf.py build
```

The first build downloads the ESP-IDF image (a few hundred MB) — later builds
are fast thanks to the mounted `ccache` volume.

### 3. Flash

**Linux / macOS:**

```bash
make flash PORT=/dev/ttyUSB0 MONITOR=1
# or: SERIAL_PORT=/dev/ttyUSB0 make flash-monitor
```

**Windows:** Docker Desktop cannot forward a COM port into a container, so
build in Docker and flash from the host:

```powershell
pwsh scripts/clock.ps1 flash -Port COM5
pwsh scripts/clock.ps1 monitor -Port COM5
```

If you have a native ESP-IDF install, the simplest option is:

```powershell
idf.py -p COM5 flash monitor
```

#### Manual flash with `esptool` (no ESP-IDF install required)

If you only have Python 3 with [`esptool`](https://github.com/espressif/esptool)
(`pip install esptool`) and the firmware is already built, you can flash the
three binaries directly. Run this from the project root:

```powershell
python -m esptool --chip esp32 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 2MB --flash_freq 40m 0x1000 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/oled_clock_project.bin
```

- The paths are relative to `build/`, so run it from the project root.
- Add `-p COM5` (Windows) or `-p /dev/ttyUSB0` (Linux/macOS) if esptool cannot
  auto-detect the port.
- `--flash_size 2MB` matches this board's configuration. Use `detect` (or the
  value printed by your board) if yours has a different flash size.
- To read the logs afterwards, open a terminal at 115200 baud, e.g.
  `pwsh scripts/clock.ps1 monitor -Port COM5`.

<details>
<summary>Advanced: flashing from Docker on Windows via WSL2 + usbipd-win</summary>

1. Install [usbipd-win](https://github.com/dorssel/usbipd-win) and share the
   CP210x/CH340 USB serial adapter with WSL2:
   ```powershell
   usbipd list
   usbipd bind --busid <BUSID>
   usbipd attach --wsl --busid <BUSID>
   ```
2. Inside WSL the adapter appears as `/dev/ttyUSB0`. Run:
   ```bash
   SERIAL_PORT=/dev/ttyUSB0 make flash-monitor
   ```

</details>

## Controls

| Screen            | Button | Action                          |
| ----------------- | ------ | ------------------------------- |
| Any               | MODE   | Next screen                     |
| Clock             | UP     | Brightness +                    |
| Clock             | DOWN   | Brightness −                    |
| Stopwatch         | SELECT | Start / stop                    |
| Stopwatch         | DOWN   | Reset to 00:00                  |
| Calendar          | —      | Shows the current month; today is highlighted |
| Reminder popup    | MODE   | Dismiss the popup early (it also auto-hides)   |
| Select mascot     | UP     | Next mascot                     |
| Select mascot     | DOWN   | Previous mascot                 |

Brighter/darker settings and the chosen mascot are saved to NVS automatically.

## Event reminders

Up to **10 events** can be stored. Each event has a date, a time (to the minute)
and a short title (max 31 characters). They live in NVS, so they survive a
reboot, and the on-device calendar shows a small **dot under every day that has
an event**.

When the RTC/NTP clock reaches an event's exact minute, the firmware:

1. shows a full-screen **reminder popup** on the OLED for 10 seconds,
2. jumps to the calendar screen so you see the day, and
3. (optionally) pushes a notification — see below.

The popup can be dismissed instantly with the **MODE** button.

### Adding events from your phone

The device runs a small web server on port 80. Once it is connected to Wi-Fi,
find its IP address in the serial log or on your router, then open it in a
phone browser:

```
http://<device-ip>/
```

You get a mobile-friendly form to pick a date and time, type a title and add the
event. Existing events are listed with a **del** link (and there is a
**Clear all** link).

> **Note:** the OLED font is uppercase ASCII only, so non-Latin titles (e.g.
> Thai) will not render on the screen — but they are stored and shown correctly
> in the web UI.

### Push notifications (Telegram / LINE)

Push notifications are **off by default**. Enable them in `menuconfig`:

```bash
idf.py menuconfig
# -> "OLED Clock" -> "Event notifications (optional)"
```

| Option                             | Description                              |
| ---------------------------------- | ---------------------------------------- |
| `OLED_CLOCK_TELEGRAM_ENABLE`       | Turn Telegram notifications on           |
| `OLED_CLOCK_TELEGRAM_BOT_TOKEN`    | Token from [@BotFather](https://t.me/BotFather) |
| `OLED_CLOCK_TELEGRAM_CHAT_ID`      | Your chat/user id (e.g. from `getUpdates`) |
| `OLED_CLOCK_LINE_ENABLE`           | Turn LINE Notify on                      |
| `OLED_CLOCK_LINE_TOKEN`            | LINE Notify personal access token        |

Notifications use HTTPS (`esp_http_client` + the ESP-IDF certificate bundle), so
the clock needs internet access, not just a local network. Sends are
best-effort: a failed POST is logged and does not block the UI.

> LINE Notify has been discontinued by LINE; **Telegram is recommended**.

## Docker details

| File                                 | Purpose                                              |
| ------------------------------------ | ---------------------------------------------------- |
| `docker/Dockerfile`                  | ESP-IDF image + ccache.                              |
| `docker/docker-compose.yml`          | Build/dev service with the project mounted.          |
| `docker/docker-compose.flash.yml`    | Adds `/dev/tty*` device passthrough for Linux/macOS. |
| `Makefile`                           | Convenience targets (`build`, `flash`, `monitor`...).|
| `scripts/clock.ps1`                  | Windows PowerShell wrapper.                          |

Change the ESP-IDF version with the `IDF_VERSION` build arg (default
`release-v5.3`), e.g.:

```bash
IDF_VERSION=release-v5.3 docker compose -f docker/docker-compose.yml build
```

> **Note:** the `build/` directory is tied to a specific ESP-IDF image. After
> changing `IDF_VERSION` (or switching between a native install and Docker),
> run `make fullclean` (or delete `build/`) before rebuilding, otherwise CMake
> will try to reuse a toolchain path that no longer exists.

## Project structure

```
oled_clock_project/
├── CMakeLists.txt              # Top-level ESP-IDF project
├── sdkconfig.defaults          # Reproducible defaults (no secrets)
├── Makefile                    # Docker helpers
├── docker/                     # Container build environment
├── scripts/clock.ps1           # Windows helper
├── main/
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild        # Wi-Fi, pins, timezone, brightness, notify...
│   └── main.c                   # Wi-Fi, SNTP, buttons, events, app loop
└── components/
    ├── ssd1306/                 # OLED driver (SSD1306/SH1106, primitives)
    │   ├── Kconfig              # Panel type, orientation, column offset
    │   ├── CMakeLists.txt
    │   ├── include/ssd1306.h
    │   └── ssd1306.c
    ├── clock_ui/                # Presentation layer
    │   ├── CMakeLists.txt
    │   ├── include/clock_ui.h
    │   └── clock_ui.c           # Screens + procedural mascots
    ├── event_manager/           # Event storage (RAM + NVS)
    │   ├── CMakeLists.txt
    │   ├── include/event_manager.h
    │   └── event_manager.c
    ├── web_server/              # Phone web UI for events
    │   ├── CMakeLists.txt
    │   ├── include/web_server.h
    │   └── web_server.c
    └── notifier/                # Optional Telegram / LINE push
        ├── CMakeLists.txt
        ├── include/notifier.h
        └── notifier.c
```

### Architecture notes

- `ssd1306` owns the framebuffer, the I2C transactions and drawing primitives
  (pixels, lines, rects, circles, scaled text). Every write returns an
  `esp_err_t`, so bus errors are visible instead of silent.
- `clock_ui` is pure presentation: it takes a `ui_state_t` snapshot and draws
  one screen. It knows nothing about Wi-Fi, NVS or the app loop.
- `event_manager` owns the event list and its NVS persistence. It exposes a
  `day_mask()` bitmap (bit `day-1`) that `main` feeds into `ui_state_t`, so the
  UI layer still does not depend on the storage layer.
- `web_server` depends only on `event_manager`; it is a thin form → `add` /
  `remove` shim.
- `notifier` is compiled empty unless a channel is enabled, and skips sending
  when its token is blank.
- `main` glues everything: it samples buttons, tracks time and stopwatch, polls
  for due events, and calls `clock_ui_render()` ~10 times per second.

## Configuration reference (`main/Kconfig.projbuild`)

| Option                          | Default            | Description                          |
| ------------------------------- | ------------------ | ------------------------------------ |
| `OLED_CLOCK_WIFI_SSID`          | *(empty)*          | 2.4 GHz network name                 |
| `OLED_CLOCK_WIFI_PASSWORD`      | *(empty)*          | WPA2 password                        |
| `OLED_CLOCK_WIFI_MAX_RETRY`     | `0` (forever)      | Reconnect attempts before giving up  |
| `OLED_CLOCK_SNTP_SERVER`        | `pool.ntp.org`     | NTP server                           |
| `OLED_CLOCK_TIMEZONE`           | `ICT-7`            | POSIX TZ string                      |
| `OLED_CLOCK_I2C_SDA` / `SCL`    | `21` / `22`        | I2C pins                             |
| `OLED_CLOCK_I2C_SPEED`          | `400000`           | I2C clock in Hz                      |
| `OLED_CLOCK_OLED_ADDR`          | `0x3C`             | OLED I2C address                     |
| `OLED_CLOCK_BTN_*`              | `13/12/14/27`      | Button GPIOs                         |
| `OLED_CLOCK_BTN_DEBOUNCE_MS`    | `50`               | Debounce time                        |
| `OLED_CLOCK_BRIGHTNESS`         | `207`              | Default contrast (16-255)            |
| `OLED_CLOCK_TELEGRAM_*`         | *off / empty*      | Telegram notifications (opt-in)      |
| `OLED_CLOCK_LINE_*`             | *off / empty*      | LINE Notify notifications (opt-in)   |

## Troubleshooting

| Symptom                        | Fix                                                                 |
| ------------------------------ | ------------------------------------------------------------------- |
| Blank / garbled screen         | Check SDA/SCL wiring and address (`0x3C` vs `0x3D`).                 |
| Image shifted right            | You have an SH1106 — enable `CONFIG_SSD1306_PANEL_SH1106`.           |
| Nothing on bricked-looking I2C | Verify 3V3 power and pull-ups; the driver logs I2C errors to UART.   |
| Clock shows `--:--`            | No Wi-Fi/SNTP yet. Check SSID/password and the `WI-FI...` status.    |
| Screen shows `NO WIFI`         | Firmware compiled with an empty SSID — `sdkconfig.defaults` was ignored. Use `menuconfig`, or delete `sdkconfig` and rebuild. |
| `WI-FI...` never becomes `NTP OK` | Credentials are set but association fails. ESP32 needs a **2.4 GHz** SSID; check the disconnect reason code in the serial log. |
| Web UI not reachable           | The device must be on Wi-Fi with a valid IP. Find it in the log (`GOT IP`) and open `http://<ip>/` on the same network. |
| Events missing after flash     | Events live in NVS. A full `esptool erase_flash` wipes them along with Wi-Fi settings. |
| No Telegram/LINE message       | Enable the channel in `menuconfig` **and** set the token; the device needs internet (HTTPS) access. Check the log for `POST ok` / `POST failed`. |
| Non-English title blank on OLED | The OLED font is uppercase ASCII only; non-Latin characters show as spaces on the screen (the web UI still shows them). |
| Text is garbled/blank glyphs   | The built-in font is uppercase-only; keep UI strings uppercase.      |
| `Failed to connect` on flash   | Wrong `PORT`/`COM`, or the board is held in reset.                   |
| `cmake: not found` while building | Stale `build/` from a different ESP-IDF image/toolchain. Run `make fullclean` (or delete `build/`) and rebuild. |

## License

Released under the MIT License. Do whatever you like — attribution is
appreciated.
