#include "sdcard_service.h"
#include "driver/sdmmc_host.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <dirent.h>
#include <sys/stat.h>
#include <cstring>

namespace sdcard_service {

static const char *TAG = "sdcard_service";

// Native 4-bit SDMMC on Slot 0 — NOT SPI mode, and NOT Slot 1. Found by reading the vendor's
// EV-board BSP source (bsp_sdcard_mount() in the vendor SDK package) and ESP-IDF's own
// soc/esp32p4/sdmmc_periph.c: ESP32-P4's SDMMC peripheral has two genuinely independent slots.
// ESP-HOSTED (WiFi, over SDIO to the C6) uses Slot 1 with GPIO-matrix-routed pins (18/19/14-17).
// Slot 0 is entirely different hardware — fixed, non-GPIO-matrix IOMUX pins, and doesn't
// conflict with Slot 1 at all. Confirmed against ESP-IDF's own
// `SDMMC_SLOT_CONFIG_DEFAULT()` for esp32p4, which independently resolves to exactly these pins
// (CLK=43, CMD=44, D0-D3=39-42) — the *pins* from vendor mp3_player.ino were right all along;
// the actual bug was `SDMMC_HOST_DEFAULT()` defaulting `.slot` to `SDMMC_HOST_SLOT_1` (the same
// slot WiFi uses) rather than SLOT_0, which is what caused every earlier attempt to fail — the
// SPI-mode rewrite that followed avoided the peripheral collision but never fixed the real
// problem, and additionally left the card unpowered (see below), so it also failed, just with a
// different symptom (timeout instead of a WiFi-corrupting crash). See docs/BRINGUP.md "TF card
// + WiFi share the ESP32-P4's one SDMMC/SDIO peripheral" for the full history.
//
// Power: the schematic's "ESP_LDO_VO4" net (feeding the AO3401 switch that's permanently on,
// since R10 is unpopulated — see docs/BRINGUP.md) is exactly what it says: on-chip LDO channel
// 4 output. That internal regulator channel is off by default and must be explicitly enabled in
// software via sd_pwr_ctrl_new_on_chip_ldo() — without it the card has no power at all,
// regardless of which pins or which mode (SDMMC or SPI) talk to it. This is the same on-chip LDO
// peripheral already used for the display's MIPI DSI PHY power in main.cpp (channel 3 there,
// channel 4 here).
constexpr int SD_LDO_CHAN_ID = 4;

static sdmmc_card_t *s_card = nullptr;
static sd_pwr_ctrl_handle_t s_pwr_ctrl_handle = nullptr;
static bool s_mounted = false;

esp_err_t mount() {
    if (s_mounted) return ESP_OK;

    if (!s_pwr_ctrl_handle) {
        sd_pwr_ctrl_ldo_config_t ldo_config = {};
        ldo_config.ldo_chan_id = SD_LDO_CHAN_ID;
        esp_err_t pwr_err = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &s_pwr_ctrl_handle);
        if (pwr_err != ESP_OK) {
            ESP_LOGE(TAG, "SD LDO power control init failed: %s", esp_err_to_name(pwr_err));
            return pwr_err;
        }
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 5;
    mount_config.allocation_unit_size = 16 * 1024;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    host.pwr_ctrl_handle = s_pwr_ctrl_handle;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT(); // already correct for esp32p4 slot 0
    slot_config.width = 4;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t err = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s (card inserted? FAT32, <32GB?)", esp_err_to_name(err));
        return err;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "SD card mounted (SDMMC slot 0): %s", s_card->cid.name);
    return ESP_OK;
}

void unmount() {
    if (!s_mounted) return;
    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
    s_card = nullptr;
    s_mounted = false;
}

bool is_mounted() { return s_mounted; }

esp_err_t list_dir(const std::string &path, std::vector<FileEntry> &entries) {
    entries.clear();
    if (!s_mounted) {
        esp_err_t err = mount();
        if (err != ESP_OK) return err;
    }

    std::string full = std::string(MOUNT_POINT) + path;
    DIR *dir = opendir(full.c_str());
    if (!dir) return ESP_FAIL;

    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        FileEntry fe;
        fe.name = ent->d_name;
        std::string entry_path = full + "/" + fe.name;
        struct stat st;
        if (stat(entry_path.c_str(), &st) == 0) {
            fe.is_dir = S_ISDIR(st.st_mode);
            fe.size = static_cast<size_t>(st.st_size);
        } else {
            fe.is_dir = false;
            fe.size = 0;
        }
        entries.push_back(fe);
    }
    closedir(dir);
    return ESP_OK;
}

esp_err_t get_space(uint64_t &total_bytes, uint64_t &free_bytes) {
    if (!s_mounted) {
        esp_err_t err = mount();
        if (err != ESP_OK) return err;
    }

    FATFS *fs = nullptr;
    DWORD free_clusters = 0;
    if (f_getfree("0:", &free_clusters, &fs) != FR_OK || fs == nullptr) return ESP_FAIL;

    uint64_t total_sectors = static_cast<uint64_t>(fs->n_fatent - 2) * fs->csize;
    uint64_t free_sectors = static_cast<uint64_t>(free_clusters) * fs->csize;
    total_bytes = total_sectors * fs->ssize;
    free_bytes = free_sectors * fs->ssize;
    return ESP_OK;
}

} // namespace sdcard_service
