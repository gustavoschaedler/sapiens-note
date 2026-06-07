#pragma once
// IDisplay for the 200x200 1-bit e-paper panel. Receives the same 1-byte-per-pixel
// framebuffer the simulator renders, packs it to 1bpp, and pushes it to the EPD.
#include "sapiens/Hal.h"

namespace sapiens::fw {

class EspDisplay final : public sapiens::IDisplay {
public:
    bool init();   // bring up SPI + panel; returns false on failure
    sapiens::DisplaySize size() const override { return {200, 200}; }
    void present(const std::uint8_t* pixels, sapiens::RefreshHint hint) override;

private:
    int partials_since_full_{0};
};

} // namespace sapiens::fw
