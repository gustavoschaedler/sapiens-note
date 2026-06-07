#pragma once
// IClock for ESP-IDF: wall clock via time(), monotonic via esp_timer.
#include <ctime>

#include "esp_timer.h"
#include "sapiens/Hal.h"

namespace sapiens::fw {

class EspClock final : public sapiens::IClock {
public:
    sapiens::Millis now_ms() const override {
        // TODO(board): set the RTC (PCF85063) / SNTP so wall time is real.
        return static_cast<sapiens::Millis>(::time(nullptr)) * 1000;
    }
    std::int64_t monotonic_ms() const override {
        return esp_timer_get_time() / 1000; // us -> ms
    }
};

} // namespace sapiens::fw
