#include "EspStorage.h"

#include <sys/stat.h>
#include <dirent.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "Pins.h"

namespace sapiens::fw {
namespace {
const char* TAG = "sapiens.sd";
sdmmc_card_t* s_card = nullptr;

void ensure_dir(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        if (mkdir(path, 0775) != 0)
            ESP_LOGW(TAG, "mkdir %s failed", path);
    }
}
} // namespace

bool EspStorage::mount_sd(const std::string& mount_point) {
    namespace p = sapiens::fw::pins;

    // SDMMC in 1-bit mode on the GPIO-matrix pins (CLK=39, CMD=41, D0=40),
    // matching the reference board (SD_MMC 1-bit).
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = p::SD_CLK;
    slot.cmd = p::SD_CMD;
    slot.d0  = p::SD_D0;
    slot.cd  = SDMMC_SLOT_NO_CD;
    slot.wp  = SDMMC_SLOT_NO_WP;

    esp_vfs_fat_sdmmc_mount_config_t mcfg = {};
    mcfg.format_if_mount_failed = false;
    mcfg.max_files = 5;
    mcfg.allocation_unit_size = 16 * 1024;

    esp_err_t err = esp_vfs_fat_sdmmc_mount(mount_point.c_str(), &host, &slot,
                                            &mcfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s (CLK=%d CMD=%d D0=%d)",
                 esp_err_to_name(err), p::SD_CLK, p::SD_CMD, p::SD_D0);
        s_card = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "SD mounted at %s", mount_point.c_str());
    sdmmc_card_print_info(stdout, s_card);

    // Make sure the notes/audio directories exist.
    ensure_dir(kRoot);
    ensure_dir(kAudioDir);

    // Diagnostic: list any recordings already on the card (survives reboots), so a
    // capture done in a prior session is visible here with its byte size.
    if (DIR* d = opendir(kAudioDir)) {
        int n = 0;
        struct dirent* e;
        while ((e = readdir(d)) != nullptr) {
            if (e->d_name[0] == '.') continue;
            std::string full = std::string(kAudioDir) + "/" + e->d_name;
            struct stat st;
            long sz = (stat(full.c_str(), &st) == 0) ? (long)st.st_size : -1;
            ESP_LOGI(TAG, "  audio file: %s (%ld bytes)", e->d_name, sz);
            ++n;
        }
        closedir(d);
        ESP_LOGI(TAG, "%d recording(s) in %s", n, kAudioDir);
    }
    return true;
}

void* EspStorage::card() { return s_card; }

void EspStorage::unmount_sd() {
    if (s_card) {
        esp_vfs_fat_sdcard_unmount("/sdcard", s_card);
        s_card = nullptr;
    }
}

} // namespace sapiens::fw
