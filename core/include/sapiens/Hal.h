#pragma once
// Hardware Abstraction Layer interfaces. The shared core talks to the outside
// world only through these; firmware and simulator each provide implementations.
// See docs/SHARED_CORE.md.

#include <cstdint>
#include <string>

#include "sapiens/Types.h"

namespace sapiens {

// --- IClock -----------------------------------------------------------------
class IClock {
public:
    virtual ~IClock() = default;
    virtual Millis now_ms() const = 0;      // wall clock, Unix epoch ms (UTC)
    virtual std::int64_t monotonic_ms() const = 0; // monotonic, for durations
};

// --- ILog -------------------------------------------------------------------
enum class LogLevel { Debug, Info, Warn, Error };

class ILog {
public:
    virtual ~ILog() = default;
    virtual void log(LogLevel level, const std::string& msg) = 0;

    void debug(const std::string& m) { log(LogLevel::Debug, m); }
    void info(const std::string& m) { log(LogLevel::Info, m); }
    void warn(const std::string& m) { log(LogLevel::Warn, m); }
    void error(const std::string& m) { log(LogLevel::Error, m); }
};

// --- IAudio -----------------------------------------------------------------
// Result of a finished capture.
struct RecordingResult {
    std::string storage_key; // where the audio was written
    int duration_secs{0};
    int sample_rate{16000};
    int channels{1};
    std::int64_t size_bytes{0};
};

class IAudio {
public:
    virtual ~IAudio() = default;
    virtual void start_capture() = 0;          // begin recording (16 kHz mono)
    virtual RecordingResult stop_capture() = 0; // finalize and return metadata
    virtual bool is_capturing() const = 0;

    // --- playback (with controls) -------------------------------------------
    virtual void play(const std::string& storage_key) = 0; // start/restart
    virtual void pause_resume_playback() {}                 // toggle pause
    virtual void stop_playback() {}
    virtual bool is_playing() const { return false; }       // active (not paused)
    virtual bool is_paused() const { return false; }
    virtual int playback_pos_secs() const { return 0; }
    virtual int playback_total_secs() const { return 0; }
    virtual float playback_progress() const { return 0.0f; } // 0..1
};

// --- IDisplay (used by the UI layer / simulator) ----------------------------
struct DisplaySize { int width{200}; int height{200}; };
enum class RefreshHint { Partial, Full };

// A 200x200, 1-bit framebuffer is provided by the renderer; IDisplay flushes it.
class IDisplay {
public:
    virtual ~IDisplay() = default;
    virtual DisplaySize size() const = 0;
    // bits: row-major, 1 byte per pixel (0=white, 1=black) for simplicity.
    virtual void present(const std::uint8_t* pixels, RefreshHint hint) = 0;
};

// --- IInput -----------------------------------------------------------------
enum class InputKind { Tap, LongPress, SwipeUp, SwipeDown, SwipeLeft, Back,
                       ButtonRecord, ButtonPower, Quit,
                       // physical 2-button nav: cycle focus (next/prev) / activate
                       FocusNext, FocusPrev, FocusSelect };

struct InputEvent {
    InputKind kind{InputKind::Tap};
    int x{0};
    int y{0};
};

// --- IPower -----------------------------------------------------------------
class IPower {
public:
    virtual ~IPower() = default;
    virtual int battery_percent() const = 0; // 0..100, -1 if unknown
    virtual bool on_external_power() const = 0;
};

} // namespace sapiens
