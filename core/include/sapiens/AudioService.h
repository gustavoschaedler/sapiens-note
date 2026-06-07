#pragma once
// Orchestrates capture: start/stop recording via IAudio, then create the note and
// its audio clip. The clip is uploaded later by the sync layer (offline-first).

#include <optional>
#include <string>

#include "sapiens/Hal.h"
#include "sapiens/Id.h"
#include "sapiens/NoteService.h"
#include "sapiens/Types.h"

namespace sapiens {

class AudioService {
public:
    AudioService(IAudio& audio, NoteService& notes, IdGenerator& ids, ILog& log);

    void start();              // begin capture
    bool is_recording() const; // mirrors IAudio::is_capturing

    // Stop capture, create a Note (PendingUpload) + its AudioClip, return the Note.
    std::optional<Note> stop_and_create(const std::string& title = "");

    // Playback controls (delegate to the audio HAL).
    void play(const std::string& storage_key);
    void pause_resume() { audio_.pause_resume_playback(); }
    void stop() { audio_.stop_playback(); }
    bool is_playing() const { return audio_.is_playing(); }
    bool is_paused() const { return audio_.is_paused(); }
    int pos_secs() const { return audio_.playback_pos_secs(); }
    int total_secs() const { return audio_.playback_total_secs(); }
    float progress() const { return audio_.playback_progress(); }

    const std::optional<AudioClip>& last_clip() const { return last_clip_; }

private:
    IAudio& audio_;
    NoteService& notes_;
    IdGenerator& ids_;
    ILog& log_;
    std::optional<AudioClip> last_clip_;
};

} // namespace sapiens
