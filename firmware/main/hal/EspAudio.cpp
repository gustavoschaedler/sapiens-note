#include "EspAudio.h"

#include <cstring>
#include <ctime>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#include "EspStorage.h"

extern "C" {
#include "audio_bsp.h"
}

namespace sapiens::fw {
namespace {
const char* TAG = "sapiens.audio";
constexpr int kSampleRate = 16000;
constexpr int kBytesPerSec = kSampleRate * 2; // mono, 16-bit
constexpr int kRecStereoBytes = 8 * 1024;     // I2S read chunk (stereo)

void write_wav_header(std::FILE* f, std::uint32_t data_bytes) {
    std::uint32_t riff = data_bytes + 36, srate = kSampleRate, brate = kBytesPerSec;
    std::uint32_t fmt_len = 16;
    std::uint16_t fmt = 1, ch = 1, balign = 2, bps = 16;
    std::fseek(f, 0, SEEK_SET);
    std::fwrite("RIFF", 1, 4, f);   std::fwrite(&riff, 4, 1, f);
    std::fwrite("WAVE", 1, 4, f);   std::fwrite("fmt ", 1, 4, f);
    std::fwrite(&fmt_len, 4, 1, f); std::fwrite(&fmt, 2, 1, f);
    std::fwrite(&ch, 2, 1, f);      std::fwrite(&srate, 4, 1, f);
    std::fwrite(&brate, 4, 1, f);   std::fwrite(&balign, 2, 1, f);
    std::fwrite(&bps, 2, 1, f);
    std::fwrite("data", 1, 4, f);   std::fwrite(&data_bytes, 4, 1, f);
}

void capture_trampoline(void* arg) { static_cast<EspAudio*>(arg)->capture_loop(); }
void playback_trampoline(void* arg) { static_cast<EspAudio*>(arg)->playback_loop(); }
} // namespace

bool EspAudio::init() {
    // Brings up I2S + ES8311 over the shared I2C bus (the codec stack creates the
    // I2C master bus on port 0; EspInput later reuses it for the touch panel).
    audio_bsp_init();
    audio_play_init();
    ready_ = true;
    ESP_LOGI(TAG, "ES8311 + I2S up (16 kHz, mono capture / stereo playback)");
    return true;
}

// --- capture -----------------------------------------------------------------

void EspAudio::start_capture() {
    if (!ready_ || capturing_) return;
    started_us_ = esp_timer_get_time();
    // Filename from the RTC wall clock: rec-YYYYMMDD-HHMMSS.wav (a chosen name is
    // appended later if the user renames it in the dashboard).
    std::time_t t = ::time(nullptr);
    std::tm tmv{};
    gmtime_r(&t, &tmv); // TZ unset -> system time is the wall clock we store
    char stem[96];
    std::snprintf(stem, sizeof(stem), "/rec-%04d%02d%02d-%02d%02d%02d.wav",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    current_path_ = std::string(EspStorage::kAudioDir) + stem;
    rec_file_ = std::fopen(current_path_.c_str(), "wb");
    if (!rec_file_) {
        ESP_LOGE(TAG, "cannot open %s for write", current_path_.c_str());
        return;
    }
    std::uint8_t hdr[44] = {0};
    std::fwrite(hdr, 1, sizeof(hdr), rec_file_); // placeholder, patched on stop
    rec_bytes_ = 0;
    capturing_ = true;
    xTaskCreate(capture_trampoline, "sn_rec", 8192, this, 5, &rec_task_);
    ESP_LOGI(TAG, "recording -> %s", current_path_.c_str());
}

void EspAudio::capture_loop() {
    auto* sbuf = static_cast<std::int16_t*>(heap_caps_malloc(kRecStereoBytes, MALLOC_CAP_8BIT));
    auto* mbuf = static_cast<std::int16_t*>(heap_caps_malloc(kRecStereoBytes / 2, MALLOC_CAP_8BIT));
    if (sbuf && mbuf) {
        const int frames = kRecStereoBytes / 4; // stereo 16-bit frames -> mono samples
        while (capturing_) {
            audio_playback_read(sbuf, kRecStereoBytes);
            for (int i = 0; i < frames; ++i) mbuf[i] = sbuf[i * 2]; // left channel
            size_t w = std::fwrite(mbuf, 1, frames * 2, rec_file_);
            rec_bytes_ += static_cast<std::uint32_t>(w);
            if (w == 0) break;
        }
    }
    if (sbuf) heap_caps_free(sbuf);
    if (mbuf) heap_caps_free(mbuf);
    rec_task_ = nullptr;
    vTaskDelete(nullptr);
}

sapiens::RecordingResult EspAudio::stop_capture() {
    capturing_ = false;
    while (rec_task_) vTaskDelay(pdMS_TO_TICKS(10)); // let the worker drain
    std::uint32_t bytes = rec_bytes_;
    if (rec_file_) {
        write_wav_header(rec_file_, bytes);
        std::fclose(rec_file_);
        rec_file_ = nullptr;
    }
    sapiens::RecordingResult r;
    r.storage_key = current_path_;
    int secs = static_cast<int>(bytes / kBytesPerSec);
    r.duration_secs = secs < 1 ? 1 : secs;
    r.sample_rate = kSampleRate;
    r.channels = 1;
    r.size_bytes = static_cast<std::int64_t>(bytes);
    ESP_LOGI(TAG, "stopped: %u bytes (%ds)", bytes, r.duration_secs);
    return r;
}

// --- playback ----------------------------------------------------------------

void EspAudio::play(const std::string& storage_key) {
    if (!ready_ || storage_key.empty()) return;
    stop_playback(); // stop any current clip first
    play_path_ = storage_key;
    stop_play_ = false;
    paused_ = false;
    play_total_bytes_ = 0;
    play_done_bytes_ = 0;
    playing_ = true;
    xTaskCreate(playback_trampoline, "sn_play", 8192, this, 5, &play_task_);
    ESP_LOGI(TAG, "playing %s", play_path_.c_str());
}

void EspAudio::playback_loop() {
    std::FILE* f = std::fopen(play_path_.c_str(), "rb");
    if (f) {
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        play_total_bytes_ = (sz > 44) ? static_cast<std::uint32_t>(sz - 44) : 0;
        std::fseek(f, 44, SEEK_SET); // skip WAV header

        const int monoBytes = 1024;
        auto* monoBuf = static_cast<std::uint8_t*>(heap_caps_malloc(monoBytes, MALLOC_CAP_8BIT));
        auto* stereo = static_cast<std::int16_t*>(heap_caps_malloc(monoBytes * 2, MALLOC_CAP_8BIT));
        audio_playback_set_vol(90);
        while (f && monoBuf && stereo && !stop_play_) {
            if (paused_) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
            size_t r = std::fread(monoBuf, 1, monoBytes, f);
            if (r == 0) break;
            if (r & 1) --r;
            int samples = static_cast<int>(r / 2);
            auto* mono = reinterpret_cast<std::int16_t*>(monoBuf);
            for (int i = 0; i < samples; ++i) { stereo[i * 2] = mono[i]; stereo[i * 2 + 1] = mono[i]; }
            audio_playback_write(stereo, samples * 2 * sizeof(std::int16_t));
            play_done_bytes_ += static_cast<std::uint32_t>(r);
        }
        if (monoBuf) heap_caps_free(monoBuf);
        if (stereo) heap_caps_free(stereo);
        std::fclose(f);
    }
    playing_ = false;
    paused_ = false;
    play_task_ = nullptr;
    vTaskDelete(nullptr);
}

void EspAudio::pause_resume_playback() {
    if (playing_) paused_ = !paused_;
}

void EspAudio::stop_playback() {
    stop_play_ = true;
    while (play_task_) vTaskDelay(pdMS_TO_TICKS(10));
    playing_ = false;
    paused_ = false;
}

int EspAudio::playback_pos_secs() const {
    return static_cast<int>(play_done_bytes_.load() / kBytesPerSec);
}
int EspAudio::playback_total_secs() const {
    return static_cast<int>(play_total_bytes_.load() / kBytesPerSec);
}
float EspAudio::playback_progress() const {
    std::uint32_t t = play_total_bytes_.load();
    return t ? static_cast<float>(play_done_bytes_.load()) / static_cast<float>(t) : 0.0f;
}

} // namespace sapiens::fw
