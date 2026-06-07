// Branch coverage for AppController: navigation, pagination, settings, the
// delete flow, the player, the transcript scroll clock, and the splash.
#include <cstdio>
#include <cstring>
#include <string>

#include "sapiens/AppController.h"
#include "sapiens/AudioService.h"
#include "sapiens/Id.h"
#include "sapiens/NoteRepository.h"
#include "sapiens/NoteService.h"
#include "sapiens/host/Host.h"

using namespace sapiens;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                \
    do {                                                                        \
        ++g_checks;                                                             \
        if (!(c)) { ++g_fail; std::fprintf(stderr, "FAIL %s:%d %s\n",           \
                    __FILE__, __LINE__, #c); }                                  \
    } while (0)

struct Rig {
    host::ManualClock clock;
    host::ConsoleLog log{false};
    host::FakeAudio audio{clock};
    IdGenerator ids{clock, 42};
    InMemoryNoteRepository repo;
    NoteService notes{repo, ids, clock, log};
    AudioService audioSvc{audio, notes, ids, log};
    AppController app{notes, audioSvc, clock, log};
    // record one real (fake) audio note, return its id, leave on NoteList
    std::string record() {
        app.start_recording();
        clock.advance(2000);
        app.stop_recording();
        return audioSvc.last_clip()->note_id;
    }
};

static void test_to_string() {
    Screen all[] = {Screen::Splash, Screen::Home, Screen::NoteList,
                    Screen::Recording, Screen::NoteDetail, Screen::Settings,
                    Screen::Player, Screen::ConfirmDelete, Screen::Sync,
                    Screen::Tags};
    for (Screen s : all) CHECK(std::strcmp(to_string(s), "unknown") != 0);
    NoteStatus st[] = {NoteStatus::PendingUpload, NoteStatus::Uploaded,
                       NoteStatus::Transcribing, NoteStatus::Transcribed,
                       NoteStatus::Failed};
    for (NoteStatus s : st) CHECK(std::strcmp(to_string(s), "unknown") != 0);
}

static void test_splash() {
    Rig r;
    CHECK(r.app.screen() == Screen::Splash);
    CHECK(r.app.splash_elapsed_ms() >= 0);
    CHECK(!r.app.maybe_advance_splash());
    r.clock.advance(r.app.splash_duration_ms());
    CHECK(r.app.maybe_advance_splash());
    CHECK(r.app.screen() == Screen::Home);
    CHECK(!r.app.maybe_advance_splash()); // no longer on splash
    r.app.debug_set_splash_elapsed(123);
    // debug setter only matters while on Splash:
    r.app.show_splash();
    r.app.debug_set_splash_elapsed(50);
    CHECK(r.app.splash_elapsed_ms() >= 50);
}

static void test_navigation() {
    Rig r;
    r.app.go_home();
    r.app.go_sync();   CHECK(r.app.screen() == Screen::Sync);
    r.app.back();      CHECK(r.app.screen() == Screen::Home);
    r.app.go_tags();   CHECK(r.app.screen() == Screen::Tags);
    r.app.back();      CHECK(r.app.screen() == Screen::Home);
    r.app.open_settings(); CHECK(r.app.screen() == Screen::Settings);
    r.app.back();      CHECK(r.app.screen() == Screen::Home);
    r.app.go_note_list(); CHECK(r.app.screen() == Screen::NoteList);
    r.app.back();      CHECK(r.app.screen() == Screen::Home); // list -> home

    // open unknown note: stays put
    r.app.go_note_list();
    r.app.open_note("does-not-exist");
    CHECK(r.app.screen() == Screen::NoteList);
    CHECK(!r.app.selected_note().has_value());

    // handle(): record toggles, Back routes through back()
    r.app.handle(InputEvent{InputKind::ButtonRecord});
    CHECK(r.app.screen() == Screen::Recording);
    r.app.handle(InputEvent{InputKind::ButtonRecord}); // stop -> list
    CHECK(r.app.screen() == Screen::NoteList);
    r.app.handle(InputEvent{InputKind::SwipeLeft});
    CHECK(r.app.screen() == Screen::Home);
    r.app.handle(InputEvent{InputKind::Tap}); // default: no-op
}

static void test_pagination() {
    Rig r;
    CHECK(r.app.page_size() == 5);
    for (int i = 0; i < 7; ++i) { r.clock.advance(1); r.notes.create_note("n"); }
    r.app.refresh();
    CHECK(r.app.total_notes() == 7);
    CHECK(r.app.page_count() == 2);
    CHECK(r.app.list_page() == 0);
    CHECK(r.app.page_first_index() == 0);
    CHECK((int)r.app.page_notes().size() == 5);
    r.app.page_up(); CHECK(r.app.list_page() == 0); // clamp at 0
    r.app.page_down(); CHECK(r.app.list_page() == 1);
    CHECK(r.app.page_first_index() == 5);
    CHECK((int)r.app.page_notes().size() == 2);
    r.app.page_down(); CHECK(r.app.list_page() == 1); // clamp at last
    r.app.page_up(); CHECK(r.app.list_page() == 0);
}

static void test_settings() {
    Rig r;
    CHECK(r.app.scroll_speed_min() == 1 && r.app.scroll_speed_max() == 5);
    r.app.set_scroll_speed(99); CHECK(r.app.scroll_speed() == 5);
    r.app.set_scroll_speed(-3); CHECK(r.app.scroll_speed() == 1);
    r.app.set_scroll_speed(3);  CHECK(r.app.scroll_speed() == 3);

    CHECK(r.app.name_format_count() == 5);
    int start = r.app.name_format();
    r.app.cycle_name_format();
    CHECK(r.app.name_format() == (start + 1) % 5);
    r.app.set_name_format(2); CHECK(r.app.name_format() == 2);
    CHECK(!r.app.name_format_preview().empty());
    for (int i = 0; i < 5; ++i) CHECK(!r.app.name_format_example(i).empty());
}

static void test_delete_flow() {
    Rig r;
    std::string id = r.record();
    r.app.open_note(id);
    CHECK(r.app.screen() == Screen::NoteDetail);
    r.app.request_delete();
    CHECK(r.app.screen() == Screen::ConfirmDelete);
    r.app.cancel_delete();
    CHECK(r.app.screen() == Screen::NoteDetail);
    r.app.request_delete();
    r.app.confirm_delete();
    CHECK(r.app.screen() == Screen::NoteList);
    CHECK(r.app.total_notes() == 0); // soft-deleted, hidden
}

static void test_player() {
    Rig r;
    std::string id = r.record();
    r.app.open_note(id);
    CHECK(r.app.has_audio());
    r.app.open_player();
    CHECK(r.app.screen() == Screen::Player);
    r.app.toggle_play();   // pause/resume branch
    r.app.play_selected();
    r.app.stop_play();
    (void)r.app.audio_playing();
    (void)r.app.audio_paused();
    (void)r.app.audio_pos_secs();
    (void)r.app.audio_total_secs();
    (void)r.app.audio_progress();
    r.app.back();          // player -> detail (stops audio)
    CHECK(r.app.screen() == Screen::NoteDetail);
    r.app.back();          // detail -> list
    CHECK(r.app.screen() == Screen::NoteList);

    // a note without audio: has_audio false, open_player is a no-op
    r.clock.advance(1);
    Note typed = r.notes.create_note("typed");
    r.app.refresh();
    r.app.open_note(typed.id);
    CHECK(!r.app.has_audio());
    r.app.open_player();
    CHECK(r.app.screen() == Screen::NoteDetail); // didn't switch to Player
}

static void test_transcript_scroll() {
    Rig r;
    std::string id = r.record();
    r.app.open_note(id);                       // resets scroll
    CHECK(r.app.transcript_scroll_ms() == 0);
    CHECK(!r.app.transcript_paused());
    r.app.tick_transcript_scroll(100);
    CHECK(r.app.transcript_scroll_ms() == 100);
    r.app.toggle_transcript_scroll();          // pause
    CHECK(r.app.transcript_paused());
    r.app.tick_transcript_scroll(100);         // ignored while paused
    CHECK(r.app.transcript_scroll_ms() == 100);
    r.app.toggle_transcript_scroll();          // resume
    r.app.tick_transcript_scroll(50);
    CHECK(r.app.transcript_scroll_ms() == 150);
    r.app.open_note(id);                        // re-open resets
    CHECK(r.app.transcript_scroll_ms() == 0);
}

static void test_reload_selected_and_tags() {
    Rig r;
    std::string id = r.record();
    r.app.open_note(id);
    CHECK(r.app.selected_note()->transcript.empty());
    // server applies a transcript out-of-band; reload_selected picks it up
    r.notes.apply_transcription(id, "hello", "", {}, "en");
    r.app.reload_selected();
    CHECK(r.app.selected_note()->transcript == "hello");

    Tag t = r.notes.create_tag("ideas");
    (void)t;
    CHECK(r.app.tags().size() == 1);
}

int main() {
    test_to_string();
    test_splash();
    test_navigation();
    test_pagination();
    test_settings();
    test_delete_flow();
    test_player();
    test_transcript_scroll();
    test_reload_selected_and_tags();
    if (g_fail == 0) { std::printf("OK: app %d checks\n", g_checks); return 0; }
    std::fprintf(stderr, "FAILED %d/%d\n", g_fail, g_checks);
    return 1;
}
