#pragma once
// Application service for notes. All note mutations go through here so versioning,
// timestamps, and tag handling stay consistent (see docs/SHARED_CORE.md).

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "sapiens/Hal.h"
#include "sapiens/Id.h"
#include "sapiens/NoteRepository.h"
#include "sapiens/Types.h"

namespace sapiens {

class NoteService {
public:
    NoteService(NoteRepository& repo, IdGenerator& ids, IClock& clock, ILog& log);

    // Create a note locally (offline-first). Starts in PendingUpload.
    Note create_note(const std::string& title);

    std::vector<Note> list_notes(const NoteFilter& filter = {}) const;
    std::optional<Note> get_note(const std::string& id) const;

    bool rename(const std::string& id, const std::string& title);
    bool set_favorite(const std::string& id, bool favorite);
    bool set_audio(const std::string& id, const std::string& storage_key, int secs);
    bool soft_delete(const std::string& id);

    // Date-time name format (index into sapiens::fmt). Affects new untitled notes.
    void set_name_format(int fmt) { name_format_ = fmt; }
    int name_format() const { return name_format_; }

    // Tag management.
    Tag create_tag(const std::string& name, const std::string& color = "");
    std::vector<Tag> list_tags() const;
    bool add_tag(const std::string& note_id, const std::string& tag_id);
    bool remove_tag(const std::string& note_id, const std::string& tag_id);

    // Applied when the server's AI pipeline returns results (via sync).
    bool apply_transcription(const std::string& id, const std::string& transcript,
                             const std::string& summary,
                             const std::vector<std::string>& key_points,
                             const std::string& language);
    bool set_status(const std::string& id, NoteStatus status);

private:
    // Bump version + updated_at and persist. Returns false if note missing.
    bool mutate(const std::string& id, const std::function<void(Note&)>& fn);

    NoteRepository& repo_;
    IdGenerator& ids_;
    IClock& clock_;
    ILog& log_;
    int name_format_{0};
};

} // namespace sapiens
