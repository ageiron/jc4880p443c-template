# jc4880p443c-template

Reusable starting point for projects on the **Guition JC4880P443C_I_W** board
(ESP32-P4 + ESP32-C6, 4.3" 480×800 MIPI-DSI touchscreen). Everything here was
extracted from the `desk-helper` project after a from-scratch bring-up —
the display driver, sdkconfig, partition table, and PlatformIO config are all
in a verified-working state, so a new project starts flashing and rendering
instead of re-debugging the same hardware quirks.

## What's in the box

```
platformio.ini          — PlatformIO config: pioarduino platform, esp32p4 board, upload command
sdkconfig.defaults       — ESP-IDF Kconfig defaults (chip rev, PSRAM, WiFi, LVGL, console)
partitions.csv           — 16 MB flash partition table (factory app at 0x20000)
CMakeLists.txt            — root ESP-IDF project file
src/
  main.cpp                — minimal bring-up app: display + touch init, "tap me" screen
  CMakeLists.txt           — component registration
  idf_component.yml        — managed component deps (LVGL, ESP-HOSTED, BSP)
  esp_lcd_st7701*.c/.h     — vendor ST7701 MIPI-DSI display driver (BSP doesn't support this panel)
docs/
  BRINGUP.md               — every gotcha hit bringing this board up, and why each fix works
  JC4880P443C_I_W Specifications-EN-V1.0*.pdf — vendor spec sheet
.vscode/tasks.json        — Build / Upload / Monitor / Test / Sync tasks
.continue/rules/project-context.md — points the local LLM at FUNCTIONAL_DESCRIPTION.md / CLAUDE.md / BRINGUP.md
CLAUDE.md                 — agent context template
FUNCTIONAL_DESCRIPTION.md — living-spec template
```

## Creating a new project from this template

1. **Copy the directory**, don't work inside this one:
   ```powershell
   Copy-Item -Recurse C:\dev\projects\jc4880p443c-template C:\dev\projects\<new-project-name>
   ```

2. **Replace placeholders.** Every file with `{{PROJECT_NAME}}` needs it
   swapped for your new project's slug (e.g. `desk-helper`, `shop-timer`).
   Files that contain it: `CMakeLists.txt`, `platformio.ini`, `src/main.cpp`,
   `CLAUDE.md`, `FUNCTIONAL_DESCRIPTION.md`. Also replace `{{DATE}}` in
   `FUNCTIONAL_DESCRIPTION.md` with today's date. Quick way to do the swap:
   ```powershell
   cd C:\dev\projects\<new-project-name>
   $name = "<new-project-name>"
   $date = Get-Date -Format "yyyy-MM-dd"
   Get-ChildItem -Recurse -File -Include *.md,*.cpp,*.txt,*.ini |
     ForEach-Object {
       (Get-Content $_.FullName -Raw) `
         -replace '\{\{PROJECT_NAME\}\}', $name `
         -replace '\{\{DATE\}\}', $date |
       Set-Content $_.FullName -Encoding utf8
     }
   ```

3. **Set the upload port and esptool path.** In `platformio.ini`, check
   `upload_port` / `monitor_port` (COM port for this specific board — may
   differ from the one used for other JC4880P443C boards) and confirm the
   `upload_command`'s esptool path matches your machine's system Python
   install (see `docs/BRINGUP.md` — "esptool venv is broken").

4. **git init and first commit:**
   ```powershell
   git init
   git add .
   git commit -m "Initial commit from jc4880p443c-template"
   ```

5. **First build + flash:**
   ```
   pio run                    # downloads ESP-IDF (~1 GB) on first run
   pio run --target upload
   pio device monitor
   ```
   You should see the ST7701 init log lines, then a dark screen with a
   "{{PROJECT_NAME}} — JC4880P443C_I_W bring-up OK" title and a "Tap me"
   button. Tapping it should increment a counter — that confirms both
   display and touch are working before you build anything on top.

6. **Fill in `FUNCTIONAL_DESCRIPTION.md`** with the actual purpose of the new
   project (it currently just points back at the shared hardware doc).

7. **Start replacing `create_ui()` in `src/main.cpp`** with your actual
   application. Leave everything above it (`app_main` through touch init)
   alone unless you have a specific reason to change it — see
   `docs/BRINGUP.md` first if you do.

## If you don't need WiFi

Remove the ESP-HOSTED block from `sdkconfig.defaults`, the
`espressif__esp_hosted` / `espressif__esp_wifi_remote` entries from
`src/idf_component.yml` and `src/CMakeLists.txt`, and the
`esp_wifi`/`esp_netif`/`esp_event` requires. Details in `docs/BRINGUP.md`.

## Keeping the template current

If a future project on this board hits a new gotcha or finds a better fix for
one documented here, port the fix back into this template (not just the
one project) — that's the whole point of having it. Update `docs/BRINGUP.md`
with the *why*, not just the fix.
