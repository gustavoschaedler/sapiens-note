// Unit tests for the small pure-logic core units: Format, AsciiText, SplashAnim,
// IdGenerator, and AudioService (with the fake HAL).
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <set>
#include <string>

#include "sapiens/AsciiText.h"
#include "sapiens/AudioService.h"
#include "sapiens/Format.h"
#include "sapiens/Id.h"
#include "sapiens/NoteRepository.h"
#include "sapiens/NoteService.h"
#include "sapiens/SplashAnim.h"
#include "sapiens/host/Host.h"

using namespace sapiens;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                \
    do {                                                                        \
        ++g_checks;                                                             \
        if (!(c)) { ++g_fail; std::fprintf(stderr, "FAIL %s:%d %s\n",           \
                    __FILE__, __LINE__, #c); }                                  \
    } while (0)
#define EQ(a, b) CHECK((a) == (b))

static void test_format() {
    using namespace fmt;
    EQ(format_duration(0), std::string("0:00"));
    EQ(format_duration(8), std::string("0:08"));
    EQ(format_duration(83), std::string("1:23"));
    EQ(format_duration(600), std::string("10:00"));
    EQ(format_duration(-5), std::string("0:00")); // negative clamps to 0

    CHECK(kNameFormatCount == 5);
    // every index maps to a non-empty strftime pattern; wraps for </> range
    for (int i = -7; i < 12; ++i) CHECK(name_format_pattern(i)[0] == '%');
    EQ(std::string(name_format_pattern(0)), std::string(name_format_pattern(5)));

    // deterministic in UTC
    setenv("TZ", "UTC", 1);
    tzset();
    const Millis ms = 1700000000000LL; // 2023-11-14 22:13:20 UTC
    EQ(format_timestamp(ms, 0), std::string("2023-11-14 22:13"));
    EQ(format_timestamp(ms, 3), std::string("2023-11-14 22:13:20"));
    CHECK(!format_timestamp(ms, 2).empty());
    CHECK(!format_timestamp(ms, 4).empty());
}

static void test_to_ascii() {
    using text::to_ascii;
    EQ(to_ascii("plain ascii 123!"), std::string("plain ascii 123!"));
    EQ(to_ascii(""), std::string(""));
    EQ(to_ascii("a\tb"), std::string("a b"));      // tab -> space
    EQ(to_ascii("a\nb"), std::string("a\nb"));      // newline kept
    EQ(to_ascii(std::string("a\x01\x02""b")), std::string("ab")); // control dropped
    // Latin-1 accents -> base letters
    EQ(to_ascii("caf\xC3\xA9"), std::string("cafe"));            // é
    EQ(to_ascii("a\xC3\xA7\xC3\xA3o"), std::string("acao"));    // ç ã
    EQ(to_ascii("\xC3\x87"), std::string("C"));                  // Ç
    EQ(to_ascii("\xC3\xB1"), std::string("n"));                  // ñ
    EQ(to_ascii("\xC3\x9F"), std::string("ss"));                 // ß
    EQ(to_ascii("\xC3\x86"), std::string("AE"));                 // Æ
    EQ(to_ascii("x\xC2\xA0y"), std::string("x y"));             // nbsp -> space
    EQ(to_ascii("\xC2\xA9"), std::string(""));                   // © dropped
    // smart punctuation
    EQ(to_ascii("\xE2\x80\x9Chi\xE2\x80\x9D"), std::string("\"hi\""));
    EQ(to_ascii("it\xE2\x80\x99s"), std::string("it's"));
    EQ(to_ascii("a\xE2\x80\x94""b"), std::string("a-b"));      // em dash
    EQ(to_ascii("end\xE2\x80\xA6"), std::string("end..."));     // ellipsis
    EQ(to_ascii("\xF0\x9F\x98\x80!"), std::string("!"));         // emoji dropped
    // every Latin-1 transliteration group (one representative per return line)
    EQ(to_ascii("\xC3\x80"), std::string("A"));   // À
    EQ(to_ascii("\xC3\x88"), std::string("E"));   // È
    EQ(to_ascii("\xC3\x8C"), std::string("I"));   // Ì
    EQ(to_ascii("\xC3\x90"), std::string("D"));   // Ð
    EQ(to_ascii("\xC3\x92"), std::string("O"));   // Ò
    EQ(to_ascii("\xC3\x98"), std::string("O"));   // Ø
    EQ(to_ascii("\xC3\x99"), std::string("U"));   // Ù
    EQ(to_ascii("\xC3\x9D"), std::string("Y"));   // Ý
    EQ(to_ascii("\xC3\xA0"), std::string("a"));   // à
    EQ(to_ascii("\xC3\xA6"), std::string("ae"));  // æ
    EQ(to_ascii("\xC3\xA8"), std::string("e"));   // è
    EQ(to_ascii("\xC3\xAC"), std::string("i"));   // ì
    EQ(to_ascii("\xC3\xB0"), std::string("d"));   // ð
    EQ(to_ascii("\xC3\xB2"), std::string("o"));   // ò
    EQ(to_ascii("\xC3\xB9"), std::string("u"));   // ù
    EQ(to_ascii("\xC3\xBF"), std::string("y"));   // ÿ
    EQ(to_ascii("\xC3\x97"), std::string(""));    // × -> dropped (default)
    EQ(to_ascii("\xC3\x9E"), std::string(""));    // Þ -> dropped
    EQ(to_ascii("a\xE2\x80\x93""b"), std::string("a-b")); // en dash
    EQ(to_ascii("\xE2\x80\x80"), std::string(""));         // en-quad -> dropped
    EQ(to_ascii("\xE0\xA4\x85"), std::string(""));         // 3-byte unknown lead
    EQ(to_ascii(std::string("x\xC3", 2)), std::string("x")); // truncated lead byte
}

static void test_splash() {
    using splash::compute;
    auto f0 = compute(0);
    EQ(f0.line1, std::string("")); EQ(f0.line2, std::string("")); CHECK(f0.visible);
    EQ(compute(130).line1, std::string("S"));
    auto f7 = compute(7 * 130);
    EQ(f7.line1, std::string("Sapiens")); EQ(f7.line2, std::string(""));
    EQ(compute(8 * 130).line2, std::string("N"));
    auto full = compute(11 * 130);             // A(7)+B(4)=11 letters
    EQ(full.line1, std::string("Sapiens")); EQ(full.line2, std::string("Note"));
    CHECK(compute(11 * 130).visible);          // hold before blink
    CHECK(compute(1700).visible);              // blink on
    CHECK(!compute(1700 + 450).visible);       // blink off
    CHECK(compute(1700 + 900).visible);        // blink on again
    CHECK(compute(-50).line1.empty());         // negative guarded
}

static bool is_hex(const std::string& s) {
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

static void test_id() {
    host::ManualClock clock;
    IdGenerator ids{clock, 12345};
    std::string a = ids.next();
    std::string b = ids.next(); // same ms -> counter bumps, still ordered
    CHECK(a.size() == 32);
    CHECK(is_hex(a));
    CHECK(a != b);
    CHECK(a < b);
    clock.advance(10);
    std::string c = ids.next();
    CHECK(b < c);

    // uniqueness across many in the same ms
    std::set<std::string> seen;
    for (int i = 0; i < 1000; ++i) seen.insert(ids.next());
    CHECK(seen.size() == 1000);

    // deterministic with a fixed seed + clock
    host::ManualClock c1, c2;
    IdGenerator g1{c1, 777}, g2{c2, 777};
    EQ(g1.next(), g2.next());

    // seed=0 path (random) still well-formed
    host::ManualClock c3;
    IdGenerator g3{c3, 0};
    std::string r = g3.next();
    CHECK(r.size() == 32 && is_hex(r));
}

static void test_audio_service() {
    host::ManualClock clock;
    host::ConsoleLog log{false};
    host::FakeAudio audio{clock};
    IdGenerator ids{clock, 1};
    InMemoryNoteRepository repo;
    NoteService notes{repo, ids, clock, log};
    AudioService svc{audio, notes, ids, log};

    // stop before start -> nullopt (warn branch)
    CHECK(!svc.stop_and_create().has_value());
    CHECK(!svc.is_recording());

    // start (idempotent), record 3s, stop -> note + clip
    svc.start();
    svc.start(); // no-op branch
    CHECK(svc.is_recording());
    clock.advance(3000);
    auto note = svc.stop_and_create();
    CHECK(note.has_value());
    CHECK(!svc.is_recording());
    CHECK(svc.last_clip().has_value());
    EQ(svc.last_clip()->duration_secs, 3);
    EQ(svc.last_clip()->note_id, note->id);
    CHECK(!note->audio_key.empty());
    EQ(note->audio_secs, 3);

    // explicit title
    svc.start();
    clock.advance(1000);
    auto titled = svc.stop_and_create("My note");
    CHECK(titled && titled->title == "My note");

    // playback delegates (FakeAudio returns defaults; just exercise the paths)
    svc.play("");                 // warn: empty key
    svc.play(note->audio_key);     // info: plays
    svc.pause_resume();
    svc.stop();
    CHECK(!svc.is_playing());
    CHECK(!svc.is_paused());
    EQ(svc.pos_secs(), 0);
    EQ(svc.total_secs(), 0);
    CHECK(svc.progress() == 0.0f);
}

static void test_host() {
    host::SystemClock sc;
    CHECK(sc.now_ms() > 0);
    CHECK(sc.monotonic_ms() > 0);
    host::ConsoleLog on{true};   // exercise every level + tag (to stderr)
    on.log(LogLevel::Debug, "d");
    on.log(LogLevel::Info, "i");
    on.log(LogLevel::Warn, "w");
    on.log(LogLevel::Error, "e");
    host::ConsoleLog off{false};
    off.log(LogLevel::Info, "silenced"); // early-return branch
    host::ManualClock mc{1000};
    host::FakeAudio fa{mc};
    fa.play("ignored"); // no-op branch
    CHECK(!fa.is_capturing());
}

static void test_note_service_extra() {
    host::ManualClock clock;
    host::ConsoleLog log{false};
    IdGenerator ids{clock, 3};
    InMemoryNoteRepository repo;
    NoteService notes{repo, ids, clock, log};

    Note n = notes.create_note("orig");
    CHECK(notes.rename(n.id, "renamed"));
    CHECK(notes.get_note(n.id)->title == "renamed");
    CHECK(notes.set_status(n.id, NoteStatus::Uploaded));
    CHECK(notes.get_note(n.id)->status == NoteStatus::Uploaded);

    Tag t = notes.create_tag("x");
    CHECK(notes.add_tag(n.id, t.id));
    CHECK(notes.remove_tag(n.id, t.id));
    CHECK(notes.get_note(n.id)->tag_ids.empty());
    CHECK(notes.list_tags().size() == 1);

    // operations on a missing id fail gracefully (mutate "not found" branch)
    CHECK(!notes.rename("nope", "x"));
    CHECK(!notes.set_favorite("nope", true));
    CHECK(!notes.set_audio("nope", "k", 1));
    CHECK(!notes.set_status("nope", NoteStatus::Failed));
    CHECK(!notes.apply_transcription("nope", "t", "s", {}, "en"));
    CHECK(!notes.soft_delete("nope"));
    CHECK(!notes.add_tag("nope", t.id));
    CHECK(!notes.get_note("nope").has_value());

    // empty-title create -> a date-time name is assigned
    Note auto_named = notes.create_note("");
    CHECK(!auto_named.title.empty());
}

int main() {
    test_format();
    test_to_ascii();
    test_splash();
    test_id();
    test_audio_service();
    test_host();
    test_note_service_extra();
    if (g_fail == 0) { std::printf("OK: core_extra %d checks\n", g_checks); return 0; }
    std::fprintf(stderr, "FAILED %d/%d\n", g_fail, g_checks);
    return 1;
}
