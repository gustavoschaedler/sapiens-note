#pragma once
// IPower for ESP-IDF: battery via ADC, deep sleep via esp_sleep.
#include "esp_sleep.h"
#include "sapiens/Hal.h"

namespace sapiens::fw {

class EspPower final : public sapiens::IPower {
public:
    int battery_percent() const override {
        // TODO(board): read BAT_ADC, convert via the divider ratio, map V->%.
        return -1; // unknown until wired
    }
    bool on_external_power() const override {
        // TODO(board): detect USB VBUS if a sense line is available.
        return false;
    }

    // Enter deep sleep; wake on touch INT / PWR button / RTC alarm.
    static void deep_sleep(uint64_t wake_after_us = 0) {
        // TODO(board): esp_sleep_enable_ext1_wakeup(TOUCH/PWR mask, ...);
        if (wake_after_us) esp_sleep_enable_timer_wakeup(wake_after_us);
        esp_deep_sleep_start();
    }
};

} // namespace sapiens::fw
