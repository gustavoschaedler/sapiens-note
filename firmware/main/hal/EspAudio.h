#pragma once
// IAudio for ES8311 over I2S: 16 kHz mono capture to an SD file, plus playback.
#include <string>

#include "sapiens/Hal.h"

namespace sapiens::fw {

class EspAudio final : public sapiens::IAudio {
public:
    bool init(); // I2C control (ES8311 regs) + I2S channels

    void start_capture() override;
    sapiens::RecordingResult stop_capture() override;
    bool is_capturing() const override { return capturing_; }
    void play(const std::string& storage_key) override;

private:
    bool capturing_{false};
    std::int64_t started_us_{0};
    std::string current_path_;
};

} // namespace sapiens::fw
