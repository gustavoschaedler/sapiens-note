// Renders every screen onto an off-screen Canvas (no SDL) and checks hit-testing,
// so the simulator's Canvas + Renderer logic is exercised in CI.
#include <cstdio>
#include <string>

#include "Canvas.h"
#include "Renderer.h"
#include "sapiens/AppController.h"
#include "sapiens/AudioService.h"
#include "sapiens/Id.h"
#include "sapiens/NoteRepository.h"
#include "sapiens/NoteService.h"
#include "sapiens/host/Host.h"

using namespace sapiens;
using sapiens::sim::Canvas;
using sapiens::sim::Hit;
using sapiens::sim::Renderer;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                \
    do {                                                                        \
        ++g_checks;                                                             \
        if (!(c)) { ++g_fail; std::fprintf(stderr, "FAIL %s:%d %s\n",           \
                    __FILE__, __LINE__, #c); }                                  \
    } while (0)

static int ink(const Canvas& c) {
    int n = 0;
    for (int i = 0; i < Canvas::W * Canvas::H; ++i) n += c.data()[i] ? 1 : 0;
    return n;
}

static void test_canvas() {
    Canvas c;
    CHECK(Canvas::text_width("abc") > 0);
    CHECK(Canvas::line_height() == 16);
    CHECK(Canvas::text_width_big("X") > 0 && Canvas::line_height_big() > 0);
    CHECK(Canvas::text_width_med("X") > 0 && Canvas::line_height_med() > 0);
    CHECK(Canvas::text_width_huge("X") > 0 && Canvas::line_height_huge() > 0);
    std::string e = Canvas::elide("a very long string that will not fit", 40);
    CHECK(e.size() < 36 && e.rfind("...") != std::string::npos);

    c.clear(0);
    CHECK(ink(c) == 0);
    c.draw_text(2, 2, "hello", 1);
    CHECK(ink(c) > 0);

    // rounded fill + invert round-trips back to blank
    Canvas r;
    r.fill_round_rect(0, 0, 40, 20, 1, 5);
    int before = ink(r);
    CHECK(before > 0);
    r.invert_round_rect(0, 0, 40, 20, 5);
    CHECK(ink(r) < before); // most of the filled area flipped to white

    // vertical clip: text drawn fully outside the band leaves no ink
    Canvas v;
    v.draw_text_vclip(0, 0, "clip", 1, 100, 120); // glyph rows 0..15, band 100..120
    CHECK(ink(v) == 0);
    Canvas v2;
    v2.draw_text_vclip(0, 100, "clip", 1, 100, 120); // now inside the band
    CHECK(ink(v2) > 0);
}

struct Rig {
    host::ManualClock clock;
    host::ConsoleLog log{false};
    host::FakeAudio audio{clock};
    IdGenerator ids{clock, 7};
    InMemoryNoteRepository repo;
    NoteService notes{repo, ids, clock, log};
    AudioService audioSvc{audio, notes, ids, log};
    AppController app{notes, audioSvc, clock, log};
    std::string record() {
        app.start_recording();
        clock.advance(2000);
        app.stop_recording();
        return audioSvc.last_clip()->note_id;
    }
};

// render the current screen and assert it drew something (splash mid-animation,
// not a blank blink frame)
static void renders(Renderer& r, Rig& rig, std::uint32_t now = 1500) {
    Canvas c;
    r.render(c, rig.app, 87, now);
    CHECK(ink(c) > 0);
}

static void test_screens_render() {
    Renderer r;
    Rig rig;
    for (int i = 0; i < 7; ++i) { rig.clock.advance(1); rig.notes.create_note("note " + std::to_string(i)); }
    std::string rec = rig.record();
    rig.notes.apply_transcription(
        rec, "This is a long transcript used to exercise the vertical scroll, "
             "word wrapping, and clipping inside the detail box. " +
             std::string(300, 'x'),
        "", {}, "en");
    rig.app.refresh();

    rig.app.show_splash();
    rig.app.debug_set_splash_elapsed(600); // mid type-out -> visible
    renders(r, rig);

    rig.app.go_home();        renders(r, rig);
    rig.app.go_sync();        renders(r, rig);
    rig.app.go_tags();        renders(r, rig);
    rig.app.open_settings();  renders(r, rig);
    rig.app.go_note_list();   renders(r, rig);

    rig.app.open_note(rec);   renders(r, rig);          // detail (static top)
    rig.app.tick_transcript_scroll(4000); renders(r, rig); // detail (scrolled)
    rig.app.open_player();    renders(r, rig);
    rig.app.back();                                       // back to detail
    rig.app.request_delete(); renders(r, rig);           // confirm dialog
    rig.app.cancel_delete();
    rig.app.start_recording(); renders(r, rig);          // recording screen
    rig.app.stop_recording();

    // favorite marker in the list + a no-audio / pending-transcription detail
    rig.notes.set_favorite(rec, true);
    rig.clock.advance(1);
    Note typed = rig.notes.create_note("typed pending");
    rig.app.refresh();
    rig.app.go_note_list();   renders(r, rig);           // favorite + audio markers
    rig.app.open_note(typed.id); renders(r, rig);        // no-audio buttons + "Pending"

    // non-paged list (<= 5 notes -> single wide REC button)
    Rig small;
    small.clock.advance(1); small.notes.create_note("only");
    small.app.refresh(); small.app.go_note_list();
    renders(r, small);
}

// Audio stub that reports "playing" so the player's active-state branches render.
struct PlayingAudio : IAudio {
    void start_capture() override {}
    RecordingResult stop_capture() override { return {}; }
    bool is_capturing() const override { return false; }
    void play(const std::string&) override {}
    bool is_playing() const override { return true; }
    int playback_total_secs() const override { return 10; }
    int playback_pos_secs() const override { return 3; }
    float playback_progress() const override { return 0.3f; }
};

static void test_player_playing() {
    host::ManualClock clock; host::ConsoleLog log{false};
    PlayingAudio audio; IdGenerator ids{clock, 9};
    InMemoryNoteRepository repo; NoteService notes{repo, ids, clock, log};
    AudioService audioSvc{audio, notes, ids, log};
    AppController app{notes, audioSvc, clock, log};
    Note n = notes.create_note("song");
    notes.set_audio(n.id, "k.wav", 10);
    app.refresh();
    app.open_note(n.id);
    app.open_player();
    Renderer r; Canvas c;
    r.render(c, app, 87, 1000); // playing -> pause icon + progress fill drawn
    CHECK(ink(c) > 0);
    CHECK(app.audio_playing());
}

static void test_hittest() {
    Renderer r;
    Rig rig;
    for (int i = 0; i < 7; ++i) { rig.clock.advance(1); rig.notes.create_note("n"); }
    std::string rec = rig.record();
    rig.app.refresh();

    using T = Hit::Type;
    // Home
    rig.app.go_home();
    CHECK(r.hittest(20, 60, rig.app).type == T::GoNotes);
    CHECK(r.hittest(140, 60, rig.app).type == T::GoSync);
    CHECK(r.hittest(20, 140, rig.app).type == T::GoTags);
    CHECK(r.hittest(140, 140, rig.app).type == T::Settings);
    CHECK(r.hittest(20, 60, rig.app).is_button());

    // Note list (paged: 8 notes incl. recording -> 2 pages)
    rig.app.go_note_list();
    CHECK(r.hittest(160, 15, rig.app).type == T::Back);     // tall Menu button
    CHECK(r.hittest(20, 170, rig.app).type == T::PageUp);
    CHECK(r.hittest(160, 170, rig.app).type == T::PageDown);
    CHECK(r.hittest(100, 170, rig.app).type == T::Record);  // REC
    CHECK(r.hittest(10, 42, rig.app).type == T::OpenNote);  // first row

    // Settings
    rig.app.open_settings();
    CHECK(r.hittest(120, 55, rig.app).type == T::SpeedDown);
    CHECK(r.hittest(178, 55, rig.app).type == T::SpeedUp);
    CHECK(r.hittest(120, 103, rig.app).type == T::FormatPrev);
    CHECK(r.hittest(178, 103, rig.app).type == T::FormatNext);
    CHECK(r.hittest(100, 170, rig.app).type == T::Back);

    // Sync / Tags
    rig.app.go_sync();
    CHECK(r.hittest(100, 130, rig.app).type == T::SyncNow);
    CHECK(r.hittest(100, 170, rig.app).type == T::Back);
    rig.app.go_tags();
    CHECK(r.hittest(100, 170, rig.app).type == T::Back);

    // Detail (has audio): play / delete / back / tap-text
    rig.app.open_note(rec);
    CHECK(r.hittest(30, 170, rig.app).type == T::OpenPlayer);
    CHECK(r.hittest(100, 170, rig.app).type == T::Delete);
    CHECK(r.hittest(160, 170, rig.app).type == T::Back);
    CHECK(r.hittest(100, 60, rig.app).type == T::ToggleScroll);

    // Player
    rig.app.open_player();
    CHECK(r.hittest(40, 135, rig.app).type == T::PlayPause);
    CHECK(r.hittest(130, 135, rig.app).type == T::Stop);
    CHECK(r.hittest(100, 176, rig.app).type == T::Back);

    // Recording stop
    rig.app.back(); rig.app.back(); // -> list
    rig.app.start_recording();
    CHECK(r.hittest(100, 160, rig.app).type == T::Record);

    // ConfirmDelete dialog buttons
    rig.app.stop_recording();
    rig.app.open_note(rec);
    rig.app.request_delete();
    CHECK(r.hittest(50, 130, rig.app).type == T::ConfirmDelete);
    CHECK(r.hittest(140, 130, rig.app).type == T::CancelDelete);

    // Splash ignores taps
    rig.app.show_splash();
    CHECK(r.hittest(100, 100, rig.app).type == T::None);
}

int main() {
    test_canvas();
    test_screens_render();
    test_player_playing();
    test_hittest();
    if (g_fail == 0) { std::printf("OK: render %d checks\n", g_checks); return 0; }
    std::fprintf(stderr, "FAILED %d/%d\n", g_fail, g_checks);
    return 1;
}
