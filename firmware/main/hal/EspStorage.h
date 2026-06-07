#pragma once
// microSD (FAT32) mount helper. Notes DB + audio files live here, not in flash.
#include <string>

namespace sapiens::fw {

class EspStorage {
public:
    // Mounts the SD card at `mount_point`. Returns true on success.
    // TODO(board): choose SDMMC vs SPI and set the correct pins (see Pins.h).
    static bool mount_sd(const std::string& mount_point = "/sdcard");
    static void unmount_sd();

    static constexpr const char* kRoot = "/sdcard/sapiensnote";
    static constexpr const char* kAudioDir = "/sdcard/sapiensnote/audio";
};

} // namespace sapiens::fw
