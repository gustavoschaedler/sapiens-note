#include "sapiens/NoteService.h"

#include <algorithm>

#include "sapiens/Format.h"

namespace sapiens {

const char* to_string(NoteStatus s) {
    switch (s) {
        case NoteStatus::PendingUpload: return "pending_upload";
        case NoteStatus::Uploaded: return "uploaded";
        case NoteStatus::Transcribing: return "transcribing";
        case NoteStatus::Transcribed: return "transcribed";
        case NoteStatus::Failed: return "failed";
    }
    return "unknown";
}

NoteService::NoteService(NoteRepository& repo, IdGenerator& ids, IClock& clock,
                         ILog& log)
    : repo_(repo), ids_(ids), clock_(clock), log_(log) {}

Note NoteService::create_note(const std::string& title) {
    Note n;
    n.id = ids_.next();
    n.status = NoteStatus::PendingUpload;
    n.version = 1;
    n.created_at = clock_.now_ms();
    n.updated_at = n.created_at;
    // Untitled notes (e.g. recordings) get a date-time name (chosen format) so
    // they're identifiable in the list.
    n.title = title.empty() ? fmt::format_timestamp(n.created_at, name_format_)
                            : title;
    repo_.upsert(n);
    log_.info("note created: " + n.id);
    return n;
}

std::vector<Note> NoteService::list_notes(const NoteFilter& filter) const {
    return repo_.list(filter);
}

std::optional<Note> NoteService::get_note(const std::string& id) const {
    return repo_.get(id);
}

bool NoteService::mutate(const std::string& id,
                         const std::function<void(Note&)>& fn) {
    auto cur = repo_.get(id);
    if (!cur) {
        log_.warn("mutate: note not found: " + id);
        return false;
    }
    Note n = *cur;
    fn(n);
    n.version += 1;
    n.updated_at = clock_.now_ms();
    repo_.upsert(n);
    return true;
}

bool NoteService::rename(const std::string& id, const std::string& title) {
    return mutate(id, [&](Note& n) { n.title = title; });
}

bool NoteService::set_favorite(const std::string& id, bool favorite) {
    return mutate(id, [&](Note& n) { n.favorite = favorite; });
}

bool NoteService::set_audio(const std::string& id, const std::string& key,
                            int secs) {
    return mutate(id, [&](Note& n) { n.audio_key = key; n.audio_secs = secs; });
}

bool NoteService::soft_delete(const std::string& id) {
    return mutate(id, [&](Note& n) { n.deleted_at = clock_.now_ms(); });
}

Tag NoteService::create_tag(const std::string& name, const std::string& color) {
    Tag t;
    t.id = ids_.next();
    t.name = name;
    t.color = color;
    repo_.upsert_tag(t);
    return t;
}

std::vector<Tag> NoteService::list_tags() const { return repo_.tags(); }

bool NoteService::add_tag(const std::string& note_id, const std::string& tag_id) {
    return mutate(note_id, [&](Note& n) {
        auto& t = n.tag_ids;
        if (std::find(t.begin(), t.end(), tag_id) == t.end()) t.push_back(tag_id);
    });
}

bool NoteService::remove_tag(const std::string& note_id,
                             const std::string& tag_id) {
    return mutate(note_id, [&](Note& n) {
        auto& t = n.tag_ids;
        t.erase(std::remove(t.begin(), t.end(), tag_id), t.end());
    });
}

bool NoteService::apply_transcription(const std::string& id,
                                      const std::string& transcript,
                                      const std::string& summary,
                                      const std::vector<std::string>& key_points,
                                      const std::string& language) {
    return mutate(id, [&](Note& n) {
        n.transcript = transcript;
        n.summary = summary;
        n.key_points = key_points;
        n.language = language;
        n.status = NoteStatus::Transcribed;
    });
}

bool NoteService::set_status(const std::string& id, NoteStatus status) {
    return mutate(id, [&](Note& n) { n.status = status; });
}

} // namespace sapiens
