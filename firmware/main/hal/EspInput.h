#pragma once
// FT6336 capacitive touch -> sapiens::InputEvent. Polled each UI tick (INT-driven
// read is a later optimization).
#include <optional>

#include "sapiens/Hal.h"

namespace sapiens::fw {

class EspInput {
public:
    bool init(); // bring up the shared I2C bus + FT6336

    // Returns a tap event when a new touch-down is detected, else nullopt.
    // Long-press / swipe classification is a TODO.
    std::optional<sapiens::InputEvent> poll();

private:
    bool was_touched_{false};
};

} // namespace sapiens::fw
