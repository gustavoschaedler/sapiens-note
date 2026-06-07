#pragma once
// Real audio capture/playback for the desktop simulator via SDL2 (16 kHz mono).
// Implements sapiens::IAudio incl. player controls (pause/stop/progress). If no
// capture device exists (no mic / CI), it degrades to a silent clip.
#include <SDL.h>

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <vector>

#include "sapiens/Hal.h"

namespace sapiens::sim {

class SdlAudio final : public sapiens::IAudio {
public:
    explicit SdlAudio(std::string data_dir) : dir_(std::move(data_dir)) {
        if (SDL_WasInit(SDL_INIT_AUDIO) == 0) SDL_InitSubSystem(SDL_INIT_AUDIO);
    }
    ~SdlAudio() override {
        if (cap_dev_) SDL_CloseAudioDevice(cap_dev_);
        if (play_dev_) SDL_CloseAudioDevice(play_dev_);
    }

    // --- capture ------------------------------------------------------------
    void start_capture() override {
        samples_.clear();
        capturing_ = true;
        SDL_AudioSpec want{}, have{};
        want.freq = 16000;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 1024;
        want.callback = &SdlAudio::capture_cb;
        want.userdata = this;
        cap_dev_ = SDL_OpenAudioDevice(nullptr, 1, &want, &have, 0);
        have_freq_ = (cap_dev_ && have.freq) ? have.freq : 16000;
        start_ticks_ = SDL_GetTicks();
        if (!cap_dev_)
            std::fprintf(stderr, "[audio] capture unavailable: %s (silence)\n",
                         SDL_GetError());
        else
            SDL_PauseAudioDevice(cap_dev_, 0);
    }

    RecordingResult stop_capture() override {
        capturing_ = false;
        if (cap_dev_) { SDL_PauseAudioDevice(cap_dev_, 1); SDL_CloseAudioDevice(cap_dev_); cap_dev_ = 0; }

        std::vector<std::int16_t> data;
        { std::lock_guard<std::mutex> lk(mu_); data.swap(samples_); }
        if (data.empty()) {
            int secs = static_cast<int>((SDL_GetTicks() - start_ticks_) / 1000);
            if (secs < 1) secs = 1;
            data.assign(static_cast<std::size_t>(secs) * have_freq_, 0);
        }

        RecordingResult r;
        r.sample_rate = have_freq_;
        r.channels = 1;
        r.duration_secs = static_cast<int>(data.size() / (have_freq_ ? have_freq_ : 16000));
        if (r.duration_secs < 1) r.duration_secs = 1;
        r.storage_key = dir_ + "/" + timestamped_name();
        r.size_bytes = static_cast<std::int64_t>(data.size() * 2) + 44;
        write_wav(r.storage_key, data, have_freq_, 1);
        return r;
    }

    bool is_capturing() const override { return capturing_; }

    // --- playback -----------------------------------------------------------
    void play(const std::string& storage_key) override {
        SDL_AudioSpec spec{};
        Uint8* wav = nullptr;
        Uint32 len = 0;
        if (!SDL_LoadWAV(storage_key.c_str(), &spec, &wav, &len)) {
            std::fprintf(stderr, "[audio] load failed: %s\n", SDL_GetError());
            return;
        }
        // (Re)open a persistent device only if the format changed.
        if (play_dev_ && (spec.freq != play_spec_.freq ||
                          spec.format != play_spec_.format ||
                          spec.channels != play_spec_.channels)) {
            SDL_CloseAudioDevice(play_dev_);
            play_dev_ = 0;
        }
        if (!play_dev_) {
            play_dev_ = SDL_OpenAudioDevice(nullptr, 0, &spec, nullptr, 0);
            play_spec_ = spec;
        }
        if (!play_dev_) { SDL_FreeWAV(wav); return; }

        bytes_per_sec_ = spec.freq * spec.channels * (SDL_AUDIO_BITSIZE(spec.format) / 8);
        total_bytes_ = len;
        SDL_ClearQueuedAudio(play_dev_);
        SDL_QueueAudio(play_dev_, wav, len);   // queue, then start immediately
        SDL_PauseAudioDevice(play_dev_, 0);
        paused_ = false;
        SDL_FreeWAV(wav);
    }

    void pause_resume_playback() override {
        if (!play_dev_ || queued() == 0) return;
        paused_ = !paused_;
        SDL_PauseAudioDevice(play_dev_, paused_ ? 1 : 0);
    }

    void stop_playback() override {
        if (!play_dev_) return;
        SDL_PauseAudioDevice(play_dev_, 1);
        SDL_ClearQueuedAudio(play_dev_);
        paused_ = false;
        total_bytes_ = 0;
    }

    bool is_playing() const override { return play_dev_ && !paused_ && queued() > 0; }
    bool is_paused() const override { return play_dev_ && paused_ && queued() > 0; }

    int playback_total_secs() const override {
        return bytes_per_sec_ ? static_cast<int>(total_bytes_ / bytes_per_sec_) : 0;
    }
    int playback_pos_secs() const override {
        if (!bytes_per_sec_ || total_bytes_ == 0) return 0;
        return static_cast<int>(played_bytes() / bytes_per_sec_);
    }
    float playback_progress() const override {
        if (total_bytes_ == 0) return 0.0f;
        return static_cast<float>(played_bytes()) / static_cast<float>(total_bytes_);
    }

private:
    std::string timestamped_name() const {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char ts[32];
        std::strftime(ts, sizeof(ts), "%Y%m%d-%H%M%S", &tm);
        // add ms to avoid collisions within the same second
        return std::string("rec-") + ts + "-" +
               std::to_string(SDL_GetTicks() % 1000) + ".wav";
    }

    Uint32 queued() const {
        return play_dev_ ? SDL_GetQueuedAudioSize(play_dev_) : 0;
    }
    Uint64 played_bytes() const {
        Uint32 q = queued();
        return total_bytes_ > q ? (total_bytes_ - q) : total_bytes_;
    }

    static void capture_cb(void* userdata, Uint8* stream, int len) {
        auto* self = static_cast<SdlAudio*>(userdata);
        const auto* in = reinterpret_cast<const std::int16_t*>(stream);
        const std::size_t n = static_cast<std::size_t>(len) / sizeof(std::int16_t);
        std::lock_guard<std::mutex> lk(self->mu_);
        self->samples_.insert(self->samples_.end(), in, in + n);
    }

    static void write_wav(const std::string& path,
                          const std::vector<std::int16_t>& s, int freq,
                          int channels) {
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) return;
        const std::uint32_t data_bytes = static_cast<std::uint32_t>(s.size() * 2);
        const std::uint32_t byte_rate = static_cast<std::uint32_t>(freq * channels * 2);
        auto u32 = [&](std::uint32_t v) { std::fwrite(&v, 4, 1, f); };
        auto u16 = [&](std::uint16_t v) { std::fwrite(&v, 2, 1, f); };
        std::fwrite("RIFF", 1, 4, f); u32(36 + data_bytes); std::fwrite("WAVE", 1, 4, f);
        std::fwrite("fmt ", 1, 4, f); u32(16); u16(1);
        u16(static_cast<std::uint16_t>(channels));
        u32(static_cast<std::uint32_t>(freq)); u32(byte_rate);
        u16(static_cast<std::uint16_t>(channels * 2)); u16(16);
        std::fwrite("data", 1, 4, f); u32(data_bytes);
        if (!s.empty()) std::fwrite(s.data(), 1, data_bytes, f);
        std::fclose(f);
    }

    std::string dir_;
    SDL_AudioDeviceID cap_dev_{0};
    SDL_AudioDeviceID play_dev_{0};
    SDL_AudioSpec play_spec_{};
    std::vector<std::int16_t> samples_;
    std::mutex mu_;
    bool capturing_{false};
    bool paused_{false};
    int have_freq_{16000};
    Uint32 start_ticks_{0};
    Uint64 total_bytes_{0};
    int bytes_per_sec_{0};
};

} // namespace sapiens::sim
