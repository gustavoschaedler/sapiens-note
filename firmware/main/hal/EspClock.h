#pragma once
// IClock for ESP-IDF: wall clock via time(), monotonic via esp_timer.
#include <ctime>

#include "esp_timer.h"
#include "sapiens/Hal.h"

namespace sapiens::fw {

class EspClock final : public sapiens::IClock {
public:
    // Reads the PCF85063 RTC (I2C @0x51) into the system clock. If the RTC has no
    // valid time, seeds it (and the system clock) with the firmware build time.
    // Call after the I2C bus exists (i.e. after EspInput::init).
    void init();

    // Persist the current system time (e.g. just set by NTP) into the PCF85063, so
    // the real time survives reboots. Returns true on success.
    bool write_system_to_rtc();

    sapiens::Millis now_ms() const override {
        return static_cast<sapiens::Millis>(::time(nullptr)) * 1000;
    }
    std::int64_t monotonic_ms() const override {
        return esp_timer_get_time() / 1000; // us -> ms
    }
};

} // namespace sapiens::fw
