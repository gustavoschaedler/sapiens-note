#include "EspAudio.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "EspStorage.h"
#include "Pins.h"

namespace sapiens::fw {
namespace {
const char* TAG = "sapiens.audio";
constexpr int kSampleRate = 16000;
} // namespace

bool EspAudio::init() {
    // TODO(board): configure ES8311 over I2C (clock, ADC/DAC, PA enable) and set
    // up an I2S std channel (Pins::I2S_*) at 16 kHz mono, 16-bit.
    ESP_LOGW(TAG, "EspAudio::init is a scaffold (TODO: real ES8311/I2S bring-up)");
    return true;
}

void EspAudio::start_capture() {
    capturing_ = true;
    started_us_ = esp_timer_get_time();
    current_path_ = std::string(EspStorage::kAudioDir) + "/rec-" +
                    std::to_string(started_us_) + ".wav";
    // TODO(board): start the I2S RX channel and a task that streams frames from
    // I2S into a WAV file on the SD card (write the header now, patch sizes on
    // stop). Never buffer the whole clip in RAM.
}

sapiens::RecordingResult EspAudio::stop_capture() {
    capturing_ = false;
    const int secs =
        static_cast<int>((esp_timer_get_time() - started_us_) / 1'000'000);
    // TODO(board): stop I2S RX, finalize the WAV header with the real sizes.
    sapiens::RecordingResult r;
    r.storage_key = current_path_;
    r.duration_secs = secs < 1 ? 1 : secs;
    r.sample_rate = kSampleRate;
    r.channels = 1;
    r.size_bytes = static_cast<std::int64_t>(r.duration_secs) * kSampleRate * 2;
    return r;
}

void EspAudio::play(const std::string& storage_key) {
    // TODO(board): open the WAV, enable the ES8311 DAC/PA, stream to I2S TX.
    ESP_LOGI(TAG, "play %s (scaffold)", storage_key.c_str());
}

} // namespace sapiens::fw
