#include "EspInput.h"

#include "esp_log.h"
#include "Pins.h"

namespace sapiens::fw {
namespace {
const char* TAG = "sapiens.touch";
} // namespace

bool EspInput::init() {
    // TODO(board): init the shared I2C bus (Pins::I2C_SDA/SCL) and verify the
    // FT6336 (addr 0x38) responds; optionally hook TOUCH_INT.
    ESP_LOGW(TAG, "EspInput::init is a scaffold (TODO: real FT6336 bring-up)");
    return true;
}

std::optional<sapiens::InputEvent> EspInput::poll() {
    // TODO(board): read FT6336 registers 0x02 (touch count) and 0x03.. (X/Y).
    // Convert the 12-bit X/Y to panel coordinates (0..199) and emit a Tap on a
    // rising edge (down). Returns nullopt while idle.
    //
    // Pseudocode once wired:
    //   uint8_t n = read_reg(0x02) & 0x0F;
    //   if (n && !was_touched_) {
    //       was_touched_ = true;
    //       int x = ((read_reg(0x03)&0x0F)<<8) | read_reg(0x04);
    //       int y = ((read_reg(0x05)&0x0F)<<8) | read_reg(0x06);
    //       return sapiens::InputEvent{sapiens::InputKind::Tap, x, y};
    //   }
    //   if (!n) was_touched_ = false;
    return std::nullopt;
}

} // namespace sapiens::fw
