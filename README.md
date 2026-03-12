# TAS58xx Driver (PlatformIO / Arduino)

This library provides a `Tas58xx` class for TAS58xx-family amps (including TAS5825 usage in this project).

## First boot checklist

- Confirm wiring: `SDA`, `SCL`, `PDN` (if used), amp power, and shared ground.
- Set the correct I2C address in constructor or build flag (`TAS58XX_ADDRESS`).
- Call `begin(...)` and verify `isPresent()`/ACK success.
- Call `applyMinimalInit()` and `clearFaults()`.
- Set `setControlState(Tas58xx::ControlState::Play)` and `setMute(false)`.
- Start at safe level (`setVolume(0.3f..0.5f)`), then tune EQ/presets.
- If audio fails, run `faults` and `clearfaults`, then re-check.

Known-good startup snippet:

```cpp
#include <Wire.h>
#include "tas58xx.h"

static Tas58xx amp(0x4C);

void setup() {
  Serial.begin(115200);
  if (!amp.begin(Wire, 18, 8, 400000, 17)) return;
  if (!amp.applyMinimalInit()) return;

  amp.clearFaults();
  amp.setControlState(Tas58xx::ControlState::Play);
  amp.setMute(false);
  amp.setVolume(0.4f);
  amp.setMixerMode(Tas58xx::MixerMode::Stereo);
  amp.setEqMode(Tas58xx::EqMode::Presets);
  amp.setEqPreset(Tas58xx::Channel::Left, 0);
  amp.setEqPreset(Tas58xx::Channel::Right, 0);
}

void loop() {}
```

## 1) Basic setup

```cpp
#include <Wire.h>
#include "tas58xx.h"

#ifndef TAS58XX_ADDRESS
#define TAS58XX_ADDRESS 0x4C
#endif

static Tas58xx amp(TAS58XX_ADDRESS);

void setup() {
  Serial.begin(115200);

  // begin(wire, sda, scl, i2c_clock_hz, pdn_pin)
  if (!amp.begin(Wire, 18, 8, 400000, 17)) {
    Serial.println("[AMP] begin failed");
    return;
  }

  if (!amp.applyMinimalInit()) {
    Serial.println("[AMP] minimal init failed");
    return;
  }

  amp.clearFaults();
  amp.setControlState(Tas58xx::ControlState::Play);
  amp.setMute(false);
  amp.setVolume(0.5f);  // 0.0 .. 1.0
}
```

## 2) I2C address override

The class defaults to `0x4C`, but you can override it by passing a different address to the constructor:

```cpp
static Tas58xx amp(0x4F);
```

In this project, `src/main.cpp` already does this through `AMP_I2C_ADDR` (from build flags such as `-D TAS58XX_ADDRESS=0x4C`).

## 3) EQ modes

```cpp
amp.setEqMode(Tas58xx::EqMode::Off);
amp.setEqMode(Tas58xx::EqMode::On15Band);
amp.setEqMode(Tas58xx::EqMode::Biamp15Band);
amp.setEqMode(Tas58xx::EqMode::Presets);
```

Use:
- `On15Band` when you want manual band gain tuning.
- `Presets` when you want the built-in crossover/voicing profiles.

## 4) Presets (per channel)

Presets are selected with:

```cpp
amp.setEqPreset(Tas58xx::Channel::Left, 5);   // LF 100Hz
amp.setEqPreset(Tas58xx::Channel::Right, 15); // HF 100Hz
```

Valid preset indices are `0..20`:

| Index | Name |
|---:|---|
| 0 | Flat |
| 1 | LF 60Hz |
| 2 | LF 70Hz |
| 3 | LF 80Hz |
| 4 | LF 90Hz |
| 5 | LF 100Hz |
| 6 | LF 110Hz |
| 7 | LF 120Hz |
| 8 | LF 130Hz |
| 9 | LF 140Hz |
| 10 | LF 150Hz |
| 11 | HF 60Hz |
| 12 | HF 70Hz |
| 13 | HF 80Hz |
| 14 | HF 90Hz |
| 15 | HF 100Hz |
| 16 | HF 110Hz |
| 17 | HF 120Hz |
| 18 | HF 130Hz |
| 19 | HF 140Hz |
| 20 | HF 150Hz |

## 5) Manual EQ band gain (per channel)

Manual band API:

```cpp
// setEqBandGain(channel, band_index, gain_db)
amp.setEqBandGain(Tas58xx::Channel::Left, 0, +3);   // 20 Hz +3 dB
amp.setEqBandGain(Tas58xx::Channel::Left, 10, -2);  // 2 kHz -2 dB
amp.setEqBandGain(Tas58xx::Channel::Right, 14, +1); // 16 kHz +1 dB
```

Rules:
- `band_index`: `0..14`
- `gain_db`: `-15..+15`

Band map:

| Band index | Center frequency |
|---:|---|
| 0 | 20 Hz |
| 1 | 31.5 Hz |
| 2 | 50 Hz |
| 3 | 80 Hz |
| 4 | 125 Hz |
| 5 | 200 Hz |
| 6 | 315 Hz |
| 7 | 500 Hz |
| 8 | 800 Hz |
| 9 | 1250 Hz |
| 10 | 2000 Hz |
| 11 | 3150 Hz |
| 12 | 5000 Hz |
| 13 | 8000 Hz |
| 14 | 16000 Hz |

## 6) Channel gain and mixer

```cpp
amp.setChannelVolumeDb(Tas58xx::Channel::Left, -3);   // dB
amp.setChannelVolumeDb(Tas58xx::Channel::Right, -3);  // dB

amp.setMixerMode(Tas58xx::MixerMode::Stereo);
```

## 7) Fault handling

```cpp
Tas58xx::FaultStatus faults{};
if (amp.readFaults(faults)) {
  bool any_fault = Tas58xx::hasAnyFault(faults, true);
  bool non_clock = Tas58xx::hasNonClockFault(faults);
  bool clock_only = Tas58xx::hasClockFault(faults);

  if (any_fault) {
    amp.clearFaults();
  }
}
```

## 8) Recommended tuning flow

1. `begin()` and `applyMinimalInit()`.
2. `setEqMode(...)`:
   - `Presets` for crossover voicing, or
   - `On15Band` for manual tuning.
3. Apply per-channel settings:
   - `setEqPreset(...)` for preset mode, or
   - `setEqBandGain(...)` for manual mode.
4. Set output level (`setVolume`, `setChannelVolumeDb`) and mixer mode.
5. Poll faults and clear when needed.

## 9) Console command quick reference

These are the runtime commands currently supported by this project in `src/main.cpp`:

| Command | Description |
|---|---|
| `v:0..10` | Master volume in 10% steps |
| `b:0..10` | Bass tone step (live update) |
| `t:0..10` | Treble tone step (live update) |
| `eqmode:0..3` | EQ mode: `0=Off`, `1=15-band`, `2=Biamp 15-band`, `3=Presets` |
| `mix:0..4` | Mixer mode: `0=Stereo`, `1=StereoInverse`, `2=Mono`, `3=Right`, `4=Left` |
| `preL:<idx\|name>` | Left preset by index (`0..20`) or name |
| `preR:<idx\|name>` | Right preset by index (`0..20`) or name |
| `presets` | Print preset index/name list |
| `eqL:<band>:<gain>` | Left manual EQ band gain (`band 0..14`, `gain -15..15`) |
| `eqR:<band>:<gain>` | Right manual EQ band gain (`band 0..14`, `gain -15..15`) |
| `chvL:<db>` | Left channel volume in dB (`-24..24`) |
| `chvR:<db>` | Right channel volume in dB (`-24..24`) |
| `faults` | Read and print current fault status |
| `clearfaults` | Clear fault registers and print clear counter |
| `play` | Start SD-backed MP3 playback |
| `stop` | Stop playback |
| `help` or `?` | Print command help |

Example command session:

```text
eqmode:3
preL:LF 90Hz
preR:14
mix:2
v:6
eqL:4:3
eqR:10:-2
chvL:-1
chvR:-1
faults
```

## 10) Troubleshooting

### No sound

- Confirm bring-up order: `begin()` → `applyMinimalInit()` → `setControlState(Play)` → `setMute(false)`.
- Verify non-zero level: use `setVolume(0.5f)` (or console `v:5`) and check channel trims (`chvL`, `chvR`) are not very low.
- Validate source path and playback state if using SD/MP3 (`play` should report started playback).
- Check mixer mode: use `mix:0` (stereo) as baseline while testing.

### No I2C ACK

- Verify wiring and pin config (`SDA`, `SCL`, shared ground, pull-ups if required by your board).
- Confirm the device address: set the constructor address or `-D TAS58XX_ADDRESS=0x..` build flag to your hardware value.
- Check PDN/reset behavior: ensure the PDN pin is correct and toggled high before first register access.
- Start with 400 kHz or lower I2C clock and retry.

### Faults keep returning

- Read first, then clear: run `faults`, then `clearfaults`, then re-check to see what persists.
- Distinguish clock vs non-clock issues using `hasClockFault()` / `hasNonClockFault()`.
- Persistent non-clock faults usually indicate output stage, load, thermal, or power issues.
- Validate power rail stability, speaker/load wiring, and temperature under load.
