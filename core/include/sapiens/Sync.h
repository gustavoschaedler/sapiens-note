#pragma once
// Offline-first delta sync client. Implements docs/SYNC_PROTOCOL.md: delta by
// updated_at cursor, last-writer-wins conflict resolution, soft-delete tombstones.
//
// The client talks to an abstract ISyncTransport so it can be tested against an
// in-process fake server now, and wired to an HTTP transport (over INetwork) once
// the Phase-2 server exists — without changing the client logic.

#include <string>
#include <vector>

#include "sapiens/NoteRepository.h"
#include "sapiens/Types.h"

namespace sapiens {

// One change to a note (v1 syncs notes; tags travel inside the note's tag_ids).
struct SyncChange {
    std::string entity_type{"note"};
    std::string entity_id;
    std::string operation; // "create" | "update" | "delete"
    std::int64_t version{0};
    std::string device_id; // origin device (for LWW tiebreak)
    Note note;             // full payload for note entities
};

struct SyncRequest {
    std::string device_id;
    Millis cursor{0};                 // last cursor this device saw (0 = first)
    std::vector<SyncChange> changes;  // local changes to push
};

struct SyncResponse {
    Millis next_cursor{0};
    bool has_more{false};
    std::vector<SyncChange> changes;  // server changes to apply locally
};

// Transport abstraction (HTTP later; fake server in tests).
class ISyncTransport {
public:
    virtual ~ISyncTransport() = default;
    virtual SyncResponse sync(const SyncRequest& req) = 0;
};

// Returns true if `incoming` should overwrite `local` under last-writer-wins:
// version, then updated_at, then device_id as a stable tiebreak. `incoming_dev`
// / `local_dev` break exact ties deterministically.
bool lww_wins(const Note& incoming, const std::string& incoming_dev,
              const Note& local, const std::string& local_dev);

class SyncClient {
public:
    SyncClient(NoteRepository& repo, std::string device_id);

    // Push local changes since the cursor and apply the server's delta.
    // Returns the number of remote changes applied. Pages until has_more clears.
    int sync(ISyncTransport& transport);

private:
    std::vector<SyncChange> collect_local_changes() const;
    int apply_remote(const std::vector<SyncChange>& changes);

    NoteRepository& repo_;
    std::string device_id_;
    Millis cursor_{0};
};

} // namespace sapiens
