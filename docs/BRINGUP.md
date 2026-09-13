# JC4880P443C_I_W bring-up notes

Everything in this file was learned the hard way while bringing up the
desk-helper project on this exact board (Guition JC4880P443C_I_W: ESP32-P4 +
ESP32-C6, 4.3" 480×800 MIPI-DSI ST7701 panel, GT911 touch). It's baked into
this template's `sdkconfig.defaults`, `platformio.ini`, `partitions.csv`, and
`src/main.cpp` already — this doc explains *why*, so you don't accidentally
undo a fix while customizing the app.

## Hardware reference

| Component      | Details                                                        |
|-----------------|------------------------------------------------------------------|
| Main SoC        | ESP32-P4, dual-core RISC-V @ 400 MHz, rev 1.x (ECO2)             |
| WiFi / BT       | ESP32-C6 co-processor via SDIO (ESP-HOSTED protocol)             |
| Display         | 4.3" IPS 480×800, MIPI-DSI, ST7701 controller                    |
| Touch           | Goodix GT911, I2C (SDA=GPIO7, SCL=GPIO8)                         |
| Flash           | 16 MB (DIO)                                                      |
| PSRAM           | 32 MB octal, 200 MHz                                             |
| Backlight       | GPIO23 (active-high, digital)                                    |
| LCD reset       | GPIO5                                                            |
| MIPI PHY power  | Internal LDO3, 2500 mV                                           |
| SDIO (P4↔C6)    | CLK=18, CMD=19, D0=14, D1=15, D2=16, D3=17, Reset=54              |
| Audio codec     | ES8311 (speaker DAC + mic ADC in one chip) — I2C: SDA=GPIO7, SCL=GPIO8 (**shared with GT911 touch bus**), addr 0x18. I2S: MCLK=GPIO13, BCLK=GPIO12, WS=GPIO10, DSDIN(speaker)=GPIO9, ASDOUT(mic)=GPIO48. PA enable=GPIO11. |
| TF/SD card      | SDMMC 4-bit: CLK=GPIO43, CMD=GPIO44, D0=GPIO39, D1=GPIO40, D2=GPIO41, D3=GPIO42. Power switch=GPIO45 (polarity unconfirmed, see below). |
| RS485 (unused)  | MAX485, hardware auto-direction (no GPIO for DE/RE). UART: TX=GPIO26, RX=GPIO27. Not wired to anything on this board build. |
| CSI camera (unused) | OV02C10 per vendor demo. I2C shares touch/audio bus (GPIO7/8). No camera module attached to this board. |
| Battery (unused)| IP5306 fuel-gauge/boost IC on VOUT-BAT. No battery attached to this board. |
| GPIO header (unused) | `JP1`: GPIO52/51/50/49/35/34/32/29. Needs external LEDs/buttons to demo meaningfully. |

Full vendor spec sheet: `docs/JC4880P443C_I_W Specifications-EN-V1.0*.pdf`
(included in this template).

### Provenance of the pins above

Confirmed by cross-referencing two independent sources in the vendor's SDK package
(`C:\Dev\JC4880P443C_I_W\`, not included in this template — see below): the schematic PNGs under
`5-Schematic\` (`3_ESP32-P4.png`, `6_CODEC&TFCARD.png`, `5_485.png`) and the vendor's own
Arduino sample `1-Demo\arduino_examples\mp3_player\mp3_player.ino`, which hardcodes the exact
same audio codec and SD card pins listed above (`AudioBoard(AudioDriverES8311, ...)`,
`SD_MMC.setPins(43, 44, 39, 40, 41, 42)`) — high confidence.

**Resolved**: the TF card power switch (GPIO45 → AO3401 P-MOSFET gate) turned out to be a red
herring — R10 (the resistor that would make that connection) is not populated, so GPIO45 has no
effect at all. The real power gate is the on-chip LDO channel 4 (`ESP_LDO_VO4` net) feeding the
now-permanently-on MOSFET — see "TF card + WiFi both use SDMMC, but on independent slots" below
for the full fix. The initial guess that this was a GPIO45-polarity question sent troubleshooting
down the wrong path for a while; don't repeat that assumption.

**Also note**: the ES8311's mic-input I2S line is labeled `ES7210_SDOUT` in the schematic net
name (pin table on `3_ESP32-P4.png`), which looks like it implies a *separate* ES7210 mic-array
codec. It doesn't — the `6_CODEC&TFCARD.png` schematic shows only one codec chip (`U1 ES8311-S`)
with its onboard mic ADC fed by an analog electret mic (`MIC1`, part `MSM381A3729H9CP`) and its
`ASDOUT` pin is what's wired to GPIO48. The `ES7210_SDOUT` net name is very likely copy-pasted
from a reference design that *does* have a separate ES7210 — treat GPIO48 as "ES8311 mic data
out", not as a second chip.

The vendor also ships a much larger SDK package (schematics, Arduino/IDF demo
examples, burn tool, ~550 MB) — not included here because of size. If you need
it, look for the original `JC4880P443C_I_W/` vendor folder from wherever you
sourced the board (Guition product page / included link), or ask whoever set
up the first project (desk-helper) for a copy. The ST7701 driver files in
`src/` of this template were extracted from
`JC4880P443C_I_W/1-Demo/arduino_examples/lvgl_demo_v8/src/lcd/` in that
package — you shouldn't need the rest of it for a new project.

## Toolchain

Use **pioarduino**, not the official PlatformIO espressif32 platform — the
official `platformio/espressif32` v7.x does not support ESP32-P4 at all.
`platformio.ini` in this template already points at:
```
https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
```
Board: `esp32-p4`. Framework: `espidf` (the `espidf` env is the proven path;
an `esp32p4_arduino` env is included as a fallback but WiFi via ESP-HOSTED has
limited Arduino support).

## Display bring-up

The Espressif BSP (`espressif/esp32_p4_function_ev_board`) **does not support
this board's 480×800 ST7701 panel** — it only handles 1024×600 and 1280×800.
Do not call BSP display init functions (`bsp_display_start`,
`bsp_display_new`, etc.) — they fail or produce garbage on this panel. It's
kept as a component dependency only because other managed components pull it
in transitively; sdkconfig options referencing it are Kconfig defaults for the
component, not something this template actually uses.

The display is driven **directly**, via the ST7701 vendor driver files in
`src/` (`esp_lcd_st7701.c/.h`, `esp_lcd_st7701_interface.h`,
`esp_lcd_st7701_mipi.c`, `esp_lcd_st7701_rgb.c` — the RGB variant must be
compiled even on this MIPI-only board because `SOC_LCD_RGB_SUPPORTED=y` on
ESP32-P4 and the driver's dispatch file references it unconditionally).

Init sequence (see `src/main.cpp`):
1. `esp_ldo_acquire_channel()` — power LDO3 at 2500 mV for the MIPI PHY
2. `esp_lcd_new_dsi_bus()` — 2-lane DSI bus at 500 Mbps
3. `esp_lcd_new_panel_io_dbi()` — DBI command channel
4. `esp_lcd_new_panel_st7701()` → `reset()` → `init()` — 480×800 DPI config at 34 MHz
5. GPIO23 high — backlight on
6. `lvgl_port_init()` + `lvgl_port_add_disp_dsi()` — register with LVGL
7. `i2c_new_master_bus()` + `esp_lcd_touch_new_i2c_gt911()` + `lvgl_port_add_touch()` — GT911 touch

## Chip revision selector (CRITICAL — crash on boot if wrong)

`CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` and `CONFIG_ESP32P4_REV_MIN_100=y` are
required in `sdkconfig.defaults`. This board uses ESP32-P4 rev 1.x (ECO2).
Rev <3.0 and >=3.0 have fundamentally different memory maps — selecting the
wrong one causes the ROM to zero out its SPI-flash data pointer
(`0x4ff3ffe8`) on startup and crash immediately, before any of your code runs.

## PSRAM not available in heap

`CONFIG_SPIRAM_BOOT_INIT` must be **unset** (not `y`) — the vendor bootloader
already initializes PSRAM, and letting the app re-init it from
`cpu_start.c` causes a null-dereference crash on startup.

But leaving it unset also means `cpu_start.c` never calls `esp_psram_init()`,
so PSRAM is detected but never mapped into virtual address space —
`heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` returns 0.

**Fix** (already in `src/main.cpp`, top of `app_main()`):
```cpp
// SPIRAM_BOOT_HW_INIT=y already ran esp_psram_chip_init().
// Call esp_psram_init() explicitly to do the virtual-address mapping.
esp_err_t psram_ret = esp_psram_init();
if (psram_ret == ESP_OK) {
    ESP_ERROR_CHECK(esp_psram_extram_add_to_heap_allocator());
}
```
Gives ~31 MB of free PSRAM in the heap — required for LVGL draw buffers
(`buff_spiram = true` in `lvgl_port_display_cfg_t`).

**PSRAM mode**: ESP32-P4 only supports HEX (16-bit) PSRAM mode.
`CONFIG_SPIRAM_MODE_OCT=y` in `sdkconfig.defaults` is silently ignored;
`CONFIG_SPIRAM_MODE_HEX=y` is always what Kconfig actually selects. Don't
worry if you see HEX in the generated `sdkconfig` — that's expected.

## GT911 I2C config macro out-of-order

`ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG()` initializes `scl_speed_hz` before
`control_phase_bytes`, which is the wrong order for the struct's declaration
order. GCC rejects this with `-Werror`. Initialize the
`esp_lcd_panel_io_i2c_config_t` struct manually instead of using the macro
(already done in `src/main.cpp`).

## Flash address / partition offset

The partition table (`partitions.csv`) puts the factory app at `0x20000`, not
the ESP-IDF/PlatformIO default `0x10000`. `platformio.ini`'s custom
`upload_command` overrides the esptool invocation to write bootloader,
partition table, and firmware at the correct offsets (`0x2000`, `0x8000`,
`0x20000`).

## esptool venv is broken in bundled PlatformIO

The PlatformIO-bundled esptool.py (in the `.espidf-5.5.x` venv) is missing
`rich_click`, and that venv has no `pip` to install it. Workaround (already
wired into `upload_command` in `platformio.ini`): use a system Python
esptool install instead.

Install it once per machine:
```
<python-install-dir>\python.exe -m pip install esptool
```
Then update the `upload_command` path in `platformio.ini` to point at that
Python's `Scripts\esptool.exe`.

**Unicode build failure**: PlatformIO can crash with `UnicodeEncodeError`
(cp1252 console) when printing the firmware size table after a successful
build. Exit code is 1, but the binaries ARE built correctly at
`.pio/build/esp32p4/{bootloader,partitions,firmware}.bin`. If `pio run
--target upload` fails this way, flash manually:
```powershell
& "<esptool-path>\esptool.exe" `
    --chip esp32p4 --port COM6 --baud 921600 `
    --before default-reset --after hard-reset `
    write-flash -z --flash-mode dio --flash-freq 80m --flash-size detect `
    0x2000  ".pio\build\esp32p4\bootloader.bin" `
    0x8000  ".pio\build\esp32p4\partitions.bin" `
    0x20000 ".pio\build\esp32p4\firmware.bin"
```

## Linker script gotcha after a clean sdkconfig regeneration

If `sdkconfig` is deleted and the first full rebuild fails at the linker step
(missing `_bss_start_low` / `_heap_start_low` — ESP32-P4 rev <3.0 low-memory
symbols), **do not** just re-run `pio run`. The partial build leaves an
incomplete linker script; relinking against it produces firmware that
corrupts startup memory and crash-loops. Always run `pio run --target clean`
before rebuilding after a failed sdkconfig regeneration.

## esp_hosted 3.x breaks the build on Windows/PlatformIO (CRITICAL — pin to 2.x)

`src/idf_component.yml` pins `espressif/esp_hosted` to `>=2.11,<3.0`, not `*`.
Leaving it unconstrained resolves to esp_hosted 3.0.6+ as of mid-2026, and
**every compile fails** with:
```
riscv32-esp-elf-g++: fatal error: cannot specify '-o' with '-c', '-S' or '-E' with multiple files
compilation terminated.
```

Root cause: esp_hosted's `host/eh_host_config/CMakeLists.txt` (from 3.0
onward) adds `"SHELL:-include eh_host_port_master_config.h"` as a compile
option. The `SHELL:` prefix is a CMake convention (for Ninja/Make generators)
meaning "don't split or reorder this token group." PlatformIO's flag
extraction for its scons-based ESP-IDF integration doesn't honor `SHELL:` on
Windows, so `-include` and its filename argument get separated by other
flags. GCC then reads `eh_host_port_master_config.h` as a second input
*source* file instead of the argument to `-include`, and rejects the
combination of multiple source files with a single `-o`/`-c`.

This isn't a template bug — it reproduces on a clean `esp_hosted@3.0.6` +
PlatformIO on Windows regardless of project. Confirmed non-flaky (fails
identically with `-j1`, single-threaded). Fix is simply staying on the 2.x
line, which doesn't use `SHELL:`, until PlatformIO or esp_hosted fixes the
incompatibility upstream. If you ever see this error after touching
`idf_component.yml` or deleting `dependencies.lock`, check the resolved
`espressif/esp_hosted` version in `dependencies.lock` first.

Note: `espressif/esp_wifi_remote` has no 2.x/3.x version line as of this
writing (latest is 1.6.x) — don't try to pin it the same way, it's
`esp_hosted`'s version that needs constraining. `esp_wifi_remote`'s own
manifest happens to privately pin `esp_hosted: '>=2.11,<3.0'` for the
esp32p4/esp32h2 targets in some releases, which is how desk-helper's original
lock file avoided this even with `esp_hosted: "*"` — but don't rely on that
transitive constraint staying in place in a future `esp_wifi_remote` release;
pin `esp_hosted` directly instead, as this template does.

## TF card + WiFi both use SDMMC, but on independent slots — RESOLVED

**Root cause found and fixed, confirmed on hardware.** ESP32-P4's SDMMC
peripheral has **two genuinely independent slots**. ESP-HOSTED (WiFi, over
SDIO to the C6) uses **Slot 1**, entirely GPIO-matrix-routed
(CLK=18/CMD=19/D0-D3=14-17). The TF card slot on this board uses **Slot 0**,
which is wired to fixed, non-GPIO-matrix IOMUX pins baked into the ESP32-P4
silicon itself (`soc/esp32p4/include/soc/sdmmc_pins.h`):
`SDMMC_SLOT0_IOMUX_PIN_NUM_{CLK,CMD,D0,D1,D2,D3}` = **43, 44, 39, 40, 41,
42** — exactly the pins from vendor `mp3_player.ino`, and exactly what
ESP-IDF's own `SDMMC_SLOT_CONFIG_DEFAULT()` resolves to for `esp32p4`. These
pins were correct from the very first attempt.

The actual bug: `SDMMC_HOST_DEFAULT()` sets `.slot = SDMMC_HOST_SLOT_1` —
the same slot WiFi uses — by default. Mounting the TF card without
overriding `.slot` to `SDMMC_HOST_SLOT_0` collided directly with
ESP-HOSTED's live transport on Slot 1 and corrupted it:
```
E (...) sdmmc_io: sdmmc_io_rw_extended: sdmmc_send_cmd returned 0x107
E (...) H_SDIO_DRV: failed to read registers
I (...) H_SDIO_DRV: Host is resetting itself, to avoid any sdio race condition
```
...followed by a full `esp_restart()`, every time, deterministically. (An
SPI-mode workaround was tried at one point to sidestep this — it avoided the
crash since SPI uses a separate peripheral entirely, but the card still
didn't respond, because of the power issue below. SPI mode is no longer
used; reverted back to native SDMMC once the real fix was found.)

**Power**: the schematic's `ESP_LDO_VO4` net (which feeds an always-on
AO3401 P-MOSFET switch to `TF_VCC` — see "TF card power switch" below) is
exactly what its name says: **on-chip LDO channel 4 output**. That internal
regulator channel is off by default and must be explicitly enabled via
`sd_pwr_ctrl_new_on_chip_ldo()` — without it the card has zero power
regardless of which pins or which mode (SDMMC or SPI) talk to it, which is
why even the SPI-mode attempt (correct pins, no peripheral conflict) still
timed out. This is the same on-chip LDO peripheral already used for the
display's MIPI DSI PHY power in `main.cpp` (channel 3 there, channel 4
here).

**Fix** (`src/services/sdcard_service.cpp`), matching the pattern in the
vendor's own `esp32_p4_function_ev_board.c` (`bsp_sdcard_mount()`):
```cpp
sd_pwr_ctrl_ldo_config_t ldo_config = { .ldo_chan_id = 4 };
sd_pwr_ctrl_handle_t pwr_handle;
sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_handle);

sdmmc_host_t host = SDMMC_HOST_DEFAULT();
host.slot = SDMMC_HOST_SLOT_0;           // NOT the default (SLOT_1) — that's WiFi's slot
host.pwr_ctrl_handle = pwr_handle;       // NOT powered without this

sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT(); // already right for esp32p4 slot 0
slot_config.width = 4;
esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
```
`sdmmc_periph: sdmmc_host_init: SDMMC host already initialized, skipping init
flow` in the log at mount time is expected/benign here — it's the
shared-controller-level init (already done by ESP-HOSTED for its own slot)
being correctly skipped on the second call, while slot-0-specific init still
proceeds. Confirmed on hardware: card mounts, lists real files (including
the vendor's own `MJPEG` folder), reports real free/total space.

**No power-switch GPIO needed in software**: R10 (the resistor that would
connect GPIO45 to the AO3401 gate) is not populated on this board, so the
external switch is permanently on — the only thing actually gating power is
the on-chip LDO channel above.

## Audio codec (ES8311): one shared duplex device, and I2S must run in STEREO slot mode

**Found by on-hardware testing** — compiled and ran without error, but
produced **no audio output and no mic input at all**, silently. Two separate
mistakes, both fixed by cross-checking against `xiaozhi-esp32`'s shipping
`guition-jc4880p443` board port (`main/boards/guition-jc4880p443/` in the
vendor SDK package — an exact match for this board, independently confirming
every audio pin in the table above):

1. **Don't create two separate `esp_codec_dev` instances** (one
   `ESP_CODEC_DEV_TYPE_OUT`, one `ESP_CODEC_DEV_TYPE_IN`) both wrapping the
   same I2S `data_if` — that produced no audio in either direction on this
   board. Create **one** `esp_codec_dev` with `dev_type =
   ESP_CODEC_DEV_TYPE_IN_OUT`, opened once, and use the same handle for both
   `esp_codec_dev_write()` (playback) and `esp_codec_dev_read()` (capture).
2. **The I2S hardware slot config must be `I2S_SLOT_MODE_STEREO` /
   `I2S_STD_SLOT_BOTH`**, even though the actual audio content is mono. This
   looks wrong — `I2S_SLOT_MODE_MONO` seems like the "correct" choice for
   mono content — but the ES8311's I2S timing expects both slots active;
   `esp_codec_dev`'s `sample_info_t.channel = 1` (set at `esp_codec_dev_open()`
   time) is what actually declares the content mono and handles the
   up/down-mixing on top of the stereo-slotted I2S bus. Using
   `I2S_SLOT_MODE_MONO` for the I2S channel config produced no audio.
3. **Wrong I2C address.** `es8311_codec_cfg_t`/`audio_codec_i2c_cfg_t.addr`
   wants the **8-bit** I2C address, `ES8311_CODEC_DEFAULT_ADDR` = `0x30`
   (`managed_components/espressif__esp_codec_dev/device/include/es8311_codec.h`)
   — not `0x18`, the ES8311 datasheet's 7-bit address, which is what's
   actually readable off the schematic net names. Using `0x18` produced zero
   ACKs on every I2C transaction to the codec (`I2C_If: Fail to write to dev
   18` / `ES8311: Open fail`) despite the GT911 touch controller on the same
   physical I2C bus working fine — a wrong-address symptom, not a bus
   problem. Use the `ES8311_CODEC_DEFAULT_ADDR` macro, don't hardcode the
   datasheet address.

All three fixed and **confirmed on hardware**: tone plays through the
speaker, mic level meter responds to real sound. See
`src/services/audio_service.cpp` for the corrected implementation.

## USB-JTAG is the only bidirectional PC↔device path

USB-JTAG (shows up as a COM port, e.g. COM6) is the only bidirectional path
between PC and ESP32-P4 on this board. UART0 TX bridges to USB, but UART0 RX
does **not** receive data sent from the PC — so `uart_read_bytes()` never
sees anything the host sends.

**Fix**: `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` in `sdkconfig.defaults` (ESP-IDF
startup then auto-installs the driver before `app_main`). Call
`usb_serial_jtag_driver_install()` explicitly in `app_main` too — it returns
`ESP_ERR_INVALID_STATE` harmlessly if the console startup already did it. Read
incoming bytes with `usb_serial_jtag_read_bytes()` from a dedicated FreeRTOS
task (give it at least an 8192-byte stack).

If your project doesn't need the PC to send data to the device, you can drop
`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` and just use ESP-IDF's normal log
output.

**COM port lock**: on Windows, stale processes (leftover Python agents, other
monitor sessions) can hold the COM port open and block flashing. Kill them
before flashing, e.g. `Stop-Process -Name python -Force`.

## WiFi (ESP-HOSTED via ESP32-C6)

ESP32-P4 has no built-in WiFi — this board's WiFi/BT goes through the
ESP32-C6 co-processor over SDIO, using Espressif's ESP-HOSTED managed
component (`espressif/esp_hosted` + `espressif/esp_wifi_remote`). GPIO wiring
is fixed by the board (see hardware reference table above) and matched by
`CONFIG_ESP_HOSTED_P4_DEV_BOARD_FUNC_BOARD=y` in `sdkconfig.defaults`.

If a project genuinely doesn't need WiFi, you can remove the whole
ESP-HOSTED block from `sdkconfig.defaults`, the `espressif__esp_hosted` /
`espressif__esp_wifi_remote` entries from `src/idf_component.yml` and
`src/CMakeLists.txt`'s `REQUIRES`, and the `esp_wifi`/`esp_netif`/`esp_event`
`REQUIRES` too.

## WiFi app-level bring-up: esp_hosted_init() is required before any esp_wifi_* call

**CRITICAL — crashes the app, found by on-hardware testing.** The Kconfig
block above only wires up the ESP-HOSTED *component*; it does **not**
initialize the SDIO transport to the C6 at runtime. Calling `esp_wifi_init()`
(directly, or via `esp_netif_create_default_wifi_sta()` →
`esp_wifi_remote_init()`) without first calling `esp_hosted_init()` fails
with:
```
E (...) H_API: Transport not initialized, call esp_hosted_init() first
```
and this isn't just a logged error — the WiFi init silently returns failure,
and the app then hard-crashes a few seconds later inside
`esp_netif_create_default_wifi_sta()` (`assert failed:
esp_netif_create_default_wifi_sta wifi_default.c:422 (netif)`, illegal
instruction, reboot).

**Fix**: call, in this order, before touching any other `esp_wifi_*`/
`esp_netif_create_default_wifi_sta()` API:
```cpp
#include "esp_hosted.h"
esp_hosted_init();
esp_hosted_connect_to_slave();
```
This matches the vendor's own `esp_hosted` example apps (e.g.
`examples/host_hosted_events/main/station_example.c` in the resolved
`managed_components/espressif__esp_hosted/` package) — they always call
`esp_hosted_init()` + `esp_hosted_connect_to_slave()` once, up front, before
any station/AP setup. `esp_hosted_init()` is idempotent (safe to call more
than once; it no-ops after the first successful call).

This wasn't visible from reading the SDK or from a clean `pio run` build —
compiles fine either way — it only shows up once you actually exercise the
WiFi path on real hardware. If you see this exact crash, it means some new
code path is calling into `esp_wifi_*` before `wifi_service::init()` (or
whatever your equivalent is) has run `esp_hosted_init()` first.

## esp_wifi_scan_start() races the C6 co-processor for ~2-3s after esp_wifi_start()

**Found by on-hardware testing**, separate from the crash above. Even after
`esp_hosted_init()`/`esp_hosted_connect_to_slave()` are done and
`esp_wifi_start()` has returned *and* the host-side `WIFI_EVENT_STA_START`
event has fired, calling `esp_wifi_scan_start()` still fails with
`ESP_ERR_WIFI_STATE` for another ~2-3 seconds. Measured cause: the ESP32-C6
co-processor is still settling its own WiFi state machine over the SDIO/RPC
link — its own "station disconnected/idle" event (visible on the host as
`RPC_WRAP: ESP Event: Station mode: Disconnected`) arrives roughly a second
*after* the host's own `STA_START` event, and scanning only becomes possible
after that. There is no host-visible event that fires exactly when the C6 is
actually ready to scan.

**Fix**: retry `esp_wifi_scan_start()` on `ESP_ERR_WIFI_STATE` with a
generous total budget — `wifi_service::scan()` retries up to 10 times, 500ms
apart (~5s ceiling). On the test board this took 6 attempts (~2.6s) before
succeeding. Don't shorten this budget without re-testing on hardware — a
budget that looked "generous" at 5 attempts / 1.5s total was still not
enough. This is specific to the ESP-HOSTED/SDIO path; a board with native
WiFi (no C6 co-processor) would not need this.

## Audio codec bring-up (ES8311 via esp_codec_dev)

Don't hand-roll ES8311 I2C register writes — use the managed components
`espressif/esp_codec_dev` (generic codec device abstraction: open/read/write/
close/volume/gain) + `espressif/es8311` (the actual ES8311 driver, exposing
an `audio_codec_if_t`). This is the same pattern Espressif's own
`esp32_p4_function_ev_board` BSP uses internally (see
`esp32_p4_function_ev_board.c` in the vendor SDK package, function
`bsp_audio_codec_speaker_init()` / `bsp_audio_codec_microphone_init()`) — do
**not** depend on that BSP component itself though, same reasoning as the
display: it's wired for the Espressif EV board's own pins, not this board's.
Call sequence: `audio_codec_new_i2s_data()` (I2S TX/RX handles) →
`audio_codec_new_i2c_ctrl()` (I2C bus + ES8311 addr 0x18) →
`audio_codec_new_gpio()` → `es8311_codec_new()` (pass PA GPIO, mclk/sclk
invert flags, `master_mode = false` since the ESP32 is I2S master) →
`esp_codec_dev_new()` → `esp_codec_dev_open()` with a sample-rate/bit-depth/
channel config before any `esp_codec_dev_read()`/`esp_codec_dev_write()`.

Since this bus is shared with GT911 touch (GPIO7/8), initialize the codec's
I2C control interface on the **same** `i2c_master_bus_handle_t` already
created for touch in `app_main()` — don't call `i2c_new_master_bus()` a
second time on the same pins, it will fail with the bus already claimed.

## Video playback (AVI/MJPEG via avi_player)

`espressif/avi_player` (pin to `^2.0.0`, matching the vendor's own
`esp_brookesia_phone` example) parses the AVI container and demuxes out each
frame — don't hand-roll AVI/RIFF parsing. **It does not decode the JPEG
frames themselves** (checked against its source,
`managed_components/espressif__avi_player/avi_player.c` — the video callback
just hands you the demuxed bytes as-is; there is no JPEG decode call
anywhere in that file). `frame_data_t.data` for a video frame is still
**compressed JPEG bytes**, `data_bytes` long — see the crash below for what
happens if you forget this. The two sample clips at
`1-Demo\4.3 TF card video file\MJPEG\*.avi` in the vendor package are a
ready-made test fixture; per `1-Demo\Instructions.txt` they need to be copied
onto a FAT32-formatted TF card under 32 GB.

### Frames must be JPEG-decoded before use — crashes LVGL otherwise

**CRITICAL — crashes the app, found by on-hardware testing.** Handing
`frame_data_t.data` straight to `lv_canvas_set_buffer()` as if it were
already-decoded RGB565 pixel data — a very natural first mistake, since nothing about the
API signals otherwise — crashes with a `Guru Meditation Error: Load access
fault` inside `lv_memcpy`, called from LVGL's own periodic redraw
(`lv_draw_sw_blend_image_to_rgb565` → `drawbuf_next_row`), a few tens of
milliseconds after the first frame arrives (not immediately — the crash
happens on LVGL's *next* render pass, not inside your callback, which makes
it look unrelated at first). Root cause: a compressed JPEG frame at this
resolution is ~28 KB, but a raw 480×800 RGB565 buffer is 768,000 bytes —
LVGL reads past the end of the real (much smaller) buffer into unmapped
memory the moment it tries to blit a row beyond what's actually there.

**Fix**: decode each frame with the ESP32-P4's **hardware JPEG decoder**
(`driver/jpeg_decode.h`, component `esp_driver_jpeg`, built into ESP-IDF —
no managed component needed) before touching the canvas:
```cpp
jpeg_decode_engine_cfg_t eng_cfg = { .intr_priority = 0, .timeout_ms = 200 };
jpeg_new_decoder_engine(&eng_cfg, &decoder);              // once, when video starts

jpeg_decode_memory_alloc_cfg_t mem_cfg = { .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER };
size_t allocated;
uint8_t *out_buf = (uint8_t *)jpeg_alloc_decoder_mem(W * H * 2, &mem_cfg, &allocated); // once

// per frame, inside the video callback:
jpeg_decode_cfg_t decode_cfg = {
    .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
    .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR, // see note below — _RGB looks "right" but isn't
    .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
};
uint32_t out_size;
jpeg_decoder_process(decoder, &decode_cfg, data->data, data->data_bytes, out_buf, allocated, &out_size);
lv_canvas_set_buffer(canvas, out_buf, data->video_info.width, data->video_info.height, LV_COLOR_FORMAT_RGB565);
```
Confirmed on hardware: 57+ frames decoded and pushed to the canvas cleanly
in 5 seconds, no crash, correct 768,000-byte output size for a 480×800
frame. See `src/ui/screen_storage.cpp`.

**Don't forget to free `out_buf`** — `jpeg_alloc_decoder_mem()` is a plain
`heap_caps_calloc()` under the hood (checked against
`esp_driver_jpeg/jpeg_decode.c`), **not** owned or freed by
`jpeg_del_decoder_engine()`. It needs its own explicit `heap_caps_free()`
when video playback stops, or every video-open/close cycle leaks ~768 KB of
PSRAM.

**`rgb_order` — use `JPEG_DEC_RGB_ELEMENT_ORDER_BGR`, not `_RGB`.** Found on
hardware: with `_RGB` the video played back with visibly wrong colors
("very weird", channel-swapped). Despite the enum name suggesting `_RGB` is
the "normal" choice for an RGB565 canvas, `_BGR` is what's actually correct
for this decoder + LVGL canvas combination. If a future frame source looks
color-swapped, this is the first setting to check.

### Video audio: use avi_player's audio_cb, not a separate pipeline

AVI audio frames (`frame_data_t` with `type == FRAME_TYPE_AUDIO`) are
**already raw PCM** — `audio_frame_format` only has one value, `FORMAT_PCM`
— so unlike video there's no decode step needed. Just forward
`data->data`/`data->data_bytes` straight to the codec, using
`data->audio_info.sample_rate`/`channel` (not hardcoded values — a
different clip could use a different rate/channel count). See
`audio_service::play_pcm()`'s `channels` parameter (added for this) and
`src/ui/screen_storage.cpp`'s `audio_frame_cb`. `play_pcm()` blocks until
the samples are actually written over I2S, which conveniently self-paces
playback to real time — no separate AV-sync clock needed.

### Closing the video screen deadlocks the device — CRITICAL, found on hardware

Tapping the close (✕) button froze the whole device (not a crash — no panic,
no reboot, just permanently unresponsive) every time. Root cause: the close
button's `LV_EVENT_CLICKED` callback runs from inside LVGL's own event
dispatch, which holds the `lvgl_port_lock()`-managed lock for the entire
callback. The original close handler called `avi_player_play_stop()`
directly from there — a *blocking* call that waits for the AVI decode task
to acknowledge the stop. But that decode task can itself be blocked inside
`video_frame_cb()` waiting to *acquire* that exact same lock (to push the
next decoded frame to the canvas). Two-way wait on the same lock = deadlock,
permanently, the moment both sides line up (which they will, essentially
every time, since the decode task is calling `lvgl_port_lock()` many times
a second).

**Fix**: never call a blocking `avi_player_*` stop/deinit function from
directly inside an LVGL event callback. Spawn a separate FreeRTOS task from
the callback instead (the callback itself just does that and returns
immediately, letting LVGL's dispatch finish and release the lock); do the
actual `avi_player_play_stop()`/`avi_player_deinit()`/cleanup in that task,
re-acquiring `lvgl_port_lock()` only for the final `lv_obj_del()` of the
video overlay. See `close_video()`/`close_video_task()` in
`src/ui/screen_storage.cpp`. This is a general rule, not specific to this
video screen: **don't call a function that blocks waiting on another task
from inside code that's currently holding a lock that other task needs.**

### Choppy audio and video — the LVGL display buffer was the bottleneck

Both audio and video were audibly/visibly stuttering together — a strong
hint they share a root cause, since they're driven by the same single
`avi_player` decode task processing frames strictly in sequence (a slow
video frame delays the very next audio chunk right behind it, and vice
versa). Measured on hardware: JPEG-decode-only throughput (no LVGL/canvas
involved) was ~23 fps (117 frames / 5s); adding the canvas `lvgl_port_lock()`
+ `lv_canvas_set_buffer()` step roughly halved that to ~11 fps (57 frames /
5s) — the LVGL canvas/redraw path was the dominant per-frame cost, not the
JPEG decode itself.

Cause: the display's LVGL draw buffer (`lvgl_port_display_cfg_t.buffer_size`
in `main.cpp`) was sized for only 100 rows out of the panel's 800 — a
reasonable size for normal partial-redraw UI updates, but the video screen's
canvas covers the *entire* 480×800 panel and changes completely on every
decoded frame, forcing LVGL to flush the full screen in 8 separate
100-row chunks *per video frame*, many times a second. That per-chunk
overhead (not the pixel-pushing itself) was the bottleneck.

**Fix**: size the LVGL draw buffer for a **full frame**
(`LCD_H_RES * LCD_V_RES` instead of `LCD_H_RES * 100`) so a full-screen
update is one flush, not eight. PSRAM is abundant on this board (~31 MB
free) so the larger buffer (768,000 bytes × 2 for double-buffering ≈ 1.5 MB)
costs nothing meaningful. This is a pure LVGL performance tuning parameter,
not part of the fragile chip-revision/PSRAM/display-init sequence — safe to
adjust without re-reading the rest of this document's display bring-up
section. See `main.cpp`'s `disp_cfg`.

**Still choppy after that fix, on hardware — the deeper cause was task
serialization, not buffer chunking.** The full-frame buffer helped, but
didn't fix it, because the real problem is architectural: `avi_player` runs
**one** task that demuxes the file and calls `video_cb`/`audio_cb`
synchronously and sequentially, in file order. Doing the actual work (JPEG
decode + canvas update; blocking I2S write) directly inside those callbacks
means a slow video frame directly delays the very next audio chunk sitting
right behind it in the stream, and vice versa — there's no way to tune
buffer sizes out of that; the two are architecturally coupled on one task.

**Fix**: decouple into two independent consumer tasks, fed by callbacks that
now only do a fast `memcpy` before returning (so the demux task itself is
never blocked on slow work):
- **Video** — a single-slot "latest frame wins" mailbox (mutex + binary
  semaphore, not a queue). If the render task falls behind, the next
  arriving frame simply replaces whatever's pending; stale frames are
  dropped rather than queued, so lag never accumulates — video just skips
  frames under load instead of falling further and further behind. This is
  the right trade-off here: this SoC's hardware JPEG decoder has a real
  throughput floor for 480×800 frames (~40 ms/frame measured, i.e. ~25 fps
  hard ceiling) that no amount of software tuning removes, so *some* frame
  dropping under load is expected, not a bug to chase further.
- **Audio** — a short FreeRTOS queue (`xQueueCreate`, depth 8) with a
  dedicated consumer task. Audio must not drop chunks (gaps are audible and
  much worse than a dropped video frame), so its callback blocks briefly
  (up to 1s) trying to enqueue rather than dropping — but critically, that
  enqueue is cheap and fast, so the *audio playback task* can call the
  blocking, real-time-pacing `audio_service::play_pcm()` promptly and
  continuously, independent of how far behind video rendering is.

Task placement: the video render task shares CPU core 1 with `avi_player`'s
own demux task (fine — its per-frame work is now just a mutex-guarded
pointer swap, not the expensive JPEG decode+redraw). The audio task runs on
core 0 with a higher priority than everything else video-related, so
scheduling contention from decode/demux work can't delay it — audio
smoothness was the primary complaint this architecture change targets. See
`src/ui/screen_storage.cpp` (`video_render_task`, `audio_play_task`,
`video_frame_cb`, `audio_frame_cb`).

**Confirmed API** (resolved version, `managed_components/espressif__avi_player/include/avi_player.h`):
`video_write_cb`/`audio_write_cb` are `void(*)(frame_data_t *data, void *arg)` — **not** `int(*)(frame_data_t*)`.
`frame_data_t` has `data`/`data_bytes`/`type` plus a union of `video_info` (`width`/`height`/`frame_format`)
and `audio_info` — check `type == FRAME_TYPE_VIDEO` before reading `video_info`. If a future
component-manager resolution moves past this API shape, `src/ui/screen_storage.cpp`'s
`video_frame_cb` is the one place that needs updating.

**`buffer_size` must be set explicitly — found by on-hardware testing.** Left at 0 (the
"default" per the header comment, which internally becomes 20 KB), decoding the vendor's own
sample clip fails immediately:
```
E (...) avi player: frame size 28044 exceeds available data
```
A single compressed JPEG frame at this video's resolution (480×800) is comfortably over 20 KB, so
the internal buffer is simply too small — and once one frame overflows it, the parser loses sync
with the AVI stream entirely (`unknown frame e0ffd8ff`, `AVI Perse failed`, garbage frame sizes in
the billions on subsequent reads). **Fix**: set `cfg.buffer_size = 100 * 1024` (or similar
comfortably-larger-than-one-frame value) before calling `avi_player_init()` — confirmed on
hardware: 117 frames decoded cleanly in 5 seconds at the correct 480×800 resolution once fixed.
See `src/ui/screen_storage.cpp`.

### Copied JPEG input buffers must be allocated in PSRAM explicitly — CRITICAL, found on hardware

After decoupling video/audio onto separate tasks (previous section), starting
playback crashed the device every time with an assert failure deep inside
the ESP-IDF JPEG driver itself:
```
assert failed: jd... (esp_driver_jpeg/jpeg_decode.c:310 "ret == ESP_OK")
```
at `jpeg_decoder_process()`, called from `video_render_task`. Root cause:
the decoupling introduced a new step that didn't exist before — copying
each frame's compressed JPEG bytes out of `avi_player`'s own buffer into a
fresh one for the video mailbox (`video_frame_cb`'s `malloc()` call). That
copy is the buffer eventually passed as `jpeg_decoder_process()`'s **input**
(`bit_stream`) — and right before the 2D-DMA engine reads it, the driver
does `esp_cache_msync(...)` on it and `assert()`s the result
(`esp_driver_jpeg/jpeg_decode.c:308-310`). That sync — and the hardware
JPEG/2D-DMA engine behind it — expects the buffer to be in **PSRAM**. A
plain `malloc()` doesn't guarantee that (it can land in internal SRAM), and
apparently did here. The *original* (pre-decoupling) implementation never
hit this because it decoded `avi_player`'s own internal buffer in place —
that buffer happened to already be PSRAM-backed, so the requirement was
invisible until something introduced a *different* buffer.

**Fix**: allocate every buffer that gets handed to the JPEG decoder as
`bit_stream` — including copies made purely for buffering/queueing
purposes, not just the "real" decode buffers — with
`heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`, not plain `malloc()`. Applied
to both the video and audio frame copies in `src/ui/screen_storage.cpp` for
consistency (audio doesn't strictly need it, since it never touches the
JPEG/2DDMA path, but there's no reason to burden internal SRAM with these
either). **General lesson**: any buffer that's going to be handed to a
DMA-backed hardware peripheral (JPEG decoder, and likely others on this
chip) needs to be allocated with explicit memory-capability flags, not
generic `malloc()` — the peripheral's placement requirements aren't visible
from the function signature alone, and this class of bug won't show up
until you actually run it on hardware.

## AI assistant crash — std::bad_alloc from std::vector/std::string on large buffers

Tapping the talk button, asking a question, and releasing produced a quick
display flash and a silent return to the launcher — actually a full
`abort()`/reboot, not a UI bug. Serial capture showed:

```
abort() was called at PC 0x400f883b on core 0
```

with no assert message and no formal panic backtrace. Symbolizing addresses
found in the raw stack dump against `firmware.elf` (via
`riscv32-esp-elf-addr2line`) pointed at `std::bad_alloc` thrown from
`operator new`, immediately calling `__cxa_allocate_exception`/`__cxa_throw`
— this project builds with C++ exceptions off, so any `throw` (including the
one `std::vector`/`std::string` do internally on allocation failure) goes
straight to `abort()` instead of unwinding.

Root cause, confirmed by adding trace logging around every step of
`ai_service::run_turn` and reproducing on hardware: this board's internal
SRAM heap is only ~180-250 KiB free at runtime (`heap_caps_get_free_size
(MALLOC_CAP_INTERNAL)`), and `sdkconfig.defaults` uses
`CONFIG_SPIRAM_USE_CAPS_ALLOC` (not `CONFIG_SPIRAM_USE_MALLOC`), so plain
`std::vector`/`std::string`/`new`/`malloc()` never spill into PSRAM here —
same underlying rule as the JPEG-buffer bug above, just hit for a different
reason (heap *capacity*, not a DMA requirement). Three buffers in the
assistant pipeline can each reach into the hundreds of KB and were all
plain STL containers: the recorded WAV bytes (`wrap_wav`'s
`std::vector<uint8_t>`), the multipart body that embeds those WAV bytes
(`http_client::post_multipart_file`'s `std::string body`), and the TTS PCM
response (`std::vector<uint8_t> pcm_out`). Any one of them was enough to
blow the ~200 KiB budget; which one failed first varied run to run
depending on recording length/heap fragmentation (confirmed: one capture
crashed inside `wrap_wav` itself, another got past it and crashed during
the Whisper multipart POST instead).

**Fix**: added `http_client::PsramBuf`, a small growable byte buffer backed
by `heap_caps_malloc`/`heap_caps_realloc(..., MALLOC_CAP_SPIRAM)`, and
switched all three buffers above to use it instead of `std::vector`/
`std::string`. Small, short-lived strings (JSON request/response bodies,
which stay in the low KB range) were left as `std::string` — only the
buffers whose size scales with recording/reply length needed to move.
**General lesson**: on this board, *any* buffer whose size scales with
user input or network response (not just DMA targets) needs to be
PSRAM-explicit — the ~250 KiB internal heap is shared with LVGL, WiFi,
and every task stack, so it has far less headroom than the raw "31 MB
free" PSRAM number suggests, and a failed allocation here doesn't
degrade gracefully, it reboots the device (no C++ exceptions).
