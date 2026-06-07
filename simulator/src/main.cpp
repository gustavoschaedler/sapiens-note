// SapiensNote desktop simulator. Runs the shared core behind an SDL2 desktop HAL,
// rendering the 200x200 1-bit e-ink UI scaled up. See docs/SIMULATOR_SPEC.md.
//
//   sapiens-sim              # window; persists to ~/.sapiensnote-sim, real mic capture
//   sapiens-sim --headless   # build/link + logic smoke test, no window (CI)
//   sapiens-sim --shot f.ppm # render the UI to an image (no window)
//
// Controls: click = tap, R = record/stop, Esc/B = back, S = settings, Q = quit.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "Canvas.h"
#include "Renderer.h"
#include "sapiens/AppController.h"
#include "sapiens/AudioService.h"
#include "sapiens/Id.h"
#include "sapiens/NoteRepository.h"
#include "sapiens/NoteService.h"
#include "sapiens/host/Host.h"

using namespace sapiens;

namespace {

// Wires the shared core around an injected repository + audio backend.
struct App {
    host::SystemClock clock;
    host::ConsoleLog log{false};
    IdGenerator ids{clock};
    NoteRepository& repo;
    NoteService notes;
    IAudio& audio;
    AudioService audioSvc;
    AppController app;

    App(NoteRepository& r, IAudio& a)
        : repo(r),
          notes(repo, ids, clock, log),
          audio(a),
          audioSvc(a, notes, ids, log),
          app(notes, audioSvc, clock, log) {}
};

// Persist the chosen name format across runs (~/.sapiensnote-sim/name_format.txt).
std::string settings_path() {
    const char* home = std::getenv("HOME");
    return (home ? std::string(home) : ".") + "/.sapiensnote-sim/name_format.txt";
}
void save_name_format(int f) {
    FILE* fp = std::fopen(settings_path().c_str(), "w");
    if (fp) { std::fprintf(fp, "%d", f); std::fclose(fp); }
}
int load_name_format() {
    FILE* fp = std::fopen(settings_path().c_str(), "r");
    int f = 0;
    if (fp) { if (std::fscanf(fp, "%d", &f) != 1) f = 0; std::fclose(fp); }
    return (f < 0 || f >= 5) ? 0 : f;
}

std::string speed_path() {
    const char* home = std::getenv("HOME");
    return (home ? std::string(home) : ".") + "/.sapiensnote-sim/scroll_speed.txt";
}
void save_scroll_speed(int s) {
    FILE* fp = std::fopen(speed_path().c_str(), "w");
    if (fp) { std::fprintf(fp, "%d", s); std::fclose(fp); }
}
int load_scroll_speed() {
    FILE* fp = std::fopen(speed_path().c_str(), "r");
    int s = 5;
    if (fp) { if (std::fscanf(fp, "%d", &s) != 1) s = 2; std::fclose(fp); }
    return (s < 1 || s > 5) ? 2 : s;
}

void apply_hit(App& a, const sim::Hit& h) {
    using T = sim::Hit::Type;
    switch (h.type) {
        case T::Record: a.app.handle(InputEvent{InputKind::ButtonRecord}); break;
        case T::OpenNote: a.app.open_note(h.note_id); break;
        case T::Back: a.app.back(); break;
        case T::Settings: a.app.open_settings(); break;
        case T::OpenPlayer: a.app.open_player(); break;
        case T::PlayPause: a.app.toggle_play(); break;
        case T::Stop: a.app.stop_play(); break;
        case T::Delete: a.app.request_delete(); break;
        case T::ConfirmDelete: a.app.confirm_delete(); break;
        case T::CancelDelete: a.app.cancel_delete(); break;
        case T::PageUp: a.app.page_up(); break;
        case T::PageDown: a.app.page_down(); break;
        case T::SpeedDown: a.app.set_scroll_speed(a.app.scroll_speed() - 1); save_scroll_speed(a.app.scroll_speed()); break;
        case T::SpeedUp: a.app.set_scroll_speed(a.app.scroll_speed() + 1); save_scroll_speed(a.app.scroll_speed()); break;
        case T::FormatPrev: { int n = a.app.name_format_count(); a.app.set_name_format((a.app.name_format() + n - 1) % n); save_name_format(a.app.name_format()); break; }
        case T::FormatNext: { int n = a.app.name_format_count(); a.app.set_name_format((a.app.name_format() + 1) % n); save_name_format(a.app.name_format()); break; }
        case T::GoNotes: a.app.go_note_list(); break;
        case T::GoSync: a.app.go_sync(); break;
        case T::GoTags: a.app.go_tags(); break;
        case T::SyncNow: a.app.refresh(); break; // placeholder (no server yet)
        case T::ToggleScroll: a.app.toggle_transcript_scroll(); break;
        case T::None: break;
    }
    a.app.refresh();
}

std::string data_dir() {
    const char* home = std::getenv("HOME");
    std::string base = home ? std::string(home) : ".";
    std::string dir = base + "/.sapiensnote-sim";
    std::error_code ec;
    std::filesystem::create_directories(dir + "/audio", ec);
    return dir;
}

// Headless: no SDL, no window. Exercises the same core + renderer logic.
int run_headless() {
    host::SystemClock extClock;
    host::FakeAudio fake{extClock};
    InMemoryNoteRepository repo;
    App a{repo, fake};
    sim::Renderer r;
    sim::Canvas c;

    std::printf("sapiens-sim --headless\n");
    a.app.go_note_list(); // skip the boot splash for the logic smoke test
    std::printf("screen=%s notes=%zu\n", to_string(a.app.screen()),
                a.app.visible_notes().size());

    apply_hit(a, r.hittest(90, 180, a.app)); // Record button (list)
    if (a.app.screen() != Screen::Recording) {
        std::fprintf(stderr, "expected Recording screen\n");
        return 1;
    }
    apply_hit(a, r.hittest(100, 160, a.app)); // big Stop button (recording)
    if (a.app.visible_notes().size() != 1) {
        std::fprintf(stderr, "expected 1 note after recording\n");
        return 1;
    }

    r.render(c, a.app, 87);
    int black = 0;
    for (int i = 0; i < sim::Canvas::W * sim::Canvas::H; ++i)
        black += c.data()[i] ? 1 : 0;

    apply_hit(a, r.hittest(5, sim::layout::kRowTop + 2, a.app));
    std::printf("after open: screen=%s, rendered black px=%d\n",
                to_string(a.app.screen()), black);
    std::printf("headless OK\n");
    return 0;
}

// Render the populated UI to a scaled PPM image (no window) for visual proof.
// `screen` may be "list" (default) or "detail".
int run_shot(const std::string& path, int scale, const std::string& screen) {
    host::SystemClock extClock;
    host::FakeAudio fake{extClock};
    InMemoryNoteRepository repo;
    App a{repo, fake};
    a.app.set_name_format(load_name_format()); // screenshots reflect chosen format
    a.app.set_scroll_speed(load_scroll_speed());
    for (int i = 1; i <= 4; ++i) a.notes.create_note("Older note " + std::to_string(i));

    Note typed = a.notes.create_note("Buy milk and eggs");
    Tag t = a.notes.create_tag("errands");
    a.notes.add_tag(typed.id, t.id);
    a.notes.set_favorite(typed.id, true);
    a.notes.apply_transcription(typed.id, "Buy milk and eggs on the way home.",
                                "Shopping reminder.", {"milk", "eggs"}, "en");

    // A recorded note (has audio) that has also been transcribed -> shows Play.
    a.app.start_recording();
    a.app.stop_recording();
    std::string rec_id = a.audioSvc.last_clip()->note_id; // title = date-time
    a.notes.apply_transcription(
        rec_id,
        "Remember to call the landlord about the leaking tap in the bathroom, "
        "and book the car service for next week.",
        "Call the landlord about the leaking tap; book the car service.",
        {"call landlord", "book car service"}, "en");

    // A long title to demonstrate the marquee (scrolling) behavior.
    Note long_note = a.notes.create_note(
        "Project kickoff brainstorm with the whole design team");
    a.notes.apply_transcription(long_note.id, "...", "Kickoff notes.", {}, "en");
    a.notes.create_note("Meeting idea");
    a.app.refresh();

    if (std::getenv("SAPIENS_LONGTX")) { // dev: long transcript to test vertical scroll
        std::string big;
        for (int i = 1; i <= 16; ++i)
            big += "Line " + std::to_string(i) +
                   ": remember to call the landlord about the leaking tap. ";
        a.notes.apply_transcription(rec_id, big, "", {}, "en");
        a.app.refresh();
    }
    if (screen == "detail") a.app.open_note(rec_id);
    else if (screen == "player") { a.app.open_note(rec_id); a.app.open_player(); }
    else if (screen == "confirm") { a.app.open_note(rec_id); a.app.request_delete(); }
    else if (screen == "recording") a.app.start_recording();
    else if (screen == "settings") a.app.open_settings();
    else if (screen == "splash") {
        a.app.show_splash();
        const char* ms = std::getenv("SAPIENS_SPLASH_MS");
        a.app.debug_set_splash_elapsed(ms ? std::atoi(ms) : 900);
    }
    else if (screen == "home") a.app.go_home();
    else if (screen == "sync") a.app.go_sync();
    else if (screen == "tags") a.app.go_tags();
    else a.app.go_note_list(); // default "list"

    sim::Renderer r;
    sim::Canvas c;
    const char* nowenv = std::getenv("SAPIENS_NOW"); // dev: override animation time
    if (nowenv && screen == "detail") a.app.tick_transcript_scroll((int)std::atol(nowenv));
    r.render(c, a.app, 87, nowenv ? (std::uint32_t)std::atol(nowenv) : 2500);
    if (const char* p = std::getenv("SAPIENS_PRESS")) { // screenshot the invert flash
        std::string s = p;
        int bx = (s == "sync" || s == "settings") ? 102 : 6;
        int by = (s == "tags" || s == "settings") ? 118 : 46;
        c.invert_round_rect(bx, by, 92, 58, 7);
    }

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); return 1; }
    const int W = sim::Canvas::W, H = sim::Canvas::H;
    const int OW = W * scale, OH = H * scale;
    const std::uint8_t* px = c.data();

    auto level = [&](int x, int y) -> float {
        if (x < 0) x = 0; if (y < 0) y = 0;
        if (x >= W) x = W - 1; if (y >= H) y = H - 1;
        return px[y * W + x];
    };
    // Bilinear upscale so the small panel looks smooth instead of blocky.
    std::fprintf(f, "P6\n%d %d\n255\n", OW, OH);
    for (int oy = 0; oy < OH; ++oy) {
        float sy = (oy + 0.5f) / scale - 0.5f;
        int y0 = (int)std::floor(sy); float fy = sy - y0;
        for (int ox = 0; ox < OW; ++ox) {
            float sx = (ox + 0.5f) / scale - 0.5f;
            int x0 = (int)std::floor(sx); float fx = sx - x0;
            float top = level(x0, y0) * (1 - fx) + level(x0 + 1, y0) * fx;
            float bot = level(x0, y0 + 1) * (1 - fx) + level(x0 + 1, y0 + 1) * fx;
            float lvl = top * (1 - fy) + bot * fy;
            unsigned char v = (unsigned char)(255.0f - lvl); // ink -> gray
            std::fputc(v, f); std::fputc(v, f); std::fputc(v, f);
        }
    }
    std::fclose(f);
    std::printf("wrote %s (%dx%d)\n", path.c_str(), OW, OH);
    return 0;
}

} // namespace

#ifdef SAPIENS_WITH_SDL
#include <SDL.h>

#include "SdlAudio.h"
#include "sapiens/SqliteNoteRepository.h"

static int run_window(int scale) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    const std::string dir = data_dir();
    SqliteNoteRepository repo(dir + "/sapiensnote.db"); // persistent local store
    sim::SdlAudio audio(dir + "/audio");             // real mic capture
    App a{repo, audio};
    a.app.set_name_format(load_name_format());       // restore chosen name format
    a.app.set_scroll_speed(load_scroll_speed());     // restore scroll speed

    const int W = sim::Canvas::W, H = sim::Canvas::H;
    // At real size (scale 1) the e-ink buffer maps 1:1 -> crisp, anti-aliased font
    // with no stretching. Only scale>1 needs filtering (linear = soft, not jagged).
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, scale > 1 ? "linear" : "nearest");
    // No ALLOW_HIGHDPI: keep the backbuffer == window so 1:1 isn't re-scaled.
    SDL_Window* win = SDL_CreateWindow(
        "SapiensNote Simulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        W * scale, H * scale, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    SDL_RenderSetLogicalSize(ren, W, H); // device pixels; SDL maps to the window
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, W, H);

    sim::Renderer r;
    sim::Canvas canvas;
    std::vector<std::uint32_t> argb(W * H);

    auto blit = [&]() {
        const std::uint8_t* px = canvas.data();
        for (int i = 0; i < W * H; ++i) {
            std::uint32_t g = 255u - px[i]; // ink level -> gray (white bg)
            argb[i] = 0xFF000000u | (g << 16) | (g << 8) | g;
        }
        SDL_UpdateTexture(tex, nullptr, argb.data(), W * sizeof(std::uint32_t));
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
    };
    auto paint = [&]() { r.render(canvas, a.app, 87, SDL_GetTicks()); blit(); };

    bool running = true;
    Uint32 last_sync = 0;             // periodic reload so external transcripts appear live
    Uint32 prev_ticks = SDL_GetTicks(); // for the transcript scroll clock
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                sim::Hit h = r.hittest(e.button.x / scale, e.button.y / scale, a.app);
                if (h.is_button()) { // e-ink color-invert flash on every button
                    r.render(canvas, a.app, 87, SDL_GetTicks());
                    canvas.invert_round_rect(h.rx, h.ry, h.rw, h.rh, 7);
                    blit();
                    SDL_Delay(130);
                }
                apply_hit(a, h);
            } else if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                    case SDLK_q: running = false; break;
                    case SDLK_r: apply_hit(a, sim::Hit{sim::Hit::Type::Record, ""}); break;
                    case SDLK_s: apply_hit(a, sim::Hit{sim::Hit::Type::Settings, ""}); break;
                    case SDLK_p: apply_hit(a, sim::Hit{sim::Hit::Type::OpenPlayer, ""}); break;
                    case SDLK_SPACE: apply_hit(a, sim::Hit{sim::Hit::Type::PlayPause, ""}); break;
                    case SDLK_b:
                    case SDLK_ESCAPE: apply_hit(a, sim::Hit{sim::Hit::Type::Back, ""}); break;
                    default: break;
                }
            }
        }
        a.app.maybe_advance_splash(); // boot splash -> Home after 4s
        // Advance the transcript scroll clock (paused via tap) while on detail.
        Uint32 tk = SDL_GetTicks();
        if (a.app.screen() == Screen::NoteDetail)
            a.app.tick_transcript_scroll((int)(tk - prev_ticks));
        prev_ticks = tk;
        // Poll the DB so transcripts written by sapiens-web appear without navigating.
        Uint32 nowt = SDL_GetTicks();
        if (nowt - last_sync > 1500) {
            auto s = a.app.screen();
            if (s == Screen::NoteList || s == Screen::NoteDetail) {
                a.app.refresh();
                a.app.reload_selected();
            }
            last_sync = nowt;
        }
        paint();
        SDL_Delay(16);
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
#endif // SAPIENS_WITH_SDL

int main(int argc, char** argv) {
    bool headless = false;
    int scale = 1; // real size (200x200), crisp 1:1; use --scale N to enlarge
    std::string shot, shot_screen = "list";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0) headless = true;
        else if (std::strcmp(argv[i], "--shot") == 0 && i + 1 < argc) shot = argv[++i];
        else if (std::strcmp(argv[i], "--screen") == 0 && i + 1 < argc) shot_screen = argv[++i];
        else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) scale = std::atoi(argv[++i]);
    }

    if (!shot.empty()) return run_shot(shot, scale, shot_screen);
    if (headless) return run_headless();

#ifdef SAPIENS_WITH_SDL
    return run_window(scale);
#else
    std::fprintf(stderr, "built without SDL; use --headless\n");
    (void)scale;
    return run_headless();
#endif
}
