#pragma once
// FT6336 capacitive touch (I2C addr 0x38) -> sapiens::InputEvent. Polled each UI
// tick. The board also carries ES8311 (0x18), PCF85063 RTC (0x51) and SHTC3 (0x70)
// on the same bus; init() scans and logs all of them.
#include <cstdint>
#include <optional>

#include "driver/i2c_master.h"
#include "sapiens/Hal.h"

namespace sapiens::fw {

class EspInput {
public:
    bool init(); // bring up the shared I2C bus + FT6336

    // Returns a Tap on a fresh touch-down (rising edge), else nullopt.
    std::optional<sapiens::InputEvent> poll();

private:
    i2c_master_bus_handle_t bus_{nullptr};
    i2c_master_dev_handle_t touch_{nullptr};
    bool was_touched_{false};
    // Physical buttons are sampled by a dedicated 5 ms task (see EspInput.cpp) and
    // their events delivered through a queue, so no per-button state lives here.
};

} // namespace sapiens::fw
