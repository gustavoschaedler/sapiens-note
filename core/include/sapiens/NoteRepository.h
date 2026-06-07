#pragma once
// Persistence boundary for notes/tags. The in-memory implementation here backs
// tests, the CLI, and the first simulator build; a SQLite-backed implementation
// (over IStorage) replaces it later without changing callers.

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "sapiens/Types.h"

namespace sapiens {

class NoteRepository {
public:
    virtual ~NoteRepository() = default;
    virtual void upsert(const Note& note) = 0;
    virtual std::optional<Note> get(const std::string& id) const = 0;
    virtual std::vector<Note> list(const NoteFilter& filter) const = 0;

    virtual void upsert_tag(const Tag& tag) = 0;
    virtual std::vector<Tag> tags() const = 0;
};

class InMemoryNoteRepository final : public NoteRepository {
public:
    void upsert(const Note& note) override { notes_[note.id] = note; }

    std::optional<Note> get(const std::string& id) const override {
        auto it = notes_.find(id);
        if (it == notes_.end()) return std::nullopt;
        return it->second;
    }

    std::vector<Note> list(const NoteFilter& f) const override {
        std::vector<Note> out;
        for (const auto& [id, n] : notes_) {
            if (n.is_deleted() && !f.include_deleted) continue;
            if (f.favorite && n.favorite != *f.favorite) continue;
            if (f.updated_after && n.updated_at <= *f.updated_after) continue;
            if (f.tag_id) {
                const auto& t = n.tag_ids;
                if (std::find(t.begin(), t.end(), *f.tag_id) == t.end()) continue;
            }
            out.push_back(n);
        }
        // Newest first (notes list / home screen ordering).
        std::sort(out.begin(), out.end(), [](const Note& a, const Note& b) {
            return a.created_at > b.created_at;
        });
        return out;
    }

    void upsert_tag(const Tag& tag) override { tags_[tag.id] = tag; }

    std::vector<Tag> tags() const override {
        std::vector<Tag> out;
        out.reserve(tags_.size());
        for (const auto& [id, t] : tags_) out.push_back(t);
        std::sort(out.begin(), out.end(),
                  [](const Tag& a, const Tag& b) { return a.name < b.name; });
        return out;
    }

private:
    std::unordered_map<std::string, Note> notes_;
    std::unordered_map<std::string, Tag> tags_;
};

} // namespace sapiens
