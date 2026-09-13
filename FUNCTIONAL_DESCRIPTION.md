# Functional Description — CapabilityDemo

> **Living document.** Update this file whenever scope, requirements, or functionality change.
> Both the developer and any coding agent (Claude Code, local LLM) should treat this as the
> single source of truth for what the project is supposed to do.

- **Created:** 2026-09-12
- **Last updated:** 2026-09-12
- **Slug:** CapabilityDemo
- **Target platform:** Guition JC4880P443C_I_W (ESP32-P4 + ESP32-C6), module `JC-ESP32P4-M3`
- **Primary language(s):** C++ (ESP-IDF framework)

## Purpose

A single firmware app that exercises the JC4880P443C_I_W board's capabilities end to end, as a
touchscreen launcher with one demo screen per capability. It doubles as a reference for what
this board can do and as a "smart display" — the marquee screen is a tap-to-talk AI voice
assistant (mic → cloud speech-to-text → Claude reply → cloud text-to-speech → speaker).

This is explicitly a **capability demo**, not a product: each screen proves one piece of
hardware/integration works, not a polished end-user app.

## Hardware

See [docs/BRINGUP.md](docs/BRINGUP.md) for the full hardware reference table (all pins below,
plus display/PSRAM/chip-revision bring-up gotchas already solved by this template).

Base board: ESP32-P4 (dual-core RISC-V, no native WiFi) + ESP32-C6 co-processor (WiFi/BT over
SDIO), 4.3" 480×800 MIPI-DSI ST7701 display, GT911 capacitive touch, 16 MB flash, 32 MB PSRAM.
Additional hardware this project uses: ES8311 audio codec (speaker DAC + mic ADC in one chip),
TF/SD card slot (4-bit SDMMC), Class-D speaker amp.

The vendor also ships schematics/datasheets/demo code for this exact module at
`C:\Dev\JC4880P443C_I_W\` (outside this repo — see `docs/BRINGUP.md` for what was extracted from
it and why).

## Framework / stack

- **Build system:** PlatformIO + [pioarduino/platform-espressif32](https://github.com/pioarduino/platform-espressif32) (stable release)
- **Framework:** ESP-IDF 5.5.x (downloaded automatically by PlatformIO on first build, ~1 GB)
- **UI library:** LVGL 9.2+ (managed component `lvgl/lvgl`)
- **Display driver:** ST7701 vendor files in `src/` (BSP does not support this panel — see `docs/BRINGUP.md`)
- **WiFi:** ESP-HOSTED via `espressif/esp_hosted` + `espressif/esp_wifi_remote` managed components
- **Audio codec:** `espressif/esp_codec_dev` (generic codec abstraction) + `espressif/es8311`
  (ES8311 driver), following the proven init pattern found in the vendor's own
  `esp32_p4_function_ev_board.c` reference (adapted to this board's own I2C/I2S pins — see below).
- **Video playback:** `espressif/avi_player` (AVI container + MJPEG frame decode)
- **HTTPS:** `esp_http_client` + `esp-tls` (bundled CA cert store via `esp_crt_bundle`)
- **JSON:** `cJSON` (ESP-IDF built-in `json` component)

## Capabilities

Each capability is a screen reachable from the home-screen launcher (`src/ui/launcher.cpp`).
Screens share a small navigation helper (`src/ui/nav.cpp`) for a consistent back-button pattern.

### 1. Home / Launcher (`ui/launcher.cpp`)

Grid of tiles, one per capability below. This *is* the display+touch capability demo — if you
can see and tap the tiles, display and touch both work. (The original template's tap-counter
screen is replaced by this.)

### 2. WiFi scan (`ui/screen_wifi_scan.cpp`, `services/wifi_service.cpp`)

Scans for nearby access points (`esp_wifi_scan_start`) and lists SSID / RSSI / auth type.
**Read-only — does not join a network.** Proves the ESP32-C6 WiFi path (ESP-HOSTED over SDIO)
works independently of anything needing internet access.

### 3. WiFi connect (`ui/screen_wifi_connect.cpp`)

Separate screen from the scan tile, because the AI assistant (capability 7) needs a *real*
internet connection and scanning alone doesn't provide one. Uses the shared
`ui/keyboard_dialog.cpp` (LVGL on-device keyboard) to enter SSID + password, calls
`wifi_service::connect()`, and persists credentials via `services/nvs_config.cpp` so re-flashing
firmware doesn't require re-entering WiFi credentials every time.

### 4. Storage & video (`ui/screen_storage.cpp`, `services/sdcard_service.cpp`)

Mounts the TF/SD card over SDMMC+FATFS at `/sdcard`, shows capacity/free space, and lists files.
Tapping an `.avi` file plays it full-screen via `avi_player` — this plays the vendor's own
sample clips already present on a card prepped per `1-Demo/Instructions.txt` (copy the `MJPEG`
folder from `1-Demo/4.3 TF card video file/` onto a FAT32, <32GB TF card).

### 5. Audio: speaker + mic (`ui/screen_audio.cpp`, `services/audio_service.cpp`)

- **Speaker:** buttons to play a generated tone and (if present) a bundled sample; volume slider.
  Routed through the onboard Class-D amp (PA enable = GPIO11).
- **Mic:** live input-level meter (VU bar) driven by continuous small reads from the codec's ADC
  path, plus a record-then-playback loop button (record N seconds to a PSRAM buffer, then play
  it back through the speaker) as a simple "does the mic actually work" proof.

Both directions go through the *same* ES8311 chip — this board doesn't have a separate mic
codec, despite one schematic net being named `ES7210_SDOUT` (see pin table below).

### 6. Settings — API keys and reply backend (`ui/screen_settings.cpp`)

Two entry paths for the Anthropic and OpenAI API keys (`services/nvs_config.cpp`, shown masked
e.g. `sk-...ab12` once saved, **never hardcoded in source** — see Security below):
- **Serial console** (`services/serial_console.cpp`) — the primary path. Keys run 50-100+
  characters, impractical on a 4.3" on-screen keyboard, and the board is already tethered to a PC
  over USB for flashing anyway. Send `SET_ANTHROPIC_KEY:<key>` / `SET_OPENAI_KEY:<key>` over the
  same serial connection (e.g. the PlatformIO monitor).
- **On-device keyboard** (`ui/keyboard_dialog.cpp`) — a fallback for anyone without serial access.

Also configures the **reply backend**: a toggle switches the assistant's "thinking" step between
the Anthropic Messages API (default) and a **local LLM** — an LM Studio or Ollama server on the
same LAN, reached via its OpenAI-compatible `/v1/chat/completions` endpoint (no API key). Needs a
URL (e.g. `http://192.168.1.50:1234`) and, for Ollama specifically, a model name matching an
installed tag exactly (LM Studio ignores this field). The OpenAI key is always required
regardless of this toggle — Whisper STT and TTS never go through the local server.

### 7. AI voice assistant (`ui/screen_assistant.cpp`, `services/ai_service.cpp`)

Tap-to-talk: press record, speak, release; the device shows state (listening → thinking →
speaking) and a running text transcript of the conversation. Pipeline (see External APIs below):

```
mic (audio_service) --WAV--> OpenAI Whisper (STT, forced language=en)
                                   |
                                   v  transcribed text
                    Anthropic Messages API, or a local LM Studio/Ollama
                    server on the LAN (Settings toggle, capability 6)
                                   |
                                   v  reply text
                             OpenAI TTS (PCM)
                                   |
                                   v
                          speaker (audio_service)
```

Requires: WiFi connected (capability 3), an OpenAI key set (capability 6), and a reply backend
configured — either the Anthropic key, or the local-LLM toggle + URL. The assistant screen should
clearly tell the user which prerequisite is missing rather than failing silently.

## Explicitly out of scope (this iteration)

These were identified from the schematics while researching the audio/storage capabilities
above but are **not** built into this app — the user's board doesn't currently have this
hardware wired up, and building UI for hardware that isn't attached would be untestable and
misleading. Kept here so a future session doesn't have to re-derive them from the schematics:

- **RS485**: onboard MAX485 transceiver with *hardware* auto-direction (a 74LVC1G132 gate +
  transistor drive DE/RE from TX activity — no GPIO needed for direction control). UART lines:
  TX=GPIO26, RX=GPIO27.
- **MIPI-CSI camera** (OV02C10 per the vendor's `video_lcd_display` example): shares the
  touch/audio I2C bus (GPIO7/8). Camera module not currently attached to this board.
- **Battery / power monitoring** (IP5306 fuel-gauge/boost IC on VOUT-BAT): vendor has a matching
  `espressif/adc_battery_estimation` managed component if this gets picked up later. No battery
  currently connected.
- **GPIO expansion header** (`JP1`: GPIO52/51/50/49/35/34/32/29 broken out): needs external
  LEDs/buttons/scope to be a meaningful demo.

## Hardware pin reference (this project's additions)

See `docs/BRINGUP.md` for the full table including display/touch/PSRAM (already verified by the
template). New pins added for this project's capabilities:

| Signal                          | Pin      | Notes                                              |
|----------------------------------|----------|-----------------------------------------------------|
| ES8311 I2C (SDA/SCL)             | GPIO7/8  | **Shared with GT911 touch bus** — same I2C master   |
| ES8311 I2C address (8-bit)       | 0x30     | `ES8311_CODEC_DEFAULT_ADDR` — not the datasheet's 7-bit `0x18`, see `docs/BRINGUP.md`. |
| I2S MCLK                         | GPIO13   |                                                      |
| I2S BCLK (SCLK)                  | GPIO12   |                                                      |
| I2S WS (LRCK)                    | GPIO10   |                                                      |
| I2S DSDIN (ESP→codec, speaker)   | GPIO9    |                                                      |
| I2S ASDOUT (codec→ESP, mic)      | GPIO48   | Schematic net name says `ES7210_SDOUT` but it's wired to the same ES8311 — this board has no separate mic codec. |
| Speaker PA enable                | GPIO11   | Class-D amp (NS4150) shutdown/enable                |
| SD/TF SDMMC CLK                  | GPIO43   | Fixed silicon IOMUX pin for SDMMC **Slot 0** (`SDMMC_SLOT0_IOMUX_PIN_NUM_CLK`) — not GPIO-matrix routed. Confirmed against ESP-IDF's own `SDMMC_SLOT_CONFIG_DEFAULT()` for esp32p4. |
| SD/TF SDMMC CMD                  | GPIO44   | Slot 0 fixed pin.                                    |
| SD/TF SDMMC D0-D3                | GPIO39-42| Slot 0 fixed pins.                                   |
| SD/TF card power                 | On-chip LDO channel 4 | `sd_pwr_ctrl_new_on_chip_ldo()`, **not** a GPIO — the schematic's `ESP_LDO_VO4` net feeds a permanently-on external MOSFET (R10 unpopulated), so the LDO channel is the only real power gate. See `docs/BRINGUP.md`. |
| WiFi (ESP-HOSTED) SDMMC slot     | Slot 1   | GPIO-matrix routed (CLK=18/CMD=19/D0-D3=14-17). Independent from the TF card's Slot 0 — **must** be selected explicitly (`host.slot = SDMMC_HOST_SLOT_0` for the TF card), since `SDMMC_HOST_DEFAULT()` defaults to Slot 1. |

## External APIs / integrations

Called from `services/ai_service.cpp` via `services/http_client.cpp` (`esp_http_client` +
`esp_crt_bundle` for TLS trust). STT and TTS always require internet access (WiFi connected via
capability 3) and are billed, metered OpenAI APIs. The reply step is either the same (Anthropic,
billed) or a LAN-only local server (no billing, no internet needed for that step specifically).

| Purpose                | Provider | Endpoint                                          | Auth                              |
|-------------------------|----------|----------------------------------------------------|-------------------------------------|
| Speech-to-text          | OpenAI   | `POST /v1/audio/transcriptions` (multipart, `model=whisper-1`, `language=en`) | `Authorization: Bearer <openai_key>` |
| Assistant reply (text)  | Anthropic| `POST /v1/messages` (JSON)                          | `x-api-key: <anthropic_key>`, `anthropic-version: 2023-06-01` |
| Assistant reply (text), local option | LM Studio / Ollama (user-hosted, LAN) | `POST <local_llm_url>/v1/chat/completions` (JSON, OpenAI-compatible) | none — LAN-only, no key |
| Text-to-speech          | OpenAI   | `POST /v1/audio/speech` (JSON, `response_format: "pcm"`) | `Authorization: Bearer <openai_key>` |

`response_format: "pcm"` is deliberate — it avoids needing an MP3/Opus decoder on-device; the
response bytes are raw PCM fed straight into `audio_service::play_pcm()`. `language: "en"` on the
Whisper request is also deliberate — without it, Whisper auto-detects the spoken language, and a
short/noisy recording can be mis-detected (confirmed on hardware: a reply came back in Spanish).

Exact request/response field names for the managed components involved
(`esp_codec_dev`/`es8311`/`avi_player`) get double-checked against whatever version PlatformIO
resolves on first build — if a version mismatch causes a build or behavior surprise, it goes in
`docs/BRINGUP.md`, the same "trial and error, then document it" pattern already used for display
bring-up.

## Security / secrets

- WiFi password, Anthropic API key, and OpenAI API key are entered on-device — either over the
  serial console (`services/serial_console.cpp`, the primary path for the two API keys, which run
  too long for the on-screen keyboard to be practical) or the LVGL keyboard
  (`ui/keyboard_dialog.cpp`) — and stored in NVS under namespace `capdemo`
  (`services/nvs_config.cpp`). The local-LLM URL and model name are stored the same way, though
  neither is a secret (no auth to a LAN-only server).
- **Never hardcoded in source** beyond throwaway local testing, per this project's `CLAUDE.md`.
- API keys are displayed masked once saved (e.g. `sk-...ab12`), never shown in full or logged.
- No secrets are transmitted anywhere except directly to their owning provider's HTTPS endpoint
  over TLS (cert validated via the ESP-IDF bundled CA store) — or, for the local-LLM option, to
  the user's own LAN server over plain HTTP (no secret involved, since there's no key).

## WiFi credentials

Entered on-device via capability 3 (`ui/screen_wifi_connect.cpp`); persisted in NVS. Not
hardcoded, not in this repo.

## Build & flash

```
pio run                    # build only
pio run --target upload    # build + flash (see docs/BRINGUP.md for upload caveats)
pio device monitor         # serial console at 115200 baud
```

ESP-IDF toolchain (~1 GB) downloads automatically on the first `pio run`.

## Key source files

| File                          | Purpose                                                       |
|--------------------------------|------------------------------------------------------------------|
| `platformio.ini`               | PlatformIO env, platform, board, build flags, custom upload command |
| `sdkconfig.defaults`           | ESP-IDF Kconfig defaults (flash, PSRAM, display, WiFi, LVGL)  |
| `partitions.csv`               | Flash partition table (16 MB, factory app at 0x20000)         |
| `src/main.cpp`                 | App entry point: verified board bring-up (unchanged) + service init + launches `ui/launcher.cpp` |
| `src/ui/launcher.cpp`          | Home screen — tile grid, one per capability                    |
| `src/ui/nav.cpp`               | Shared screen-stack / back-button helper                       |
| `src/ui/keyboard_dialog.cpp`   | Reusable on-device text-entry modal (SSID/password/API keys)   |
| `src/ui/screen_wifi_scan.cpp`  | WiFi scan, read-only                                            |
| `src/ui/screen_wifi_connect.cpp`| WiFi join flow, needed for the assistant                       |
| `src/ui/screen_storage.cpp`    | SD card browser + `.avi` video playback                        |
| `src/ui/screen_audio.cpp`      | Speaker + mic demo                                              |
| `src/ui/screen_settings.cpp`   | API key entry                                                   |
| `src/ui/screen_assistant.cpp`  | Tap-to-talk AI assistant UI                                     |
| `src/services/nvs_config.*`    | Typed NVS get/set for WiFi creds + API keys                    |
| `src/services/wifi_service.*`  | `esp_wifi` scan/connect wrapper                                 |
| `src/services/sdcard_service.*`| SDMMC+FATFS mount, listing, free space                          |
| `src/services/audio_service.*` | ES8311 codec init (`esp_codec_dev`+`es8311`), play/record       |
| `src/services/http_client.*`   | HTTPS JSON/multipart POST helper (TLS trust, auth headers)      |
| `src/services/ai_service.*`    | Orchestrates Whisper → Claude → TTS pipeline                    |
| `src/esp_lcd_st7701*.c/.h`     | ST7701 MIPI-DSI driver (vendor files, see `docs/BRINGUP.md`)   |
| `docs/BRINGUP.md`              | Verified hardware bring-up gotchas — read before editing init code |

## Known gotchas

See [docs/BRINGUP.md](docs/BRINGUP.md) — board-specific gotchas (chip revision, PSRAM, display,
audio codec, SD card, WiFi) live there since they apply to any project on this board, not just
this one.

## Non-functional requirements

- Every screen must be reachable and exitable from the launcher without a reboot.
- No screen should hang indefinitely on a failed network/hardware call — surface an error state
  and let the user back out.
- Secrets never appear in logs (`ESP_LOGx`) or on-screen in full.

## Success criteria

- Launcher shows and all tiles navigate to their screen and back.
- WiFi scan lists real nearby APs.
- WiFi connect joins a real network using on-device-entered credentials, persists across reboot.
- SD card screen shows real capacity/free space and file list from an inserted card; tapping the
  vendor sample `.avi` plays it.
- Speaker screen produces audible sound; mic screen's level meter visibly responds to sound;
  record/playback loop is audible.
- Settings screen saves both API keys, shown masked after saving.
- Assistant screen: with WiFi connected and both keys set, a full tap-to-talk round trip
  (record → transcript text appears → assistant reply text appears → reply is spoken) completes.
- `pio run` builds clean.

## Out of scope

- RS485, MIPI-CSI camera, battery monitoring, GPIO expansion header (see "Explicitly out of
  scope" above — documented for later, not implemented now).
- On-device/offline speech recognition or TTS (would need a local model far beyond what fits
  this board's compute/RAM budget) — the assistant is cloud-backed by design.
- Multi-turn conversation memory across app restarts (each assistant session starts fresh).
- Production-grade UX polish — this is a capability demo, not a shipping product.

## Change log

- 2026-09-12 — Project scoped from `jc4880p443c-template` into a full capability-demo app:
  launcher + WiFi scan/connect + SD card & video playback + audio (speaker/mic) + AI voice
  assistant (OpenAI Whisper STT + Anthropic Claude + OpenAI TTS). Pin map for audio codec, SD
  card, RS485, camera, and battery reverse-engineered from vendor schematics
  (`C:\Dev\JC4880P443C_I_W\5-Schematic\`) and cross-checked against vendor sample code.
  `pio run` builds clean.

  **Flashed and tested on real hardware this session** (board on COM6). Confirmed working:
  display, touch, PSRAM, launcher navigation, WiFi scan (real nearby APs), WiFi connect (joins a
  real network), audio speaker + mic (both directions), and SD card mount + file listing (real
  files, real free/total space). Three real bugs found and fixed by on-hardware testing (full
  detail in `docs/BRINGUP.md`, since none were visible from a clean build):
  1. WiFi crashed the app — `esp_hosted_init()`/`esp_hosted_connect_to_slave()` were never
     called before `esp_wifi_init()`.
  2. `esp_wifi_scan_start()` raced the C6 co-processor's own startup for ~2-3s after
     `esp_wifi_start()` — fixed with a bounded retry.
  3. SD card: `SDMMC_HOST_DEFAULT()` defaults to the same SDMMC slot (Slot 1) WiFi uses, and the
     TF card's power comes from an on-chip LDO channel that's off by default — neither was
     obvious from the schematic alone. Fixed by explicitly selecting Slot 0 (WiFi's independent
     Slot 1 is unaffected) and enabling on-chip LDO channel 4 via `sd_pwr_ctrl_new_on_chip_ldo()`.
  4. Audio: two separate `esp_codec_dev` instances instead of one shared duplex device, I2S slot
     mode needed to be STEREO despite mono content, and the ES8311 I2C address was the
     datasheet's 7-bit `0x18` instead of the driver's expected 8-bit `0x30` — cross-checked
     against `xiaozhi-esp32`'s exact-board port (`guition-jc4880p443`) in the vendor SDK package.

  Not yet exercised on hardware: video playback (`avi_player`) and the full AI assistant
  pipeline (the three external HTTP APIs are a first integration, most likely to need iteration
  once tested against live responses).

- 2026-09-12 (continued) — Extended hardware testing session: **video playback and WiFi UX now
  fully confirmed working too.** Real bugs found and fixed (full detail in `docs/BRINGUP.md`):
  5. Video/audio playback: `avi_player` only demuxes, it doesn't JPEG-decode — frames need the
     ESP32-P4's hardware JPEG decoder (`driver/jpeg_decode.h`) before hitting the LVGL canvas, or
     it crashes reading a ~28KB compressed buffer as if it were a 768KB raw RGB565 frame.
  6. Video/audio were both choppy because `avi_player` runs one task processing frames strictly
     in sequence — a slow video frame directly delayed the next audio chunk. Fixed by decoupling
     onto two independent tasks (video: single-slot "latest frame wins" mailbox; audio: a short
     FIFO queue), each fed by fast-copy-only callbacks.
  7. That decoupling introduced a new crash: the frame copies were plain `malloc()`, which can
     land outside PSRAM — the JPEG hardware decoder's DMA/cache-sync step asserts if its input
     buffer isn't there. Fixed with `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`.
  8. Closing the video screen deadlocked the device — the close button's LVGL callback (which
     holds the display lock) was calling a blocking `avi_player_play_stop()` that waits on the
     decode task, which was itself blocked trying to acquire that same lock. Fixed by deferring
     the actual stop/cleanup to a separate FreeRTOS task.
  9. JPEG color order needed to be BGR, not RGB, for correct colors on this decoder+canvas combo
     (found by literally looking at the screen — colors were channel-swapped with `_RGB`).
  10. WiFi Connect had no way to reconnect using already-saved credentials — only "enter new
      details," which also had no way to verify a typed password before submitting (masked with
      no reveal option). Both fixed: a "Reconnect" button that reuses saved credentials, and a
      "Show password" toggle in the credential dialog. The latter turned out to be the actual
      cause of several real "connection failed" reports — a typo made on the on-screen keyboard
      that the user had no way to visually verify before submitting.

  Everything through **Storage & Video and WiFi** is now confirmed working end-to-end on real
  hardware, including full audio+video playback with a minor (acceptable) video pause after a
  few seconds. Still not exercised: the AI assistant screen — needs the user's own Anthropic and
  OpenAI API keys entered via Settings first.

- 2026-09-13 — **AI voice assistant now confirmed working end-to-end on real hardware,
  completing every in-scope capability.** API keys turned out to be too long to type comfortably
  on the on-screen keyboard, so a serial console (`SET_ANTHROPIC_KEY:`/`SET_OPENAI_KEY:` over the
  same USB connection used for flashing) was added as the primary entry path; the on-screen
  keyboard dialog remains as a fallback. Real bugs found and fixed along the way (full detail in
  `docs/BRINGUP.md`):
  11. First attempt at the serial console crashed the board in a fast boot loop — it installed a
      UART0 driver, but this board's console runs over USB-Serial/JTAG
      (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`), a different peripheral entirely. Fixed by reading
      from `stdin` instead, which ESP-IDF already wires to that console peripheral — no manual
      driver install needed.
  12. Tapping the talk button and releasing crashed the device (a quick display flash, then back
      at the launcher — actually a silent `abort()`/reboot). Root cause: this board's internal
      SRAM heap is only ~180-250 KiB free at runtime, `sdkconfig.defaults` doesn't route generic
      `malloc`/`new`/`std::vector`/`std::string` into PSRAM, and three buffers in the assistant
      pipeline can each reach into the hundreds of KB (the recorded WAV, the HTTP multipart body
      that embeds it, the TTS PCM response) — any one of them could blow the budget, and since
      this project builds without C++ exceptions, a failed allocation aborts instead of throwing
      cleanly. Fixed by adding `http_client::PsramBuf`, a small PSRAM-backed growable buffer, and
      switching those three buffers to it.
  13. First real run surfaced an actual OpenAI account issue, not a firmware bug: Whisper
      returned HTTP 429 until billing was enabled on the OpenAI account.
  14. A reply came back in Spanish — Whisper auto-detects the spoken language rather than
      assuming English, and a short/noisy recording gave it little to go on. Fixed by forcing
      `language: en` in the Whisper request.

  Added beyond the original scope, per a follow-up request: an optional **local LLM backend** —
  a Settings toggle lets the "thinking" step go to a local LM Studio/Ollama server on the LAN
  instead of Anthropic (via its OpenAI-compatible `/v1/chat/completions` endpoint, no API key
  needed), configured with a URL and an optional model-name field (LM Studio ignores it; Ollama's
  OpenAI-compat endpoint requires it to match a pulled model tag exactly). Whisper STT and TTS
  still always go through OpenAI regardless of this toggle. Confirmed working end-to-end against
  a real LM Studio server on the user's network.

  **Every in-scope capability (display/touch, WiFi, storage/video, audio, AI assistant) is now
  confirmed working on real hardware.**
