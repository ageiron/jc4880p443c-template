# CapabilityDemo

A touchscreen capability demo for the **Guition JC4880P443C_I_W** board (ESP32-P4 + ESP32-C6,
4.3" 480×800 MIPI-DSI touchscreen). One home-screen launcher, one demo screen per hardware
capability — see [FUNCTIONAL_DESCRIPTION.md](FUNCTIONAL_DESCRIPTION.md) for the full spec.

Built from `jc4880p443c-template` — display, touch, PSRAM, and WiFi bring-up were already solved
there; this project adds audio (speaker + mic), SD card + video playback, and a cloud-backed AI
voice assistant on top.

## Capabilities

| Screen           | What it proves works                                                  |
|-------------------|--------------------------------------------------------------------------|
| Launcher (home)   | Display + touch                                                          |
| WiFi scan         | ESP32-C6 WiFi path (read-only — lists nearby APs, doesn't join)          |
| WiFi connect      | Actually joining a network (needed for the AI assistant)                 |
| Storage & video   | TF/SD card mount + file listing + `.avi` (MJPEG) video playback          |
| Audio             | ES8311 codec — speaker playback, mic level meter, record/playback loop   |
| Settings          | On-device entry of the two API keys the assistant needs                  |
| AI voice assistant| Tap-to-talk: mic → OpenAI Whisper (STT) → Anthropic Claude → OpenAI TTS → speaker |

Not built (hardware not attached to this board — see FUNCTIONAL_DESCRIPTION.md "Explicitly out
of scope" for the pin references anyway): RS485, MIPI-CSI camera, battery monitoring, GPIO
expansion header.

## Setup

1. **TF card**: FAT32, <32 GB. Copy the `MJPEG` folder from the vendor package
   (`1-Demo\4.3 TF card video file\MJPEG\`) onto it to get sample video clips for the storage
   screen.
2. **Speaker**: wire to the board's `SPEAKER_P`/`SPEAKER_N` terminal.
3. **API keys**: none needed to build/flash — enter them on-device via the Settings screen after
   first boot (stored in NVS, never in source; see FUNCTIONAL_DESCRIPTION.md "Security / secrets").
4. **WiFi**: only needed for the assistant screen; connect on-device via the WiFi connect screen.

## Build & flash

```
pio run                    # build only (downloads ESP-IDF ~1GB on first run)
pio run --target upload    # build + flash
pio device monitor         # serial console at 115200 baud
```

See [docs/BRINGUP.md](docs/BRINGUP.md) for upload caveats (esptool path, COM port, Unicode build
quirk) and the full verified hardware pin reference.

## Project layout

```
src/
  main.cpp                  — app_main: board bring-up (display/PSRAM/touch) + launches the UI
  ui/                        — one file per screen + shared nav/keyboard-dialog helpers
  services/                  — hardware/network glue: NVS config, WiFi, SD card, audio codec,
                                HTTPS client, AI assistant pipeline orchestration
  esp_lcd_st7701*.c/.h       — vendor ST7701 MIPI-DSI display driver
docs/
  BRINGUP.md                 — every hardware gotcha hit bringing this board up, and why each fix works
  JC4880P443C_I_W Specifications-EN-V1.0*.pdf — vendor spec sheet
FUNCTIONAL_DESCRIPTION.md    — living spec: capabilities, pin map, external APIs, security, success criteria
CLAUDE.md                    — agent context (stack, conventions)
```

## Conventions

See [CLAUDE.md](CLAUDE.md). In short: read `FUNCTIONAL_DESCRIPTION.md` and `docs/BRINGUP.md`
before touching anything, secrets go in NVS not source, build/flash/monitor via the VS Code
tasks, commit and push manually after meaningful changes.
