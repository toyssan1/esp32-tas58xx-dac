# ESP32-S3 Web MP3 Player + TAS58xx Amplifier

A complete web-controlled MP3 player for the **ESP32-S3** with a **TAS5805M / TAS5825M** class-D amplifier. The device creates a WiFi access point and serves a mobile-friendly web UI for playback control, volume, and per-channel EQ crossover presets.

## Features

- MP3 playback from SD card via I2S to a TAS58xx amplifier
- WiFi Access Point with built-in web server (no router needed)
- Volume slider (0–100%)
- Separate LF and HF crossover preset selectors (per-channel EQ)
- SD card file browser with one-tap playback
- Real-time state display (JSON)
- Full serial debug output at 115200 baud

## Hardware requirements

| Component | Details |
|-----------|---------|
| MCU | ESP32-S3-DevKitC-1 (or any ESP32-S3 board) |
| Amplifier | TAS5805M or TAS5825M on I2C |
| Storage | MicroSD card with `.mp3` files |
| Audio | I2S connection from ESP32-S3 to TAS58xx |

## Default pin mapping

All pins can be overridden via build flags or `#define` before including headers.

| Function | Pin | Build flag |
|----------|-----|-----------|
| I2C SDA | 18 | `PIN_I2C_SDA` |
| I2C SCL | 8 | `PIN_I2C_SCL` |
| I2S BCLK | 11 | `PIN_I2S_BCLK` |
| I2S LRCLK | 12 | `PIN_I2S_LRCLK` |
| I2S DOUT | 10 | `PIN_I2S_DOUT` |
| SD MISO | 40 | `PIN_SD_MISO` |
| SD MOSI | 38 | `PIN_SD_MOSI` |
| SD SCK | 39 | `PIN_SD_SCK` |
| SD CS | 41 | `PIN_SD_CS` |
| TAS58xx PDN | 17 | `TAS58XX_GPIO_PDN` |
| TAS58xx I2C addr | 0x4C | `TAS58XX_ADDRESS` |

## Directory structure

```
examples/esp32_s3_web_player/
├── platformio.ini                         # PlatformIO project config
├── src/main.cpp                           # PlatformIO source
├── arduino/esp32_s3_web_player.ino        # Arduino IDE sketch (same code)
└── README.md
```

Both files contain identical application code. The `.ino` omits `#include <Arduino.h>` since the Arduino IDE adds it automatically.

---

## Option A: PlatformIO (recommended)

### 1. Configure WiFi and pins

Edit `platformio.ini` to set your AP network name and password:

```ini
build_flags =
  -D WIFI_SSID=\"my-amp\"
  -D WIFI_PASSWORD=\"mypassword\"
  -D TAS58XX_ADDRESS=0x4C
  -D TAS58XX_GPIO_PDN=17
  ; ... pin overrides as needed
```

> If `WIFI_SSID` is left empty, it defaults to `tas58xx-web` / `tas58xx123`.

### 2. Build and upload

```bash
cd examples/esp32_s3_web_player
pio run -t upload
```

### 3. Monitor serial output

```bash
pio device monitor
```

You should see boot output like:

```
============================
ESP32-S3 TAS58xx Web Player
============================
CPU freq: 240 MHz
Free heap: 280000 bytes

[SETUP] init_wifi...
[WIFI] Starting AP: SSID="tas58xx-web" PASS="tas58xx123"
[WIFI] AP IP: 192.168.4.1
[SETUP] init_amp...
[AMP] ready (total 245 ms)
[SETUP] init_sd...
[SD] mounted, cardType=2, size=14910 MB
[SD] cache built: 5 files in 82 ms
[SETUP] init_web...
[WEB] server started

[SETUP] ===== READY =====
```

### 4. Connect and use

1. On your phone or laptop, connect to the WiFi network (default: `tas58xx-web` / `tas58xx123`)
2. Open a browser to **http://192.168.4.1**
3. Use the web UI to browse files, start playback, adjust volume, and set EQ

### Dependencies

Managed automatically by PlatformIO via `lib_deps`:

- `earlephilhower/ESP8266Audio` — MP3 decoder + I2S output
- `tas58xx` — TAS58xx amplifier driver (this repository)

---

## Option B: Arduino IDE

### 1. Install board support

1. Open **Arduino IDE** > **Preferences** > **Additional Board Manager URLs**
2. Add: `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. Open **Board Manager**, search **esp32**, and install **esp32 by Espressif Systems**

### 2. Install libraries

Install via **Sketch > Include Library > Manage Libraries...**:

- **ESP8266Audio** by Earle Philhower

Then install the **tas58xx** library:
- Download or clone this repository
- Copy the repository root folder into your Arduino libraries directory  
  (e.g., `~/Arduino/libraries/tas58xx/`)

### 3. Configure WiFi and pins

Open `arduino/esp32_s3_web_player.ino` and edit the defaults near the top of the file:

```cpp
#ifndef WIFI_SSID
#define WIFI_SSID "my-amp"        // your AP network name
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "mypassword" // your AP password (8+ chars)
#endif
```

Adjust pin defines if your wiring differs from the defaults.

### 4. Select board and upload

1. **Tools > Board** > **ESP32S3 Dev Module**
2. **Tools > USB CDC On Boot** > **Enabled** (for serial output via USB)
3. **Tools > Flash Size** > **8MB** (or match your board)
4. **Tools > PSRAM** > **OPI PSRAM** (if your board has PSRAM)
5. Select port and click **Upload**

### 5. Monitor and connect

1. Open **Serial Monitor** at **115200** baud
2. Press the board reset button if needed — watch for the `===== READY =====` message
3. Connect to the WiFi AP and browse to **http://192.168.4.1**

---

## Web UI

The built-in web interface provides:

- **Start / Stop** buttons for MP3 playback
- **Volume** slider (0–100%)
- **LF Crossover** dropdown — sets the left channel EQ preset (Flat, 60–150 Hz)
- **HF Crossover** dropdown — sets the right channel EQ preset (Flat, 60–150 Hz)
- **File list** — tap any MP3 file on the SD card to play it
- **State panel** — live JSON view of the current device state

## REST API

All endpoints return JSON. Use `Content-Type: application/json` responses.

| Method | Endpoint | Parameters | Description |
|--------|----------|------------|-------------|
| GET | `/` | — | Web UI (HTML page) |
| GET | `/api/state` | — | Current player state |
| GET | `/api/files` | — | List MP3 files on SD |
| POST | `/api/play` | `file` (optional) | Start playback; defaults to first file |
| POST | `/api/stop` | — | Stop playback |
| POST | `/api/volume` | `value` (0–100) | Set volume |
| POST | `/api/eq_preset` | `index` (0–20), `channel` (left/right/both) | Set EQ preset |

### EQ preset index mapping

| Index | Name |
|-------|------|
| 0 | Flat (no filter) |
| 1–10 | LF crossover 60–150 Hz |
| 11–20 | HF crossover 60–150 Hz |

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| No WiFi network visible | Check serial output for `[WIFI]` messages. Ensure password is 8+ characters. |
| SD mount failed | Verify SPI pin wiring. Check that an SD card is inserted and formatted as FAT32. |
| AMP begin failed | Check I2C wiring (SDA/SCL), TAS58xx power, and I2C address (`0x4C` or `0x4E`). |
| No sound | Verify I2S wiring. Check that volume is >0 and the amp is not muted in serial output. |
| Playback slow to start | Normal on first play — I2S bus initialization takes a moment. Subsequent plays are faster. |
