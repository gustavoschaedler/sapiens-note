// LVGL-based SapiensNote simulator. Mirrors the main simulator and the device UI,
// rendered at native resolution (crisp vector fonts via tiny_ttf, no nearest-zoom)
// with REAL audio (record + playback) via the shared SdlAudio. Same shared core.
//
//   sapiens-sim-lvgl [--scale N]   # window (N = UI scale, default 2)
//   sapiens-sim-lvgl --shot f.ppm [--screen list|detail|player]
//   sapiens-sim-lvgl --headless
//
// Override the font with SAPIENS_TTF=/path/to/font.ttf.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <SDL.h>
#include "lvgl.h" // also pulls in lv_tiny_ttf when LV_USE_TINY_TTF=1

#include "SdlAudio.h" // shared with the main simulator (real capture/playback)
#include "Icons.h"    // shared return-arrow drawing
#include "sapiens/AppController.h"
#include "sapiens/AsciiText.h"
#include "sapiens/Format.h"
#include "sapiens/SplashAnim.h"
#include "sapiens/AudioService.h"
#include "sapiens/Id.h"
#include "sapiens/Version.h"
#include "LogoBitmap.h"
#include "sapiens/NoteRepository.h"
#include "sapiens/NoteService.h"
#include "sapiens/SqliteNoteRepository.h"
#include "sapiens/host/Host.h"

using namespace sapiens;

namespace {

int g_s = 2; // UI scale: render at 200*g_s for crisp, real-size fonts
inline int P(int v) { return v * g_s; }

std::string data_dir() {
    const char* home = std::getenv("HOME");
    std::string dir = (home ? std::string(home) : ".") + "/.sapiensnote-sim";
    std::error_code ec;
    std::filesystem::create_directories(dir + "/audio", ec);
    return dir;
}

// Audio is injected: SdlAudio (window) for real mic; FakeAudio (headless/shot) so
// CI and screenshots never touch the microphone.
struct App {
    host::SystemClock clock;
    host::ConsoleLog log{false};
    IdGenerator ids{clock};
    NoteRepository& repo;          // injected: SQLite (window) or in-memory (shots)
    NoteService notes;
    IAudio& audio;
    AudioService audioSvc;
    AppController app;
    App(NoteRepository& r, IAudio& a)
        : repo(r), notes(repo, ids, clock, log), audio(a),
          audioSvc(a, notes, ids, log), app(notes, audioSvc, clock, log) {}
};

App* g_app = nullptr;
const lv_font_t* g_font = nullptr;
const lv_font_t* g_font_big = nullptr;
std::vector<std::string> g_row_ids;

// live-updated widgets (so we refresh without rebuilding & resetting scroll)
lv_obj_t* g_rec_timer = nullptr;
lv_obj_t* g_pl_state = nullptr;
lv_obj_t* g_pl_pos = nullptr;
lv_obj_t* g_pl_bar = nullptr;
lv_obj_t* g_pl_ppbtn_lbl = nullptr;
lv_obj_t* g_splash_a = nullptr;
lv_obj_t* g_splash_b = nullptr;
int g_sync_ticks = 0;       // throttle the DB poll (tick is 60ms)
std::string g_data_sig;     // rebuild only when the data actually changed
lv_obj_t* g_tx1 = nullptr;  // transcript scroll: two copies for the infinite loop
lv_obj_t* g_tx2 = nullptr;
int g_tx_period = 0;        // px (real); 0 = content fits, no scroll

inline lv_color_t white() { return lv_color_white(); }
inline lv_color_t black() { return lv_color_black(); }

// Display name = title + recording duration (when the note has audio).
inline std::string display_title(const Note& n) {
    std::string t = sapiens::text::to_ascii(n.title.empty() ? "Untitled" : n.title);
    if (n.audio_secs > 0) t += "  " + sapiens::fmt::format_duration(n.audio_secs);
    return t;
}

// Persist the chosen name format (shared file with the main simulator).
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
    return (f < 0 || f >= sapiens::fmt::kNameFormatCount) ? 0 : f;
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

// --- styling helpers (take 200-space coords, scale by g_s) -------------------
void style_panel(lv_obj_t* o) {
    lv_obj_set_style_bg_color(o, white(), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(o, black(), 0);
    lv_obj_set_style_text_font(o, g_font, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* label(lv_obj_t* parent, const std::string& txt, int x, int y,
                const lv_font_t* font = nullptr, int w = 0) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_color(l, black(), 0);
    lv_obj_set_style_text_font(l, font ? font : g_font, 0);
    if (w > 0) { lv_obj_set_width(l, P(w)); lv_label_set_long_mode(l, LV_LABEL_LONG_DOT); }
    lv_label_set_text(l, txt.c_str());
    lv_obj_set_pos(l, P(x), P(y));
    return l;
}

lv_obj_t* hline(lv_obj_t* parent, int x, int y, int w) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(o, P(w), 2);
    lv_obj_set_pos(o, P(x), P(y));
    lv_obj_set_style_bg_color(o, black(), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    return o;
}

// A filled rectangle block (no border) in 200-space coords.
lv_obj_t* block(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(o, P(w), P(h));
    lv_obj_set_pos(o, P(x), P(y));
    lv_obj_set_style_bg_color(o, black(), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    return o;
}

// Small battery glyph whose fill reflects the charge level (matches hand sim).
void battery_icon(lv_obj_t* parent, int x, int y, int w, int h, int pct) {
    lv_obj_t* body = lv_obj_create(parent);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(body, P(w), P(h));
    lv_obj_set_pos(body, P(x), P(y));
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(body, black(), 0);
    lv_obj_set_style_border_width(body, 2, 0);
    lv_obj_set_style_radius(body, P(2), 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    block(parent, x + w, y + h / 2 - 2, 2, 4); // terminal nub
    int p = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
    int fw = (w - 4) * p / 100;
    if (fw > 0) block(parent, x + 2, y + 2, fw, h - 4); // proportional fill
}

void marker(lv_obj_t* parent, int x, int y, int d, int radius) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(o, P(d), P(d));
    lv_obj_set_pos(o, P(x), P(y));
    lv_obj_set_style_bg_color(o, black(), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, radius, 0);
}

// e-ink color-invert on click: black face + white label while pressed.
void invert_button(lv_obj_t* b, bool on) {
    lv_obj_set_style_bg_color(b, black(), 0);
    lv_obj_set_style_bg_opa(b, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_t* lbl = lv_obj_get_child(b, 0);
    if (lbl) lv_obj_set_style_text_color(lbl, on ? white() : black(), 0);
}
void on_btn_press(lv_event_t* e) { invert_button((lv_obj_t*)lv_event_get_target(e), true); }
void on_btn_unpress(lv_event_t* e) { invert_button((lv_obj_t*)lv_event_get_target(e), false); }

lv_obj_t* button(lv_obj_t* parent, const std::string& txt, int x, int y, int w,
                 int h, lv_event_cb_t cb, void* ud, lv_obj_t** out_lbl = nullptr) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_pos(b, P(x), P(y));
    lv_obj_set_size(b, P(w), P(h));
    // Outline button: no fill, just a black border + soft drop shadow (3D).
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0); // no fill
    lv_obj_set_style_border_color(b, black(), 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_radius(b, P(10), 0); // rounded corners
    lv_obj_set_style_shadow_width(b, P(5), 0);
    lv_obj_set_style_shadow_offset_x(b, P(1), 0);
    lv_obj_set_style_shadow_offset_y(b, P(3), 0);
    lv_obj_set_style_shadow_color(b, black(), 0);
    lv_obj_set_style_shadow_opa(b, LV_OPA_40, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    // Pressed state: sink in (nudge down, smaller shadow) + e-ink color invert.
    lv_obj_set_style_translate_y(b, P(2), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_offset_y(b, P(1), LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, on_btn_press, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(b, on_btn_unpress, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(b, on_btn_unpress, LV_EVENT_PRESS_LOST, nullptr);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    lv_obj_t* l = lv_label_create(b);
    lv_obj_set_style_text_color(l, black(), 0);
    lv_obj_set_style_text_font(l, g_font, 0);
    lv_label_set_text(l, txt.c_str());
    lv_obj_center(l);
    if (out_lbl) *out_lbl = l;
    return b;
}

// A button whose label uses the big font (for large icons / REC text).
lv_obj_t* big_button(lv_obj_t* parent, const char* text, int x, int y, int w,
                     int h, lv_event_cb_t cb, void* ud = nullptr) {
    lv_obj_t* lbl = nullptr;
    lv_obj_t* b = button(parent, "", x, y, w, h, cb, ud, &lbl);
    lv_obj_set_style_text_font(lbl, g_font_big, 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return b;
}

// --- "return/undo" back icon, drawn once into an A8 image (shared by buttons) --
lv_image_dsc_t g_back_dsc;
std::vector<std::uint8_t> g_back_buf;
void make_back_image() {
    const int W = 56, H = 46;
    g_back_buf.assign((size_t)W * H, 0);
    sim::icons::return_arrow(
        [&](int x, int y) { if (x >= 0 && y >= 0 && x < W && y < H) g_back_buf[(size_t)y * W + x] = 255; },
        W, H);
    g_back_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    g_back_dsc.header.cf = LV_COLOR_FORMAT_A8;
    g_back_dsc.header.w = W;
    g_back_dsc.header.h = H;
    g_back_dsc.header.stride = W;
    g_back_dsc.data = g_back_buf.data();
    g_back_dsc.data_size = (uint32_t)g_back_buf.size();
}
// Back button: a plain text "Back" button (rounded).
lv_obj_t* back_button(lv_obj_t* parent, int x, int y, int w, int h, lv_event_cb_t cb) {
    return button(parent, "Back", x, y, w, h, cb, nullptr);
}

// Map scroll speed (1..10) to a label scroll animation duration (ms): slow->long.
int scroll_anim_ms() {
    // speed 1..4 -> 12000ms (slow) .. 6000ms (fast). Recalibrated slower.
    return 14500 - g_app->app.scroll_speed() * 2000; // 1..5 -> 12500..4500ms
}
// A label that scrolls (marquee) at the configured speed.
lv_obj_t* scroll_label(lv_obj_t* parent, const std::string& txt, int x, int y,
                       const lv_font_t* font, int w) {
    lv_obj_t* l = label(parent, txt, x, y, font, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_duration(l, scroll_anim_ms(), 0);
    return l;
}

void rebuild();

// --- event callbacks --------------------------------------------------------
void on_record(lv_event_t*) { g_app->app.handle(InputEvent{InputKind::ButtonRecord}); g_app->app.refresh(); rebuild(); }
void on_back(lv_event_t*) { g_app->app.back(); rebuild(); }
void on_open_player(lv_event_t*) { g_app->app.open_player(); rebuild(); }
void on_playpause(lv_event_t*) { g_app->app.toggle_play(); rebuild(); }
void on_stop(lv_event_t*) { g_app->app.stop_play(); rebuild(); }
void on_sync_now(lv_event_t*) { g_app->app.refresh(); rebuild(); } // placeholder
void on_go_notes(lv_event_t*) { g_app->app.go_note_list(); rebuild(); }
void on_go_sync(lv_event_t*) { g_app->app.go_sync(); rebuild(); }
void on_go_tags(lv_event_t*) { g_app->app.go_tags(); rebuild(); }
void on_toggle_scroll(lv_event_t*) { g_app->app.toggle_transcript_scroll(); } // tap text
void on_settings(lv_event_t*) { g_app->app.open_settings(); rebuild(); }
void on_delete(lv_event_t*) { g_app->app.request_delete(); rebuild(); }
void on_confirm_del(lv_event_t*) { g_app->app.confirm_delete(); rebuild(); }
void on_cancel_del(lv_event_t*) { g_app->app.cancel_delete(); rebuild(); }
void on_page_up(lv_event_t*) { g_app->app.page_up(); rebuild(); }
void on_page_down(lv_event_t*) { g_app->app.page_down(); rebuild(); }
void on_speed_down(lv_event_t*) { auto& a = g_app->app; a.set_scroll_speed(a.scroll_speed() - 1); save_scroll_speed(a.scroll_speed()); rebuild(); }
void on_speed_up(lv_event_t*) { auto& a = g_app->app; a.set_scroll_speed(a.scroll_speed() + 1); save_scroll_speed(a.scroll_speed()); rebuild(); }
void on_format_prev(lv_event_t*) { auto& a = g_app->app; int n = a.name_format_count(); a.set_name_format((a.name_format() + n - 1) % n); save_name_format(a.name_format()); rebuild(); }
void on_format_next(lv_event_t*) { auto& a = g_app->app; int n = a.name_format_count(); a.set_name_format((a.name_format() + 1) % n); save_name_format(a.name_format()); rebuild(); }
void on_row(lv_event_t* e) {
    auto idx = reinterpret_cast<std::intptr_t>(lv_event_get_user_data(e));
    if (idx >= 0 && idx < (std::intptr_t)g_row_ids.size())
        g_app->app.open_note(g_row_ids[idx]);
    rebuild();
}

// --- screens ----------------------------------------------------------------
void title_bar(lv_obj_t* scr) {
    label(scr, "Sapiens Note", 4, 2, g_font);
    lv_obj_t* b = label(scr, "87%", 0, 0, &lv_font_montserrat_14);
    lv_obj_align(b, LV_ALIGN_TOP_RIGHT, P(-6), P(6));
    hline(scr, 0, 20, 200);
}

void build_list(lv_obj_t* scr) {
    auto& app = g_app->app;
    // Custom 2-line header: brand (line 1) + counter/pagination (line 2) on the
    // left; on the right a tall "Menu" button, the battery before it, and a
    // vertical separator line.
    label(scr, "Sapiens Note", 4, 2, g_font);
    std::string sub = std::to_string(app.total_notes()) + " notes";
    if (app.page_count() > 1)
        sub += "   " + std::to_string(app.list_page() + 1) + "/" +
               std::to_string(app.page_count());
    label(scr, sub, 6, 22);
    button(scr, "Menu", 150, 3, 46, 32, on_back, nullptr); // tall, spans 2 lines
    // battery icon with the percentage centered below it (bigger text)
    const int batVal = 87, icx = 120, icw = 22, cx = icx + icw / 2;
    battery_icon(scr, icx, 3, icw, 10, batVal);
    lv_obj_t* bat = lv_label_create(scr);
    lv_obj_set_style_text_color(bat, black(), 0);
    lv_obj_set_style_text_font(bat, &lv_font_montserrat_28, 0);
    lv_label_set_text(bat, (std::to_string(batVal) + "%").c_str());
    lv_obj_set_width(bat, P(40));
    lv_obj_set_style_text_align(bat, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(bat, P(cx - 20), P(15));
    hline(scr, 0, 38, 200);

    auto notes = app.page_notes();
    g_row_ids.clear();
    if (notes.empty()) label(scr, "No notes yet - tap Rec", 18, 96);

    const int top = 40, rowH = 23;
    for (int i = 0; i < (int)notes.size(); ++i) {
        const auto& n = notes[i];
        int ry = top + i * rowH;
        g_row_ids.push_back(n.id);
        lv_obj_t* row = lv_obj_create(scr);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(row, P(200), P(rowH));
        lv_obj_set_pos(row, 0, P(ry));
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, on_row, LV_EVENT_CLICKED,
                            reinterpret_cast<void*>((std::intptr_t)i));
        char num[8];
        std::snprintf(num, sizeof(num), "%04d", app.page_first_index() + i + 1);
        label(row, num, 6, 4, g_font);
        if (n.status != NoteStatus::Transcribed) marker(row, 44, rowH / 2 - 3, 6, LV_RADIUS_CIRCLE);
        scroll_label(row, display_title(n), 52, 4, g_font, 118);
        if (!n.audio_key.empty()) label(row, ">", 176, 4);
        if (n.favorite) marker(row, 186, rowH / 2 - 4, 8, P(2));
        hline(scr, 4, ry + rowH - 1, 192);
    }

    if (app.page_count() > 1) {
        big_button(scr, LV_SYMBOL_UP, 8, 160, 44, 30, on_page_up, nullptr);
        big_button(scr, "REC", 64, 160, 72, 30, on_record, nullptr);
        big_button(scr, LV_SYMBOL_DOWN, 148, 160, 44, 30, on_page_down, nullptr);
    } else {
        big_button(scr, "REC", 58, 160, 84, 30, on_record, nullptr);
    }
}

void build_detail(lv_obj_t* scr) {
    title_bar(scr);
    const auto& sel = g_app->app.selected_note();
    if (!sel) { label(scr, "Note not found", 8, 60); return; }
    scroll_label(scr, display_title(*sel), 6, 24, g_font_big, 188);
    hline(scr, 6, 44, 188);

    if (sel->status != NoteStatus::Transcribed) {
        label(scr, "Pending transcription.", 6, 52, g_font, 188);
        label(scr, "Appears after sync.", 6, 74, g_font, 188);
    } else {
        // Full transcript fills the space between the title and the buttons; if it
        // overflows, auto-scroll vertically as an infinite loop (clipped). Tapping
        // the box pauses/resumes. The scroll itself is driven by tick_timer.
        const int ax = 6, ay = 48, aw = 188, ah = 106; // bottom = 154, buttons @156
        lv_obj_t* box = lv_obj_create(scr);
        lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE); // clips children to box
        lv_obj_set_pos(box, P(ax), P(ay));
        lv_obj_set_size(box, P(aw), P(ah));
        lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(box, 0, 0);
        lv_obj_set_style_radius(box, 0, 0);
        lv_obj_set_style_pad_all(box, 0, 0);
        lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(box, on_toggle_scroll, LV_EVENT_CLICKED, nullptr);
        std::string txt = sapiens::text::to_ascii(sel->transcript);
        auto mk = [&]() {
            lv_obj_t* l = lv_label_create(box);
            lv_obj_set_style_text_color(l, black(), 0);
            lv_obj_set_style_text_font(l, g_font, 0);
            lv_obj_set_width(l, P(aw));
            lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
            lv_label_set_text(l, txt.c_str());
            lv_obj_set_pos(l, 0, 0);
            return l;
        };
        g_tx1 = mk();
        lv_obj_update_layout(g_tx1);
        int content_h = lv_obj_get_height(g_tx1);
        int boxh = P(ah);
        if (content_h > boxh) {        // overflows -> infinite vertical loop
            g_tx_period = content_h + boxh; // gap = one box (blank, then from below)
            g_tx2 = mk();                   // second copy follows in from the bottom
            lv_obj_set_y(g_tx1, 0);
            lv_obj_set_y(g_tx2, g_tx_period);
        } else {
            g_tx2 = nullptr;
            g_tx_period = 0;
        }
    }

    // big icon-only actions: play | trash | back
    bool audio = !sel->audio_key.empty();
    if (audio) {
        big_button(scr, LV_SYMBOL_PLAY, 8, 156, 56, 38, on_open_player, nullptr);
        big_button(scr, LV_SYMBOL_TRASH, 72, 156, 56, 38, on_delete, nullptr);
        back_button(scr, 136, 156, 56, 38, on_back);
    } else {
        big_button(scr, LV_SYMBOL_TRASH, 8, 156, 90, 38, on_delete, nullptr);
        back_button(scr, 104, 156, 90, 38, on_back);
    }
}

void build_player(lv_obj_t* scr) {
    title_bar(scr);
    const auto& sel = g_app->app.selected_note();
    std::string title = sel ? display_title(*sel) : "Voice memo";
    scroll_label(scr, title, 6, 24, g_font_big, 188);
    hline(scr, 6, 44, 188);

    g_pl_state = label(scr, g_app->app.audio_playing() ? "Playing"
                       : g_app->app.audio_paused() ? "Paused" : "Stopped", 8, 56);

    char t[24];
    std::snprintf(t, sizeof(t), "%02d:%02d", g_app->app.audio_pos_secs() / 60,
                  g_app->app.audio_pos_secs() % 60);
    g_pl_pos = label(scr, t, 10, 84);
    std::snprintf(t, sizeof(t), "%02d:%02d", g_app->app.audio_total_secs() / 60,
                  g_app->app.audio_total_secs() % 60);
    label(scr, t, 150, 84);

    g_pl_bar = lv_bar_create(scr);
    lv_obj_set_size(g_pl_bar, P(180), P(10));
    lv_obj_set_pos(g_pl_bar, P(10), P(104));
    lv_obj_set_style_bg_color(g_pl_bar, white(), 0);
    lv_obj_set_style_border_color(g_pl_bar, black(), 0);
    lv_obj_set_style_border_width(g_pl_bar, 2, 0);
    lv_obj_set_style_radius(g_pl_bar, P(2), 0);
    lv_obj_set_style_bg_color(g_pl_bar, black(), LV_PART_INDICATOR);
    lv_obj_set_style_radius(g_pl_bar, 0, LV_PART_INDICATOR);
    lv_bar_set_range(g_pl_bar, 0, 1000);
    lv_bar_set_value(g_pl_bar, (int)(g_app->app.audio_progress() * 1000), LV_ANIM_OFF);

    lv_obj_t* pp = big_button(scr,
        g_app->app.audio_playing() ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY,
        14, 124, 84, 34, on_playpause, nullptr);
    g_pl_ppbtn_lbl = lv_obj_get_child(pp, 0);
    big_button(scr, LV_SYMBOL_STOP, 102, 124, 84, 34, on_stop, nullptr);
    back_button(scr, 70, 166, 60, 28, on_back);
}

void build_recording(lv_obj_t* scr) {
    title_bar(scr);
    lv_obj_t* band = lv_obj_create(scr);
    lv_obj_remove_flag(band, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(band, P(152), P(40));
    lv_obj_set_pos(band, P(24), P(42));
    lv_obj_set_style_bg_color(band, black(), 0);
    lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(band, P(6), 0);
    lv_obj_set_style_border_width(band, 0, 0);
    lv_obj_t* rl = lv_label_create(band);
    lv_obj_set_style_text_color(rl, white(), 0);
    lv_obj_set_style_text_font(rl, g_font_big, 0);
    lv_label_set_text(rl, "REC");
    lv_obj_center(rl);

    // Big recording timer (montserrat_48), horizontally centered.
    g_rec_timer = label(scr, "00:00", 0, 88, &lv_font_montserrat_48);
    lv_obj_align(g_rec_timer, LV_ALIGN_TOP_MID, 0, P(88));
    big_button(scr, "STOP", 24, 138, 152, 50, on_record);
}

// Centered text between the stepper buttons (label uses montserrat_14, centered).
void centered(lv_obj_t* scr, const std::string& t, int x, int y, int w, const lv_font_t* f);
// Small ‹ value › stepper beside a title — all medium font, proportional buttons.
void small_stepper(lv_obj_t* scr, int y, lv_event_cb_t dec, lv_event_cb_t inc,
                   const std::string& val) {
    const lv_font_t* f = &lv_font_montserrat_22;
    lv_obj_t* a = button(scr, LV_SYMBOL_LEFT, 110, y, 26, 26, dec, nullptr);
    lv_obj_set_style_text_font(lv_obj_get_child(a, 0), f, 0);
    lv_obj_t* b = button(scr, LV_SYMBOL_RIGHT, 168, y, 26, 26, inc, nullptr);
    lv_obj_set_style_text_font(lv_obj_get_child(b, 0), f, 0);
    centered(scr, val, 136, y + 3, 32, f);
}
void centered(lv_obj_t* scr, const std::string& t, int x, int y, int w, const lv_font_t* f) {
    lv_obj_t* l = label(scr, t, x, y, f, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
}

void build_settings(lv_obj_t* scr) {
    title_bar(scr);
    auto& app = g_app->app;
    // Same layout as the main simulator: labelled ‹ value › steppers.
    const lv_font_t* med = &lv_font_montserrat_22;
    label(scr, "Scroll speed", 6, 50, med);
    small_stepper(scr, 46, on_speed_down, on_speed_up,
                  std::to_string(app.scroll_speed()) + "/" +
                  std::to_string(app.scroll_speed_max()));
    label(scr, "Name format", 6, 98, med);
    small_stepper(scr, 94, on_format_prev, on_format_next,
                  std::to_string(app.name_format() + 1) + "/" +
                  std::to_string(app.name_format_count()));
    centered(scr, app.name_format_preview(), 6, 128, 188, &lv_font_montserrat_14);
    lv_obj_t* bk = back_button(scr, 72, 160, 56, 30, on_back);
    lv_obj_set_style_text_font(lv_obj_get_child(bk, 0), med, 0);
}

void build_confirm(lv_obj_t* scr) {
    title_bar(scr);
    label(scr, "Delete this note?", 6, 30, g_font_big);
    const auto& sel = g_app->app.selected_note();
    if (sel) lv_label_set_long_mode(
        label(scr, sel->title.empty() ? "Untitled" : sel->title, 6, 60, g_font, 188),
        LV_LABEL_LONG_SCROLL_CIRCULAR);
    label(scr, "This cannot be undone.", 6, 84);
    button(scr, "Delete", 18, 118, 78, 36, on_confirm_del, nullptr);
    button(scr, "Cancel", 104, 118, 78, 36, on_cancel_del, nullptr);
}

// Apply one animation frame (typewriter + blink) to the splash labels.
void update_splash() {
    if (!g_splash_a || !g_splash_b) return;
    auto f = sapiens::splash::compute(g_app->app.splash_elapsed_ms());
    lv_label_set_text(g_splash_a, f.line1.c_str());
    lv_label_set_text(g_splash_b, f.line2.c_str());
    lv_obj_align(g_splash_a, LV_ALIGN_TOP_MID, 0, P(84));  // wordmark below the logo
    lv_obj_align(g_splash_b, LV_ALIGN_TOP_MID, 0, P(116));
    lv_opa_t op = f.visible ? LV_OPA_COVER : LV_OPA_TRANSP;
    lv_obj_set_style_opa(g_splash_a, op, 0);
    lv_obj_set_style_opa(g_splash_b, op, 0);
}

// Boot splash: logo on top, then "Sapiens"/"Note" typed out + blinking, version
// at the bottom. The logo is the shared 1-bit bitmap, scaled into an lv_canvas so
// it stays crisp at any --scale.
void build_splash(lv_obj_t* scr) {
    static std::vector<std::uint8_t> logobuf;  // must outlive the canvas
    const int cw = P(sim::logo::W), chh = P(sim::logo::H);
    logobuf.assign(static_cast<std::size_t>(cw) * chh * 4 + 64, 0);
    lv_obj_t* cv = lv_canvas_create(scr);
    lv_canvas_set_buffer(cv, logobuf.data(), cw, chh, LV_COLOR_FORMAT_ARGB8888);
    lv_canvas_fill_bg(cv, lv_color_white(), LV_OPA_COVER);
    sim::logo::draw([&](int x, int y) {
        for (int dy = 0; dy < g_s; ++dy)
            for (int dx = 0; dx < g_s; ++dx)
                lv_canvas_set_px(cv, x * g_s + dx, y * g_s + dy, lv_color_black(), LV_OPA_COVER);
    });
    lv_obj_align(cv, LV_ALIGN_TOP_MID, 0, P(8));

    g_splash_a = label(scr, "", 0, 0, &lv_font_montserrat_28);
    g_splash_b = label(scr, "", 0, 0, &lv_font_montserrat_28);
    lv_obj_t* ver = label(scr, std::string("v") + SAPIENS_VERSION, 0, 0,
                          &lv_font_montserrat_14);
    lv_obj_align(ver, LV_ALIGN_BOTTOM_MID, 0, P(-6));   // version pinned at bottom
    update_splash();
}

// A Home-menu button with a bigger label (invert-on-tap comes from button()).
lv_obj_t* menu_button(lv_obj_t* p, const char* text, int x, int y, int w, int h,
                      lv_event_cb_t cb) {
    lv_obj_t* lbl = nullptr;
    lv_obj_t* b = button(p, "", x, y, w, h, cb, nullptr, &lbl);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_40, 0); // bigger text
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return b;
}

// Home menu: 2x2 grid of big buttons (Notes | Sync / Tags | Settings).
void build_home(lv_obj_t* scr) {
    title_bar(scr);
    const int lx = 6, rx = 102, w = 92, ty = 46, by = 118, h = 58;
    menu_button(scr, "Notes", lx, ty, w, h, on_go_notes);
    menu_button(scr, "Sync", rx, ty, w, h, on_go_sync);
    menu_button(scr, "Tags", lx, by, w, h, on_go_tags);
    menu_button(scr, "Settings", rx, by, w, h, on_settings);
}

void build_sync(lv_obj_t* scr) {
    title_bar(scr);
    label(scr, "Sync", 6, 26, g_font_big);
    label(scr, "Account: not signed in", 6, 62);
    label(scr, "Status: offline", 6, 82);
    label(scr, "Last sync: never", 6, 102);
    button(scr, "Sync now", 56, 120, 88, 30, on_sync_now, nullptr);
    back_button(scr, 70, 160, 60, 30, on_back);
}

void build_tags(lv_obj_t* scr) {
    title_bar(scr);
    label(scr, "Tags", 6, 26, g_font_big);
    auto tags = g_app->app.tags();
    if (tags.empty()) {
        label(scr, "No tags yet", 6, 64);
    } else {
        int y = 58;
        for (int i = 0; i < (int)tags.size() && i < 5; ++i, y += 20)
            label(scr, "# " + tags[i].name, 6, y);
    }
    back_button(scr, 70, 160, 60, 30, on_back);
}

void rebuild() {
    g_rec_timer = g_pl_state = g_pl_pos = g_pl_bar = g_pl_ppbtn_lbl = nullptr;
    g_splash_a = g_splash_b = nullptr;
    g_tx1 = g_tx2 = nullptr;
    g_tx_period = 0;
    lv_obj_t* scr = lv_screen_active();
    lv_obj_clean(scr);
    style_panel(scr);
    switch (g_app->app.screen()) {
        case Screen::Splash: build_splash(scr); break;
        case Screen::Home: build_home(scr); break;
        case Screen::NoteList: build_list(scr); break;
        case Screen::NoteDetail: build_detail(scr); break;
        case Screen::Recording: build_recording(scr); break;
        case Screen::Settings: build_settings(scr); break;
        case Screen::Player: build_player(scr); break;
        case Screen::ConfirmDelete: build_confirm(scr); break;
        case Screen::Sync: build_sync(scr); break;
        case Screen::Tags: build_tags(scr); break;
    }
}

// Live refresh (no rebuild -> scrolling labels keep animating).
void tick_timer(lv_timer_t*) {
    auto& app = g_app->app;
    if (app.maybe_advance_splash()) { rebuild(); return; } // splash done -> Home
    if (app.screen() == Screen::Splash) { update_splash(); return; } // animate

    // Poll the DB (~1.5s) so transcripts written by sapiens-web appear live; only
    // rebuild when the data changed, to avoid resetting the list's marquee.
    if (++g_sync_ticks >= 25) {
        g_sync_ticks = 0;
        auto s = app.screen();
        if (s == Screen::NoteList || s == Screen::NoteDetail) {
            app.refresh();
            app.reload_selected();
            std::string sig;
            for (const auto& n : app.visible_notes())
                sig += n.id + ":" + std::to_string(static_cast<int>(n.status)) + ";";
            if (const auto& sel = app.selected_note())
                sig += "sel:" + sel->id + ":" + std::to_string(sel->transcript.size());
            if (sig != g_data_sig) { g_data_sig = sig; rebuild(); return; }
        }
    }

    // Transcript infinite vertical scroll (paused via tap), driven each tick.
    if (app.screen() == Screen::NoteDetail && g_tx1 && g_tx_period > 0) {
        if (!app.transcript_paused()) app.tick_transcript_scroll(60); // ~tick period
        int div = 56 - app.scroll_speed() * 6; if (div < 10) div = 10;
        long sm = static_cast<long>(app.transcript_scroll_ms());
        long off = (sm * g_s / div) % g_tx_period; // ms-per-200px -> real px
        lv_obj_set_y(g_tx1, static_cast<int>(-off));
        if (g_tx2) lv_obj_set_y(g_tx2, static_cast<int>(-off + g_tx_period));
    }

    char buf[24];
    if (app.screen() == Screen::Recording && g_rec_timer) {
        int s = app.recording_elapsed_secs();
        std::snprintf(buf, sizeof(buf), "%02d:%02d", s / 60, s % 60);
        lv_label_set_text(g_rec_timer, buf);
    } else if (app.screen() == Screen::Player) {
        if (g_pl_state) lv_label_set_text(g_pl_state, app.audio_playing() ? "Playing"
                                          : app.audio_paused() ? "Paused" : "Stopped");
        if (g_pl_pos) {
            int s = app.audio_pos_secs();
            std::snprintf(buf, sizeof(buf), "%02d:%02d", s / 60, s % 60);
            lv_label_set_text(g_pl_pos, buf);
        }
        if (g_pl_bar) lv_bar_set_value(g_pl_bar, (int)(app.audio_progress() * 1000), LV_ANIM_OFF);
        if (g_pl_ppbtn_lbl) lv_label_set_text(g_pl_ppbtn_lbl, app.audio_playing() ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    }
}

// Built-in LVGL bitmap fonts (stable; no runtime TTF rasterizer -> no crashes).
const lv_font_t* load_font(int size) {
    return size >= 32 ? &lv_font_montserrat_36 : &lv_font_montserrat_28;
}

std::string seed_demo(App& a) {
    Note typed = a.notes.create_note("Buy milk and eggs");
    a.notes.set_favorite(typed.id, true);
    Tag t = a.notes.create_tag("errands");
    a.notes.add_tag(typed.id, t.id);
    a.notes.apply_transcription(typed.id, "Buy milk and eggs on the way home.",
                                "Shopping reminder.", {"milk", "eggs"}, "en");
    a.app.start_recording();
    a.app.stop_recording();
    std::string rec = a.audioSvc.last_clip()->note_id; // title = date-time
    a.notes.apply_transcription(
        rec, "Remember to call the landlord about the leaking tap in the bathroom.",
        "Call the landlord about the leaking tap.", {"landlord", "tap"}, "en");
    Note longn = a.notes.create_note(
        "Project kickoff brainstorm with the whole design team");
    a.notes.apply_transcription(longn.id, "...", "Kickoff notes.", {}, "en");
    a.notes.create_note("Meeting idea");
    a.app.refresh();
    return rec;
}

std::vector<std::uint32_t> g_fb; // ARGB8888 framebuffer (LVGL renders into this)
int g_fbw = 0, g_fbh = 0;
// mouse state (fed to LVGL via an indev read callback)
int g_mx = 0, g_my = 0;
bool g_mdown = false;

// LVGL flush -> copy the rendered region into our ARGB framebuffer.
void fb_flush(lv_display_t* d, const lv_area_t* area, uint8_t* px) {
    const int aw = area->x2 - area->x1 + 1;
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x) {
            int si = ((y - area->y1) * aw + (x - area->x1)) * 4;
            if (y >= 0 && y < g_fbh && x >= 0 && x < g_fbw)
                g_fb[y * g_fbw + x] =
                    0xFF000000u | (px[si + 2] << 16) | (px[si + 1] << 8) | px[si + 0];
        }
    lv_display_flush_ready(d);
}

void mouse_read(lv_indev_t*, lv_indev_data_t* data) {
    data->point.x = g_mx;
    data->point.y = g_my;
    data->state = g_mdown ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

int run_shot(const std::string& path, const std::string& screen) {
    g_fbw = P(200); g_fbh = P(200);
    g_fb.assign(g_fbw * g_fbh, 0xFFFFFFu);
    static std::vector<std::uint8_t> buf(g_fbw * g_fbh * 4);
    lv_display_t* d = lv_display_create(g_fbw, g_fbh);
    lv_display_set_buffers(d, buf.data(), nullptr, buf.size(), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(d, fb_flush);

    static host::SystemClock ec;
    static host::FakeAudio fake{ec};      // never touches the mic
    static InMemoryNoteRepository shot_repo; // shots/headless: synthetic notes
    static App a{shot_repo, fake};
    g_app = &a;
    a.app.set_name_format(load_name_format()); // screenshots reflect chosen format
    a.app.set_scroll_speed(load_scroll_speed());
    for (int i = 1; i <= 4; ++i) a.notes.create_note("Older note " + std::to_string(i));
    std::string rec = seed_demo(a);
    if (std::getenv("SAPIENS_LONGTX")) { // dev: long transcript to test vertical scroll
        std::string big;
        for (int i = 1; i <= 16; ++i)
            big += "Line " + std::to_string(i) +
                   ": remember to call the landlord about the leaking tap. ";
        a.notes.apply_transcription(rec, big, "", {}, "en");
        a.app.refresh();
    }
    if (screen == "detail") a.app.open_note(rec);
    else if (screen == "player") { a.app.open_note(rec); a.app.open_player(); }
    else if (screen == "confirm") { a.app.open_note(rec); a.app.request_delete(); }
    else if (screen == "settings") a.app.open_settings();
    else if (screen == "recording") a.app.start_recording();
    else if (screen == "splash") {
        a.app.show_splash();
        const char* ms = std::getenv("SAPIENS_SPLASH_MS");
        a.app.debug_set_splash_elapsed(ms ? std::atoi(ms) : 900);
    }
    else if (screen == "home") a.app.go_home();
    else if (screen == "sync") a.app.go_sync();
    else if (screen == "tags") a.app.go_tags();
    else a.app.go_note_list(); // default "list"
    rebuild();
    if (screen == "home") if (const char* p = std::getenv("SAPIENS_PRESS")) {
        std::string s = p; // screenshot the invert flash on a Home button
        int idx = s == "notes" ? 3 : s == "sync" ? 4 : s == "tags" ? 5 : 6;
        lv_obj_t* b = lv_obj_get_child(lv_screen_active(), idx);
        if (b) invert_button(b, true);
    }
    lv_refr_now(d);

    // Stress: run many animation frames so scrolling labels re-rasterize glyphs
    // (this previously exhausted LVGL's 64KB pool -> stb assert). Survival = fixed.
    if (std::getenv("SAPIENS_STRESS")) {
        for (int i = 0; i < 1500; ++i) { lv_timer_handler(); SDL_Delay(1); }
        std::printf("stress survived (no assert)\n");
    }
    if (const char* am = std::getenv("SAPIENS_ANIM_MS")) { // dev: advance animations
        lv_timer_create(tick_timer, 60, nullptr); // the shot has no tick timer
        int ms = std::atoi(am);
        for (int e = 0; e < ms; e += 16) { lv_timer_handler(); SDL_Delay(16); }
        lv_refr_now(d);
    }

    if (path.empty()) return 0; // headless: render only, no file
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return 1;
    std::fprintf(f, "P6\n%d %d\n255\n", g_fbw, g_fbh);
    for (int i = 0; i < g_fbw * g_fbh; ++i) {
        unsigned char v = g_fb[i] & 0xFF; // monochrome UI
        std::fputc(v, f); std::fputc(v, f); std::fputc(v, f);
    }
    std::fclose(f);
    std::printf("wrote %s (%dx%d)\n", path.c_str(), g_fbw, g_fbh);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    bool headless = false;
    std::string shot, shot_screen = "list";
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0) headless = true;
        else if (std::strcmp(argv[i], "--shot") == 0 && i + 1 < argc) shot = argv[++i];
        else if (std::strcmp(argv[i], "--screen") == 0 && i + 1 < argc) shot_screen = argv[++i];
        else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) g_s = std::atoi(argv[++i]);
    }
    if (g_s < 1) g_s = 1;

    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return SDL_GetTicks(); });
    g_font = load_font(P(15));
    g_font_big = load_font(P(18));
    make_back_image();

    if (!shot.empty()) return run_shot(shot, shot_screen);
    if (headless) {                       // render off-screen, no window, no mic
        run_shot("", "list");
        std::printf("sapiens-sim-lvgl --headless: LVGL %d.%d.%d OK\n",
                    LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
        return 0;
    }

    // Drive LVGL through a buffer display presented by SDL ourselves (the proven
    // path used by the main simulator). We do NOT use lv_sdl_window_create, which
    // crashes during macOS video init on some setups.
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    g_fbw = P(200);
    g_fbh = P(200);
    g_fb.assign(g_fbw * g_fbh, 0xFFFFFFFFu);
    static std::vector<std::uint8_t> draw_buf(g_fbw * g_fbh * 4);

    lv_display_t* d = lv_display_create(g_fbw, g_fbh);
    lv_display_set_buffers(d, draw_buf.data(), nullptr, draw_buf.size(),
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(d, fb_flush);
    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, mouse_read);

    SDL_Window* win = SDL_CreateWindow("SapiensNote Simulator (LVGL)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, g_fbw, g_fbh, 0);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, g_fbw, g_fbh);

    static sim::SdlAudio audio(data_dir() + "/audio"); // REAL mic capture/playback
    static SqliteNoteRepository win_repo(data_dir() + "/sapiensnote.db"); // SHARED DB
    static App a{win_repo, audio};
    g_app = &a;
    a.app.set_name_format(load_name_format());
    a.app.set_scroll_speed(load_scroll_speed());
    a.app.refresh(); // start empty: only real recordings appear (no examples)
    lv_timer_create(tick_timer, 60, nullptr); // fast enough for the splash typing
    rebuild();

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_MOUSEMOTION) { g_mx = e.motion.x; g_my = e.motion.y; }
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                g_mx = e.button.x; g_my = e.button.y; g_mdown = true;
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                g_mdown = false;
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_q) {
                running = false;
            }
        }
        lv_timer_handler();
        SDL_UpdateTexture(tex, nullptr, g_fb.data(), g_fbw * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
        SDL_Delay(5);
    }
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
