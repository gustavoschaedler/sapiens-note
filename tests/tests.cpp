// Minimal dependency-free test harness for the SapiensNote core.
#include <cstdio>
#include <string>

#include "sapiens/AppController.h"
#include "sapiens/AudioService.h"
#include "sapiens/Id.h"
#include "sapiens/NoteRepository.h"
#include "sapiens/NoteService.h"
#include "sapiens/host/Host.h"

using namespace sapiens;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(cond)) {                                                    \
            ++g_failures;                                                 \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,  \
                         #cond);                                          \
        }                                                                 \
    } while (0)

namespace {

struct Fixture {
    host::ManualClock clock;
    host::ConsoleLog log{false};
    IdGenerator ids{clock, 42}; // fixed seed -> deterministic
    InMemoryNoteRepository repo;
    NoteService notes{repo, ids, clock, log};
};

void test_id_ordering_and_uniqueness() {
    host::ManualClock clock;
    IdGenerator ids{clock, 7};
    std::string a = ids.next();
    std::string b = ids.next(); // same ms -> counter increments, still ordered
    clock.advance(5);
    std::string c = ids.next();
    CHECK(a.size() == 32);
    CHECK(a != b);
    CHECK(a < b); // monotonic within a millisecond
    CHECK(b < c); // monotonic across time
}

void test_create_and_list() {
    Fixture f;
    CHECK(f.notes.list_notes().empty());
    f.clock.advance(1);
    Note n1 = f.notes.create_note("first");
    f.clock.advance(1);
    Note n2 = f.notes.create_note("second");
    auto all = f.notes.list_notes();
    CHECK(all.size() == 2);
    // newest first
    CHECK(all[0].id == n2.id);
    CHECK(all[1].id == n1.id);
    CHECK(all[0].status == NoteStatus::PendingUpload);
    CHECK(all[0].version == 1);
}

void test_mutations_bump_version() {
    Fixture f;
    Note n = f.notes.create_note("x");
    CHECK(n.version == 1);
    f.clock.advance(10);
    CHECK(f.notes.rename(n.id, "renamed"));
    auto got = f.notes.get_note(n.id);
    CHECK(got.has_value());
    CHECK(got->title == "renamed");
    CHECK(got->version == 2);
    CHECK(got->updated_at > n.updated_at);

    CHECK(f.notes.set_favorite(n.id, true));
    CHECK(f.notes.get_note(n.id)->favorite);
    CHECK(f.notes.get_note(n.id)->version == 3);
}

void test_tags() {
    Fixture f;
    Note n = f.notes.create_note("tagme");
    Tag t = f.notes.create_tag("ideas", "#000");
    CHECK(f.notes.add_tag(n.id, t.id));
    CHECK(f.notes.get_note(n.id)->tag_ids.size() == 1);
    // idempotent add
    CHECK(f.notes.add_tag(n.id, t.id));
    CHECK(f.notes.get_note(n.id)->tag_ids.size() == 1);

    NoteFilter byTag;
    byTag.tag_id = t.id;
    CHECK(f.notes.list_notes(byTag).size() == 1);

    CHECK(f.notes.remove_tag(n.id, t.id));
    CHECK(f.notes.get_note(n.id)->tag_ids.empty());
    CHECK(f.notes.list_notes(byTag).empty());
}

void test_soft_delete_hidden_from_list() {
    Fixture f;
    Note n = f.notes.create_note("bye");
    CHECK(f.notes.soft_delete(n.id));
    CHECK(f.notes.list_notes().empty());
    NoteFilter withDeleted;
    withDeleted.include_deleted = true;
    CHECK(f.notes.list_notes(withDeleted).size() == 1);
    CHECK(f.notes.get_note(n.id)->is_deleted());
}

void test_apply_transcription() {
    Fixture f;
    Note n = f.notes.create_note("pending");
    CHECK(n.status == NoteStatus::PendingUpload);
    CHECK(f.notes.apply_transcription(n.id, "hello world", "greeting",
                                      {"point a", "point b"}, "en"));
    auto got = f.notes.get_note(n.id);
    CHECK(got->status == NoteStatus::Transcribed);
    CHECK(got->transcript == "hello world");
    CHECK(got->summary == "greeting");
    CHECK(got->key_points.size() == 2);
    CHECK(got->language == "en");
}

void test_audio_service_and_app_controller() {
    Fixture f;
    host::FakeAudio audio{f.clock};
    AudioService audioSvc{audio, f.notes, f.ids, f.log};
    AppController app{f.notes, audioSvc, f.clock, f.log};

    // Boot shows the splash screen; it auto-advances to Home after 4s.
    CHECK(app.screen() == Screen::Splash);
    f.clock.advance(3000);
    CHECK(!app.maybe_advance_splash());      // not enough time elapsed yet
    f.clock.advance(1000);
    CHECK(app.maybe_advance_splash());        // 4s elapsed -> advances
    CHECK(app.screen() == Screen::Home);

    // Home -> Notes.
    app.go_note_list();
    CHECK(app.screen() == Screen::NoteList);

    // Back from the list returns to Home.
    app.back();
    CHECK(app.screen() == Screen::Home);
    app.go_note_list();

    // Press record -> Recording screen.
    app.handle(InputEvent{InputKind::ButtonRecord, 0, 0});
    CHECK(app.screen() == Screen::Recording);
    CHECK(audioSvc.is_recording());
    f.clock.advance(3000); // 3 seconds

    // Press record again -> stops, creates a note, back to list.
    app.handle(InputEvent{InputKind::ButtonRecord, 0, 0});
    CHECK(app.screen() == Screen::NoteList);
    CHECK(!audioSvc.is_recording());
    CHECK(app.visible_notes().size() == 1);
    CHECK(audioSvc.last_clip().has_value());
    CHECK(audioSvc.last_clip()->duration_secs == 3);
    CHECK(audioSvc.last_clip()->note_id == app.visible_notes()[0].id);
}

} // namespace

int main() {
    test_id_ordering_and_uniqueness();
    test_create_and_list();
    test_mutations_bump_version();
    test_tags();
    test_soft_delete_hidden_from_list();
    test_apply_transcription();
    test_audio_service_and_app_controller();

    if (g_failures == 0) {
        std::printf("OK: all %d checks passed\n", g_checks);
        return 0;
    }
    std::fprintf(stderr, "FAILED: %d/%d checks failed\n", g_failures, g_checks);
    return 1;
}
