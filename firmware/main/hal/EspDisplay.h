#pragma once
// IDisplay for the 200x200 1-bit e-paper panel (SSD1681-class controller).
// Receives the same 1-byte-per-pixel framebuffer the simulator renders, converts
// it to the panel's 1bpp format, and pushes it via the ported epaper driver.
#include "sapiens/Hal.h"

// Ported from the working Pala Note reference firmware.
class epaper_driver_display;
class board_power_bsp_t;

namespace sapiens::fw {

class EspDisplay final : public sapiens::IDisplay {
public:
    bool init();   // power rails + SPI + panel bring-up; returns false on failure
    sapiens::DisplaySize size() const override { return {200, 200}; }
    void present(const std::uint8_t* pixels, sapiens::RefreshHint hint) override;

private:
    void blit(const std::uint8_t* pixels); // 1B/px -> panel 1bpp buffer
    void full_refresh();                    // full waveform + re-arm partial mode

    board_power_bsp_t* power_{nullptr};
    epaper_driver_display* panel_{nullptr};
    int partials_since_full_{0};
};

} // namespace sapiens::fw
