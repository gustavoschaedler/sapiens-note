#pragma once
// Core domain types for SapiensNote. Mirrors docs/DOMAIN_MODEL.md (single-user model).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sapiens {

using Millis = std::int64_t; // Unix epoch milliseconds (UTC)

// Lifecycle of a note's AI processing (see docs/DATABASE.md notes.status).
enum class NoteStatus {
    PendingUpload, // captured locally, audio not yet uploaded
    Uploaded,      // audio on server, queued for processing
    Transcribing,  // AI pipeline running
    Transcribed,   // transcript + summary available
    Failed,        // processing failed; user may retry
};

const char* to_string(NoteStatus s);

// A user label for organizing notes (unique per user by name).
struct Tag {
    std::string id;
    std::string name;
    std::string color; // optional hex, e.g. "#000000"
};

// Recorded audio linked to a note (16 kHz mono). Bytes live outside the DB;
// here we keep a reference + metadata.
struct AudioClip {
    std::string id;
    std::string note_id;
    std::string storage_key; // path / object key
    int duration_secs{0};
    int sample_rate{16000};
    int channels{1};
    std::int64_t size_bytes{0};
};

// The primary entity.
struct Note {
    std::string id;
    std::string title;
    std::string transcript; // filled by the AI pipeline (server)
    std::string summary;     // filled by the AI pipeline (server)
    std::vector<std::string> key_points;
    std::string language; // detected, e.g. "pt", "en"
    std::string audio_key; // storage key/path of the recording (empty if none)
    int audio_secs{0};     // recording duration in seconds (0 if no audio)
    NoteStatus status{NoteStatus::PendingUpload};
    bool favorite{false};
    std::vector<std::string> tag_ids;
    std::int64_t version{1};
    Millis created_at{0};
    Millis updated_at{0};
    std::optional<Millis> deleted_at; // soft delete (tombstone)

    bool is_deleted() const { return deleted_at.has_value(); }
};

// Filter for listing notes.
struct NoteFilter {
    std::optional<std::string> tag_id;
    std::optional<bool> favorite;
    std::optional<Millis> updated_after;
    bool include_deleted{false};
};

} // namespace sapiens
