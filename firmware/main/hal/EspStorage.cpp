#include "EspStorage.h"

#include <sys/stat.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

namespace sapiens::fw {
namespace {
const char* TAG = "sapiens.sd";
sdmmc_card_t* s_card = nullptr;
} // namespace

bool EspStorage::mount_sd(const std::string& mount_point) {
    // TODO(board): this uses the SPI host as a placeholder. Confirm whether the
    // board wires the TF slot to SDMMC or SPI, and set the pins from Pins.h.
    esp_vfs_fat_sdmmc_mount_config_t cfg = {};
    cfg.format_if_mount_failed = false;
    cfg.max_files = 8;
    cfg.allocation_unit_size = 16 * 1024;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    // TODO(board): configure spi bus (MOSI/MISO/SCLK/CS) before mounting.
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();

    esp_err_t err = esp_vfs_fat_sdspi_mount(mount_point.c_str(), &host, &slot,
                                            &cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount failed: %s", esp_err_to_name(err));
        return false;
    }
    ::mkdir(kRoot, 0775);
    ::mkdir(kAudioDir, 0775);
    ESP_LOGI(TAG, "microSD mounted at %s", mount_point.c_str());
    return true;
}

void EspStorage::unmount_sd() {
    if (s_card) {
        esp_vfs_fat_sdcard_unmount("/sdcard", s_card);
        s_card = nullptr;
    }
}

} // namespace sapiens::fw
