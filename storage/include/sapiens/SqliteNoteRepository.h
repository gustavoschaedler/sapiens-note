#pragma once
// SQLite-backed NoteRepository. Local persistence for the clients (and a model for
// the server schema). Mirrors docs/DATABASE.md (single-user). Depends on sqlite3.

#include <string>

#include "sapiens/NoteRepository.h"

struct sqlite3; // fwd

namespace sapiens {

class SqliteNoteRepository final : public NoteRepository {
public:
    // path may be a file path or ":memory:". Throws std::runtime_error on failure.
    explicit SqliteNoteRepository(const std::string& path);
    ~SqliteNoteRepository() override;

    SqliteNoteRepository(const SqliteNoteRepository&) = delete;
    SqliteNoteRepository& operator=(const SqliteNoteRepository&) = delete;

    void upsert(const Note& note) override;
    std::optional<Note> get(const std::string& id) const override;
    std::vector<Note> list(const NoteFilter& filter) const override;

    void upsert_tag(const Tag& tag) override;
    std::vector<Tag> tags() const override;

private:
    void exec(const char* sql);
    void load_tag_ids(Note& n) const;

    sqlite3* db_{nullptr};
};

} // namespace sapiens
