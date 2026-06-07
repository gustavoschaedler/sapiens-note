// Sync tests: two devices + an in-process fake server. Proves push/pull, LWW
// conflict resolution, and tombstone propagation per docs/SYNC_PROTOCOL.md.
#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_map>

#include "sapiens/Id.h"
#include "sapiens/NoteRepository.h"
#include "sapiens/NoteService.h"
#include "sapiens/Sync.h"
#include "sapiens/host/Host.h"

using namespace sapiens;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(c)) { ++g_fail; std::fprintf(stderr, "FAIL %s:%d %s\n",     \
                    __FILE__, __LINE__, #c); }                            \
    } while (0)

// Minimal authoritative server: applies pushed changes with LWW, returns deltas.
class FakeServer : public ISyncTransport {
public:
    SyncResponse sync(const SyncRequest& req) override {
        for (const auto& c : req.changes) {
            auto it = store_.find(c.entity_id);
            if (it == store_.end() ||
                lww_wins(c.note, c.device_id, it->second, origin_[c.entity_id])) {
                store_[c.entity_id] = c.note;
                origin_[c.entity_id] = c.device_id;
                max_updated_ = std::max(max_updated_, c.note.updated_at);
            }
        }
        SyncResponse r;
        r.has_more = false;
        for (const auto& [id, n] : store_) {
            if (n.updated_at > req.cursor) {
                SyncChange c;
                c.entity_id = id;
                c.operation = n.is_deleted() ? "delete" : "update";
                c.version = n.version;
                c.device_id = origin_[id];
                c.note = n;
                r.changes.push_back(c);
            }
        }
        r.next_cursor = std::max(max_updated_, req.cursor);
        return r;
    }

private:
    std::unordered_map<std::string, Note> store_;
    std::unordered_map<std::string, std::string> origin_;
    Millis max_updated_{0};
};

int main() {
    host::ManualClock clock;          // shared clock -> comparable timestamps
    host::ConsoleLog log{false};
    IdGenerator ids{clock, 5};

    InMemoryNoteRepository repoA, repoB;
    NoteService notesA{repoA, ids, clock, log};
    NoteService notesB{repoB, ids, clock, log};
    SyncClient syncA{repoA, "deviceA"};
    SyncClient syncB{repoB, "deviceB"};
    FakeServer server;

    // 1) Create on A, propagate to B.
    clock.advance(1);
    Note n = notesA.create_note("groceries");
    syncA.sync(server);
    int appliedB = syncB.sync(server);
    CHECK(appliedB == 1);
    CHECK(repoB.get(n.id).has_value());
    CHECK(repoB.get(n.id)->title == "groceries");

    // 2) Concurrent edit -> last writer (newer updated_at) wins on both devices.
    clock.advance(1);
    notesA.rename(n.id, "groceries-A"); // v2 @ t2
    clock.advance(1);
    notesB.set_favorite(n.id, true);     // v2 @ t3 (later)
    syncA.sync(server);
    syncB.sync(server);
    syncA.sync(server); // pull B's winning version back to A
    auto a = repoA.get(n.id);
    auto b = repoB.get(n.id);
    CHECK(a && b);
    CHECK(a->version == b->version);
    CHECK(a->favorite == true);          // B's later edit won
    CHECK(a->title == "groceries");       // B's record had original title
    CHECK(a->title == b->title);

    // 3) Delete on A -> tombstone reaches B; hidden from B's list.
    clock.advance(1);
    notesA.soft_delete(n.id);
    syncA.sync(server);
    int del = syncB.sync(server);
    CHECK(del == 1);
    CHECK(repoB.get(n.id)->is_deleted());
    CHECK(notesB.list_notes().empty());

    if (g_fail == 0) { std::printf("OK: sync %d checks\n", g_checks); return 0; }
    std::fprintf(stderr, "FAILED %d/%d\n", g_fail, g_checks);
    return 1;
}
