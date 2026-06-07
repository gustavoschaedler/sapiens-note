#pragma once
// Shared formatting helpers: date-time name formats (selectable in Settings) and
// recording-duration strings. Used by NoteService (titles) and the renderers.
#include <cstdio>
#include <ctime>
#include <string>

#include "sapiens/Types.h"

namespace sapiens::fmt {

// The 5 selectable name formats. `example`/strftime pattern pairs.
inline constexpr int kNameFormatCount = 5;

inline const char* name_format_pattern(int i) {
    switch (((i % kNameFormatCount) + kNameFormatCount) % kNameFormatCount) {
        case 0: return "%Y-%m-%d %H:%M";   // 2026-06-05 21:10
        case 1: return "%d/%m/%Y %H:%M";   // 05/06/2026 21:10
        case 2: return "%b %d, %H:%M";     // Jun 05, 21:10
        case 3: return "%Y-%m-%d %H:%M:%S"; // 2026-06-05 21:10:08
        default: return "%H:%M - %d %b %Y"; // 21:10 - 05 Jun 2026
    }
}

inline std::string format_timestamp(Millis ms, int fmt) {
    std::time_t t = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[48];
    std::strftime(buf, sizeof(buf), name_format_pattern(fmt), &tm);
    return buf;
}

// "0:08", "1:23"
inline std::string format_duration(int secs) {
    if (secs < 0) secs = 0;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", secs / 60, secs % 60);
    return buf;
}

} // namespace sapiens::fmt
