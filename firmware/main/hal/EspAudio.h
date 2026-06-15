#pragma once
// IAudio for ES8311 over I2S: 16 kHz mono capture to an SD WAV file, plus
// playback. Wraps the vendored audio_bsp (esp_codec_dev + ES8311) and streams
// to/from the SD card on a worker task so the UI loop never blocks.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sapiens/Hal.h"

namespace sapiens::fw {

class EspAudio final : public sapiens::IAudio {
public:
    bool init(); // codec + I2S bring-up (shares the touch I2C bus)

    void start_capture() override;
    sapiens::RecordingResult stop_capture() override;
    bool is_capturing() const override { return capturing_; }

    void play(const std::string& storage_key) override;
    void pause_resume_playback() override;
    void stop_playback() override;
    bool is_playing() const override { return playing_ && !paused_; }
    bool is_paused() const override { return paused_; }
    int playback_pos_secs() const override;
    int playback_total_secs() const override;
    float playback_progress() const override;

    // worker-task bodies (public so the C task trampoline can reach them)
    void capture_loop();
    void playback_loop();

private:
    bool ready_{false};

    // capture
    std::atomic<bool> capturing_{false};
    std::FILE* rec_file_{nullptr};
    TaskHandle_t rec_task_{nullptr};
    std::atomic<std::uint32_t> rec_bytes_{0};
    std::int64_t started_us_{0};
    std::string current_path_;

    // playback
    std::atomic<bool> playing_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> stop_play_{false};
    TaskHandle_t play_task_{nullptr};
    std::string play_path_;
    std::atomic<std::uint32_t> play_total_bytes_{0};
    std::atomic<std::uint32_t> play_done_bytes_{0};
};

} // namespace sapiens::fw
