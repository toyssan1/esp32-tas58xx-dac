# ESP32-S3 Web MP3 Controller Example

This example runs **on ESP32-S3-WROOM-1** and provides a web page to:
- start/stop MP3 playback from SD
- set amp volume
- set EQ preset (left/right/both)
- list MP3 files from SD mount path (`/`)

## Directory

- `src/main.cpp` → PlatformIO entry
- `arduino/esp32_s3_web_player.ino` → Arduino IDE entry (includes the same `src/main.cpp`)
- `platformio.ini` → standalone PlatformIO config for this example

## Required libraries

### PlatformIO
Already declared in this example `platformio.ini`:
- `ESPAsyncWebServer`
- `ESP8266Audio`

### Arduino IDE
Install these via Library Manager / ZIP:
- **ESPAsyncWebServer**
- **AsyncTCP** (ESP32 dependency for async server)
- **ESP8266Audio**

Also make sure the `tas58xx` library is visible to Arduino IDE.
Options:
1. Copy `lib/tas58xx` into your Arduino libraries folder, or
2. Keep this repository opened and use the local copy if your IDE setup supports it.

## Pin defaults

Defaults are set for your current board wiring and can be overridden with macros:
- I2C: `SDA=18`, `SCL=8`
- I2S: `BCLK=11`, `LRCLK=12`, `DOUT=10`
- SD SPI: `MISO=40`, `MOSI=38`, `SCK=39`, `CS=41`
- TAS58xx: `address=0x4C`, `PDN=17`

## Run with PlatformIO

```bash
cd examples/esp32_s3_web_player
pio run -t upload -t monitor
```

Before upload, edit `platformio.ini` and set:
- `WIFI_SSID`
- `WIFI_PASSWORD`

Then open the printed IP in your browser.

## Run with Arduino IDE

1. Open `examples/esp32_s3_web_player/arduino/esp32_s3_web_player.ino`.
2. Select board: **ESP32S3 Dev Module** (or your exact S3-WROOM-1 board profile).
3. Ensure required libs are installed.
4. Add/adjust macros at top of sketch (or in IDE build flags if used):
   - `WIFI_SSID`, `WIFI_PASSWORD`
   - pin/address macros if your wiring differs.
5. Upload and open Serial Monitor at `115200`.
6. Browse to the printed IP.

## API endpoints

- `GET /api/state`
- `GET /api/files`
- `POST /api/play?file=/path/file.mp3` (file optional)
- `POST /api/stop`
- `POST /api/volume?value=0..100`
- `POST /api/eq_preset?index=0..20&channel=left|right|both`

## Notes

- If `WIFI_SSID` is empty, device starts AP mode:
  - SSID: `tas58xx-web`
  - Password: `tas58xx123`
- MP3 scanning is recursive from SD root `/`.
- EQ presets map to the TAS58xx preset index table (`0..20`).
