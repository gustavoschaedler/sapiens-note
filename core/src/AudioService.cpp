#include "sapiens/AudioService.h"

namespace sapiens {

AudioService::AudioService(IAudio& audio, NoteService& notes, IdGenerator& ids,
                           ILog& log)
    : audio_(audio), notes_(notes), ids_(ids), log_(log) {}

void AudioService::start() {
    if (audio_.is_capturing()) return;
    log_.info("recording started");
    audio_.start_capture();
}

bool AudioService::is_recording() const { return audio_.is_capturing(); }

std::optional<Note> AudioService::stop_and_create(const std::string& title) {
    if (!audio_.is_capturing()) {
        log_.warn("stop_and_create called while not recording");
        return std::nullopt;
    }
    const RecordingResult r = audio_.stop_capture();
    log_.info("recording stopped (" + std::to_string(r.duration_secs) + "s)");

    // Empty title -> NoteService assigns a date-time name.
    Note note = notes_.create_note(title);

    AudioClip clip;
    clip.id = ids_.next();
    clip.note_id = note.id;
    clip.storage_key = r.storage_key;
    clip.duration_secs = r.duration_secs;
    clip.sample_rate = r.sample_rate;
    clip.channels = r.channels;
    clip.size_bytes = r.size_bytes;
    last_clip_ = clip;

    // Link the recording (key + duration) to the note for playback + display.
    notes_.set_audio(note.id, clip.storage_key, clip.duration_secs);
    return notes_.get_note(note.id).value_or(note);
}

void AudioService::play(const std::string& storage_key) {
    if (storage_key.empty()) {
        log_.warn("play: no audio for this note");
        return;
    }
    log_.info("playing " + storage_key);
    audio_.play(storage_key);
}

} // namespace sapiens
