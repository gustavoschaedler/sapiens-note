#pragma once
// Portable host HAL implementations shared by the CLI, the simulator, and tests.
// (Firmware provides its own ESP-IDF implementations instead.)

#include <chrono>
#include <cstdio>
#include <string>

#include "sapiens/Hal.h"

namespace sapiens::host {

// Real wall/monotonic clock.
class SystemClock final : public IClock {
public:
    Millis now_ms() const override {
        using namespace std::chrono;
        return duration_cast<milliseconds>(
                   system_clock::now().time_since_epoch())
            .count();
    }
    std::int64_t monotonic_ms() const override {
        using namespace std::chrono;
        return duration_cast<milliseconds>(
                   steady_clock::now().time_since_epoch())
            .count();
    }
};

// Deterministic clock for tests: time only advances when you tell it to.
class ManualClock final : public IClock {
public:
    explicit ManualClock(Millis start = 1'700'000'000'000) : t_(start) {}
    Millis now_ms() const override { return t_; }
    std::int64_t monotonic_ms() const override { return t_; }
    void advance(Millis ms) { t_ += ms; }

private:
    Millis t_;
};

// Logs to stderr; can be silenced.
class ConsoleLog final : public ILog {
public:
    explicit ConsoleLog(bool enabled = true) : enabled_(enabled) {}
    void log(LogLevel level, const std::string& msg) override {
        if (!enabled_) return;
        const char* tag = "INFO";
        switch (level) {
            case LogLevel::Debug: tag = "DBG"; break;
            case LogLevel::Info: tag = "INF"; break;
            case LogLevel::Warn: tag = "WRN"; break;
            case LogLevel::Error: tag = "ERR"; break;
        }
        std::fprintf(stderr, "[%s] %s\n", tag, msg.c_str());
    }

private:
    bool enabled_;
};

// Stand-in audio backend: pretends to record, returns plausible metadata.
// Replaced by SDL audio (simulator) / ES8311 (firmware) later.
class FakeAudio final : public IAudio {
public:
    explicit FakeAudio(IClock& clock) : clock_(clock) {}

    void start_capture() override {
        capturing_ = true;
        started_ms_ = clock_.monotonic_ms();
    }
    RecordingResult stop_capture() override {
        capturing_ = false;
        RecordingResult r;
        int secs = static_cast<int>((clock_.monotonic_ms() - started_ms_) / 1000);
        r.duration_secs = secs < 1 ? 1 : secs;
        r.size_bytes = static_cast<std::int64_t>(r.duration_secs) * 16000 * 2;
        r.storage_key = "audio/fake-" + std::to_string(started_ms_) + ".flac";
        return r;
    }
    bool is_capturing() const override { return capturing_; }
    void play(const std::string&) override {}

private:
    IClock& clock_;
    bool capturing_{false};
    std::int64_t started_ms_{0};
};

} // namespace sapiens::host
