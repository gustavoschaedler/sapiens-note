#pragma once
// Shared boot-splash animation timeline so every client (hand simulator, LVGL
// simulator, firmware) renders the exact same frames: the product name is typed
// out letter by letter, then the whole title blinks. Total run = 4000 ms (see
// AppController::splash_duration_ms).

#include <string>

namespace sapiens::splash {

// One animation frame derived purely from elapsed time.
struct Frame {
    std::string line1;  // revealed prefix of "Sapiens"
    std::string line2;  // revealed prefix of "Note"
    bool visible;       // false during the "off" half of a blink
};

inline Frame compute(int elapsed_ms) {
    const std::string A = "Sapiens", B = "Note";
    const int per_letter = 130;          // typewriter speed (ms per glyph)
    const int blink_start = 1700;        // begin blinking after a short hold
    const int blink_half = 450;          // on/off half-period
    const int total = static_cast<int>(A.size() + B.size());

    int n = elapsed_ms / per_letter;
    if (n < 0) n = 0;
    if (n > total) n = total;
    int n1 = n < static_cast<int>(A.size()) ? n : static_cast<int>(A.size());
    int n2 = n - static_cast<int>(A.size());
    if (n2 < 0) n2 = 0;

    bool visible = true;
    if (n >= total) { // typing finished -> blink
        int bt = elapsed_ms - blink_start;
        if (bt < 0) bt = 0;
        visible = (bt / blink_half) % 2 == 0;
    }
    return {A.substr(0, n1), B.substr(0, n2), visible};
}

} // namespace sapiens::splash
