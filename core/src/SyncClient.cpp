#include "sapiens/Sync.h"

namespace sapiens {

bool lww_wins(const Note& incoming, const std::string& incoming_dev,
              const Note& local, const std::string& local_dev) {
    if (incoming.version != local.version)
        return incoming.version > local.version;
    if (incoming.updated_at != local.updated_at)
        return incoming.updated_at > local.updated_at;
    return incoming_dev > local_dev; // deterministic tiebreak
}

SyncClient::SyncClient(NoteRepository& repo, std::string device_id)
    : repo_(repo), device_id_(std::move(device_id)) {}

std::vector<SyncChange> SyncClient::collect_local_changes() const {
    NoteFilter f;
    f.include_deleted = true;       // tombstones must propagate
    f.updated_after = cursor_;      // delta rule
    std::vector<SyncChange> out;
    for (const auto& n : repo_.list(f)) {
        SyncChange c;
        c.entity_type = "note";
        c.entity_id = n.id;
        c.operation = n.is_deleted() ? "delete"
                      : (n.version == 1 ? "create" : "update");
        c.version = n.version;
        c.device_id = device_id_;
        c.note = n;
        out.push_back(c);
    }
    return out;
}

int SyncClient::apply_remote(const std::vector<SyncChange>& changes) {
    int applied = 0;
    for (const auto& c : changes) {
        auto local = repo_.get(c.entity_id);
        if (!local || lww_wins(c.note, c.device_id, *local, device_id_)) {
            repo_.upsert(c.note);
            ++applied;
        }
    }
    return applied;
}

int SyncClient::sync(ISyncTransport& transport) {
    int applied = 0;
    bool first = true;
    while (true) {
        SyncRequest req;
        req.device_id = device_id_;
        req.cursor = cursor_;
        if (first) req.changes = collect_local_changes(); // push once per sync()
        SyncResponse resp = transport.sync(req);
        applied += apply_remote(resp.changes);
        cursor_ = resp.next_cursor;
        first = false;
        if (!resp.has_more) break;
    }
    return applied;
}

} // namespace sapiens
