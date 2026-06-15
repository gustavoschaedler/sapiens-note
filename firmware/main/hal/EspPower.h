#pragma once
// IPower for ESP-IDF: battery via ADC1_CH3 (GPIO4, 2:1 divider), deep sleep.
#include "esp_sleep.h"
#include "sapiens/Hal.h"

namespace sapiens::fw {

class EspPower final : public sapiens::IPower {
public:
    // Battery charge 0..100, or -1 if it can't be read. Reads the ADC at most once
    // every ~30 s and caches the result (the header polls this every repaint).
    int battery_percent() const override;

    bool on_external_power() const override {
        // TODO(board): detect USB VBUS if a sense line is available.
        return false;
    }

    // Enter deep sleep; wakes when either nav button (GPIO0/GPIO18) is pressed.
    static void deep_sleep(uint64_t wake_after_us = 0);
};

} // namespace sapiens::fw
