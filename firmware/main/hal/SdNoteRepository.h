#pragma once
// NoteRepository backed by a JSON index file on the SD card. Keeps notes/tags in
// memory for fast access (same semantics as InMemoryNoteRepository) and persists
// the whole store to `path` on every mutation, loading it back at construction so
// the note list survives reboots. Audio bytes stay as separate WAVs on the card.
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "sapiens/NoteRepository.h"
#include "sapiens/Types.h"

namespace sapiens::fw {

class SdNoteRepository final : public sapiens::NoteRepository {
public:
    explicit SdNoteRepository(std::string path);

    void upsert(const sapiens::Note& note) override;
    std::optional<sapiens::Note> get(const std::string& id) const override;
    std::vector<sapiens::Note> list(const sapiens::NoteFilter& filter) const override;
    void upsert_tag(const sapiens::Tag& tag) override;
    std::vector<sapiens::Tag> tags() const override;

private:
    void load();
    void save() const;

    std::string path_;
    std::unordered_map<std::string, sapiens::Note> notes_;
    std::unordered_map<std::string, sapiens::Tag> tags_;
};

} // namespace sapiens::fw
