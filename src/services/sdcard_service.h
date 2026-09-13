#pragma once
// TF/SD card over 4-bit SDMMC + FATFS. Pins confirmed from the vendor's own
// mp3_player.ino sample for this board family — see docs/BRINGUP.md "TF/SD card".
#include "esp_err.h"
#include <cstdint>
#include <string>
#include <vector>

namespace sdcard_service {

constexpr const char *MOUNT_POINT = "/sdcard";

struct FileEntry {
    std::string name;
    bool is_dir;
    size_t size;
};

// Idempotent; powers the card slot and mounts FATFS. Called lazily by list_dir()/get_space()
// if not already mounted.
esp_err_t mount();
void unmount();
bool is_mounted();

// path is relative to MOUNT_POINT, e.g. "" for the root or "/MJPEG" for a subfolder.
esp_err_t list_dir(const std::string &path, std::vector<FileEntry> &entries);
esp_err_t get_space(uint64_t &total_bytes, uint64_t &free_bytes);

} // namespace sdcard_service
