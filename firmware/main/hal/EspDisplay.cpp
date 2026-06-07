#include "EspDisplay.h"

#include <array>
#include <cstring>

#include "esp_log.h"
#include "Pins.h"

namespace sapiens::fw {
namespace {
const char* TAG = "sapiens.epd";
constexpr int W = 200, H = 200;
// Full refresh every N partials to clear e-ink ghosting (see FIRMWARE_SPEC.md).
constexpr int kFullEvery = 8;
} // namespace

bool EspDisplay::init() {
    // TODO(board): init SPI bus (Pins::EPD_*), reset the panel, send the EPD
    // controller init sequence (LUTs, resolution), and a clear. The exact
    // command set depends on the panel controller used by this Waveshare board.
    ESP_LOGW(TAG, "EspDisplay::init is a scaffold (TODO: real EPD bring-up)");
    return true;
}

void EspDisplay::present(const std::uint8_t* pixels, sapiens::RefreshHint hint) {
    // Pack the 200x200, 1-byte-per-pixel buffer (1=black) into 1bpp, MSB-first,
    // which is what e-paper controllers expect (1 bit per pixel, 25 bytes/row).
    static std::array<std::uint8_t, (W / 8) * H> packed;
    std::memset(packed.data(), 0x00, packed.size());
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            if (pixels[y * W + x] >= 128) { // threshold ink level -> 1-bit
                const int byte = y * (W / 8) + (x >> 3);
                packed[byte] |= static_cast<std::uint8_t>(0x80 >> (x & 7));
            }
        }
    }

    bool full = (hint == sapiens::RefreshHint::Full) ||
                (++partials_since_full_ >= kFullEvery);
    if (full) partials_since_full_ = 0;

    // TODO(board): write `packed` to the panel RAM and trigger the appropriate
    // (full vs partial) refresh waveform.
    (void)full;
}

} // namespace sapiens::fw
