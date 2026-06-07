#pragma once
// Immediate-mode renderer: paints AppController state onto a Canvas using the
// proportional anti-aliased font, and resolves taps to actions. Long names scroll
// (marquee, speed from Settings). Shared by the simulator and the firmware.
#include <cstdio>
#include <string>

#include "Canvas.h"
#include "LogoBitmap.h"
#include "sapiens/AppController.h"
#include "sapiens/AsciiText.h"
#include "sapiens/Format.h"
#include "sapiens/SplashAnim.h"
#include "sapiens/Version.h"

namespace sapiens::sim {

inline std::string display_title(const sapiens::Note& n) {
    std::string t = sapiens::text::to_ascii(n.title.empty() ? "Untitled" : n.title);
    if (n.audio_secs > 0) t += "  " + sapiens::fmt::format_duration(n.audio_secs);
    return t;
}

namespace layout {
constexpr int kTitleH = 18;
constexpr int kSubH = 38;          // subheader (total / page) bottom line
constexpr int kRowTop = 40;
constexpr int kRowH = 23;          // 5 rows
constexpr int kRows = 5;
constexpr int kBarY = 159, kBarH = 32; // bottom control bar (below the last row)
constexpr int kSetX = 182, kSetY = 3, kSetW = 14, kSetH = 12;
// bottom controls
constexpr int kBtnY = 168, kBtnH = 26;
constexpr int kUpX = 6, kDnX = 150, kNavW = 44;
// recording Stop = size of the REC rectangle
constexpr int kRecBandX = 24, kRecBandY = 42, kRecBandW = 152, kRecBandH = 40;
constexpr int kStopX = 24, kStopY = 138, kStopW = 152, kStopH = 50;
// detail action row
constexpr int kPlayX = 8, kPlayY = 168, kPlayW = 92, kPlayH = 26;
// player
constexpr int kPbX = 10, kPbY = 104, kPbW = 180, kPbH = 8;
constexpr int kPPx = 14, kPPy = 124, kPPw = 84, kPPh = 34;
constexpr int kStX = 102, kStY = 124, kStW = 84, kStH = 34;
constexpr int kBkX = 70, kBkY = 168, kBkW = 60, kBkH = 24;
// settings scroll-speed slider track (same line as the "Speed" label)
constexpr int kSldX = 64, kSldY = 56, kSldW = 124, kSldH = 8;
constexpr int kSldHitY = 48, kSldHitH = 24;
} // namespace layout

namespace dlg {
constexpr int kDelX = 18, kDelY = 118, kDelW = 78, kDelH = 36;
constexpr int kCanX = 104, kCanY = 118, kCanW = 78, kCanH = 36;
} // namespace dlg

struct Hit {
    enum class Type { None, Record, OpenNote, Back, Settings, OpenPlayer,
                      PlayPause, Stop, Delete, ConfirmDelete, CancelDelete,
                      PageUp, PageDown, SpeedDown, SpeedUp, FormatPrev,
                      FormatNext, GoNotes, GoSync, GoTags, SyncNow,
                      ToggleScroll } type{Type::None};
    std::string note_id;
    int value{-1}; // for ScrollSpeed
    int rx{-1}, ry{-1}, rw{-1}, rh{-1}; // button bounds (for the click-invert flash)
    bool is_button() const { return rx >= 0; }
};

inline std::string mmss(int s) {
    char b[8];
    std::snprintf(b, sizeof(b), "%02d:%02d", s / 60, s % 60);
    return b;
}

class Renderer {
public:
    void render(Canvas& c, const sapiens::AppController& app, int battery_pct,
                std::uint32_t now_ms = 0) {
        now_ = now_ms;
        // marquee speed (1..4): 1 = slow (big divisor), 4 = fast (small divisor),
        // recalibrated slower so even the max isn't too fast.
        scroll_div_ = 56 - app.scroll_speed() * 6;
        if (scroll_div_ < 10) scroll_div_ = 10;
        c.clear(0);
        if (app.screen() == sapiens::Screen::Splash) { draw_splash(c, app); return; }
        // The note list owns its whole header (tall Menu button + battery + sep).
        if (app.screen() == sapiens::Screen::NoteList) {
            draw_note_list(c, app, battery_pct);
            return;
        }
        draw_title_bar(c, battery_pct);
        switch (app.screen()) {
            case sapiens::Screen::Home: draw_home(c, app); break;
            case sapiens::Screen::NoteList: break; // handled above
            case sapiens::Screen::Recording: draw_recording(c, app); break;
            case sapiens::Screen::NoteDetail: draw_detail(c, app); break;
            case sapiens::Screen::Settings: draw_settings(c, app); break;
            case sapiens::Screen::Player: draw_player(c, app); break;
            case sapiens::Screen::ConfirmDelete: draw_confirm(c, app); break;
            case sapiens::Screen::Sync: draw_sync(c, app); break;
            case sapiens::Screen::Tags: draw_tags(c, app); break;
            case sapiens::Screen::Splash: break; // handled above
        }
    }

    // Home menu: 2x2 grid of big buttons (kept in sync with draw_home).
    static constexpr int kHmLX = 6, kHmRX = 102, kHmW = 92;
    static constexpr int kHmTY = 46, kHmBY = 118, kHmH = 58;
    // Note-list "Menu" button: tall, spanning both header lines on the right,
    // with the battery before it and a vertical separator line.
    static constexpr int kListMenuW = 46, kListMenuH = 32, kListMenuY = 3;
    static constexpr int kListMenuX = Canvas::W - 4 - kListMenuW; // 150

    Hit hittest(int x, int y, const sapiens::AppController& app) const {
        using namespace layout;
        // Build a button hit carrying its bounds (for the click-invert flash).
        auto B = [&](Hit::Type t, int bx, int by, int bw, int bh) -> Hit {
            if (!in(x, y, bx, by, bw, bh)) return {};
            return {t, "", -1, bx, by, bw, bh};
        };
        switch (app.screen()) {
            case sapiens::Screen::Splash:
                return {}; // taps ignored; auto-advances
            case sapiens::Screen::Home:
                if (auto h = B(Hit::Type::GoNotes, kHmLX, kHmTY, kHmW, kHmH); h.type != Hit::Type::None) return h;
                if (auto h = B(Hit::Type::GoSync, kHmRX, kHmTY, kHmW, kHmH); h.type != Hit::Type::None) return h;
                if (auto h = B(Hit::Type::GoTags, kHmLX, kHmBY, kHmW, kHmH); h.type != Hit::Type::None) return h;
                if (auto h = B(Hit::Type::Settings, kHmRX, kHmBY, kHmW, kHmH); h.type != Hit::Type::None) return h;
                return {};
            case sapiens::Screen::Sync:
                if (auto h = B(Hit::Type::SyncNow, 56, 120, 88, 30); h.type != Hit::Type::None) return h;
                if (auto h = B(Hit::Type::Back, 70, 160, 60, 30); h.type != Hit::Type::None) return h;
                return {};
            case sapiens::Screen::Tags:
                return B(Hit::Type::Back, 70, 160, 60, 30);
            case sapiens::Screen::Recording:
                return B(Hit::Type::Record, kStopX, kStopY, kStopW, kStopH);
            case sapiens::Screen::Player:
                if (auto h = B(Hit::Type::PlayPause, kPPx, kPPy, kPPw, kPPh); h.type != Hit::Type::None) return h;
                if (auto h = B(Hit::Type::Stop, kStX, kStY, kStW, kStH); h.type != Hit::Type::None) return h;
                if (auto h = B(Hit::Type::Back, kBkX, kBkY, kBkW, kBkH); h.type != Hit::Type::None) return h;
                return {};
            case sapiens::Screen::NoteDetail: {
                const auto& sel = app.selected_note();
                bool audio = sel && !sel->audio_key.empty();
                const int by = 156, bh = 38;
                if (audio) {
                    if (auto h = B(Hit::Type::OpenPlayer, 8, by, 56, bh); h.type != Hit::Type::None) return h;
                    if (auto h = B(Hit::Type::Delete, 72, by, 56, bh); h.type != Hit::Type::None) return h;
                    if (auto h = B(Hit::Type::Back, 136, by, 56, bh); h.type != Hit::Type::None) return h;
                } else {
                    if (auto h = B(Hit::Type::Delete, 8, by, 90, bh); h.type != Hit::Type::None) return h;
                    if (auto h = B(Hit::Type::Back, 104, by, 90, bh); h.type != Hit::Type::None) return h;
                }
                // Tap the transcript area -> pause/resume the auto-scroll.
                if (in(x, y, 6, 44, Canvas::W - 12, 110)) return {Hit::Type::ToggleScroll};
                return {};
            }
            case sapiens::Screen::ConfirmDelete:
                if (auto h = B(Hit::Type::ConfirmDelete, dlg::kDelX, dlg::kDelY, dlg::kDelW, dlg::kDelH); h.type != Hit::Type::None) return h;
                return B(Hit::Type::CancelDelete, dlg::kCanX, dlg::kCanY, dlg::kCanW, dlg::kCanH);
            case sapiens::Screen::Settings: {
                if (auto h = B(Hit::Type::SpeedDown, 110, 46, 26, 26); h.type != Hit::Type::None) return h;
                if (auto h = B(Hit::Type::SpeedUp, 168, 46, 26, 26); h.type != Hit::Type::None) return h;
                if (auto h = B(Hit::Type::FormatPrev, 110, 94, 26, 26); h.type != Hit::Type::None) return h;
                if (auto h = B(Hit::Type::FormatNext, 168, 94, 26, 26); h.type != Hit::Type::None) return h;
                return B(Hit::Type::Back, 72, 160, 56, 30);
            }
            case sapiens::Screen::NoteList:
            default: break;
        }
        // Note list
        if (auto h = B(Hit::Type::Back, kListMenuX, kListMenuY, kListMenuW, kListMenuH); h.type != Hit::Type::None) return h; // "Menu" -> Home
        bool paged = app.page_count() > 1;
        if (paged) {
            if (auto h = B(Hit::Type::PageUp, 6, kBarY, 46, kBarH); h.type != Hit::Type::None) return h;
            if (auto h = B(Hit::Type::PageDown, 142, kBarY, 46, kBarH); h.type != Hit::Type::None) return h;
        }
        int rx = paged ? 64 : 56, rw = paged ? 72 : 88;
        if (auto h = B(Hit::Type::Record, rx, kBarY, rw, kBarH); h.type != Hit::Type::None) return h;
        auto notes = app.page_notes();
        for (int i = 0; i < (int)notes.size(); ++i)
            if (in(x, y, 0, kRowTop + i * kRowH, Canvas::W, kRowH))
                return {Hit::Type::OpenNote, notes[i].id};
        return {};
    }

private:
    std::uint32_t now_{0};
    int scroll_div_{16};

    static bool in(int x, int y, int rx, int ry, int rw, int rh) {
        return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
    }
    static int cy_for(int band_y, int h) { return band_y + (h - Canvas::line_height()) / 2; }

    // Raised "3D" button frame: a soft drop shadow + a white face + black border.
    // The shadow is simulator-only eye-candy (the e-paper HAL thresholds to 1-bit).
    static constexpr int kBtnR = 7;
    void frame3d(Canvas& c, int x, int y, int w, int h, int r = kBtnR) {
        c.blend_fill_round_rect(x + 2, y + 3, w, h, 255, 48, r); // drop shadow
        c.fill_round_rect(x, y, w, h, 0, r);                     // white face
        c.draw_round_rect(x, y, w, h, 1, r);                     // border
    }

    void button(Canvas& c, int x, int y, int w, int h, const std::string& label) {
        frame3d(c, x, y, w, h);
        c.draw_text(x + (w - Canvas::text_width(label)) / 2, cy_for(y, h), label, 1);
    }
    // Button with crisp big-font label (no upscaling -> not jagged).
    void big_btn(Canvas& c, int x, int y, int w, int h, const std::string& t) {
        frame3d(c, x, y, w, h);
        c.draw_text_big(x + (w - Canvas::text_width_big(t)) / 2,
                        y + (h - Canvas::line_height_big()) / 2, t, 1);
    }

    // --- icons (drawn centered in a button rect) ----------------------------
    void ic_play(Canvas& c, int cx, int cy, int s) { c.tri_right(cx - s / 2, cy, s, s + 2, 1); }
    void ic_pause(Canvas& c, int cx, int cy, int s) {
        c.fill_rect(cx - s / 2, cy - s / 2, 4, s, 1);
        c.fill_rect(cx + s / 2 - 4, cy - s / 2, 4, s, 1);
    }
    void ic_stop(Canvas& c, int cx, int cy, int s) { c.fill_rect(cx - s / 2, cy - s / 2, s, s, 1); }
    void ic_up(Canvas& c, int cx, int cy, int s) { c.tri_up(cx, cy - s / 2, s + 2, s, 1); }
    void ic_down(Canvas& c, int cx, int cy, int s) { c.tri_down(cx, cy - s / 2, s + 2, s, 1); }
    void ic_trash(Canvas& c, int cx, int cy, int w, int h) {
        int x = cx - w / 2, y = cy - h / 2;
        c.fill_rect(x - 2, y, w + 4, 2, 1);             // lid
        c.fill_rect(cx - 3, y - 3, 6, 2, 1);            // handle
        c.draw_rect(x, y + 4, w, h - 4, 1);             // body
        for (int k = -1; k <= 1; ++k) c.fill_rect(cx + k * (w / 4), y + 8, 1, h - 13, 1);
    }
    // framed icon button (raised 3D, like the text buttons)
    void ibtn(Canvas& c, int x, int y, int w, int h) { frame3d(c, x, y, w, h); }

    void draw_title_bar(Canvas& c, int battery) {
        c.draw_text(4, 1, "Sapiens Note", 1);
        if (battery >= 0) {
            std::string b = std::to_string(battery) + "%";
            c.draw_text(Canvas::W - 4 - Canvas::text_width(b), 1, b, 1);
        }
        c.hline(0, layout::kTitleH, Canvas::W, 1);
    }

    // Boot splash: "Sapiens" / "Notes" typed out letter by letter, then blinking.
    void draw_splash(Canvas& c, const sapiens::AppController& app) {
        auto f = sapiens::splash::compute(app.splash_elapsed_ms());
        // Logo at the top — always visible (doesn't blink with the wordmark).
        int lx = (Canvas::W - logo::W) / 2, ly = 8;
        logo::draw([&](int x, int y) { c.set(lx + x, ly + y, 1); });
        // Build version pinned at the bottom (stays put across blink frames).
        std::string v = std::string("v") + SAPIENS_VERSION;
        c.draw_text((Canvas::W - Canvas::text_width(v)) / 2, Canvas::H - 14, v, 1);
        if (!f.visible) return; // blink "off" frame -> wordmark hidden (logo stays)
        int y1 = 86, y2 = y1 + 31;
        c.draw_text_big((Canvas::W - Canvas::text_width_big(f.line1)) / 2, y1, f.line1, 1);
        c.draw_text_big((Canvas::W - Canvas::text_width_big(f.line2)) / 2, y2, f.line2, 1);
    }

    // A Home-menu button with a bigger (medium font) label.
    void menu_btn(Canvas& c, int x, int y, int w, int h, const std::string& label) {
        frame3d(c, x, y, w, h);
        c.draw_text_med(x + (w - Canvas::text_width_med(label)) / 2,
                        y + (h - Canvas::line_height_med()) / 2, label, 1);
    }

    // Home menu: 2x2 grid of big buttons (Notes | Sync / Tags | Settings).
    void draw_home(Canvas& c, const sapiens::AppController&) {
        menu_btn(c, kHmLX, kHmTY, kHmW, kHmH, "Notes");
        menu_btn(c, kHmRX, kHmTY, kHmW, kHmH, "Sync");
        menu_btn(c, kHmLX, kHmBY, kHmW, kHmH, "Tags");
        menu_btn(c, kHmRX, kHmBY, kHmW, kHmH, "Settings");
    }

    // Small battery glyph whose fill reflects the charge level.
    void battery_icon(Canvas& c, int x, int y, int w, int h, int pct) {
        c.draw_round_rect(x, y, w, h, 1, 2);        // body
        c.fill_rect(x + w, y + h / 2 - 2, 2, 4, 1); // terminal nub
        int p = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
        int inner = w - 4;
        c.fill_rect(x + 2, y + 2, inner * p / 100, h - 4, 1); // proportional fill
    }

    void draw_sync(Canvas& c, const sapiens::AppController&) {
        c.draw_text_big(6, 26, "Sync", 1);
        c.draw_text(6, 62, "Account: not signed in", 1);
        c.draw_text(6, 82, "Status: offline", 1);
        c.draw_text(6, 102, "Last sync: never", 1);
        button(c, 56, 120, 88, 30, "Sync now");
        button(c, 70, 160, 60, 30, "Back");
    }

    void draw_tags(Canvas& c, const sapiens::AppController& app) {
        c.draw_text_big(6, 26, "Tags", 1);
        auto tags = app.tags();
        if (tags.empty()) {
            c.draw_text(6, 64, "No tags yet", 1);
        } else {
            int y = 58;
            for (int i = 0; i < (int)tags.size() && i < 5; ++i, y += 20)
                c.draw_text(6, y, "# " + tags[i].name, 1);
        }
        button(c, 70, 160, 60, 30, "Back");
    }

    void draw_note_list(Canvas& c, const sapiens::AppController& app, int battery) {
        // Custom 2-line header: brand (line 1) + counter/pagination (line 2) on
        // the left; on the right a tall "Menu" button, the battery before it, and
        // a vertical separator line.
        c.draw_text(4, 1, "Sapiens Note", 1);
        std::string sub = std::to_string(app.total_notes()) + " notes";
        if (app.page_count() > 1)
            sub += "   " + std::to_string(app.list_page() + 1) + "/" +
                   std::to_string(app.page_count());
        c.draw_text(6, 22, sub, 1);
        // tall Menu button on the right; battery icon above its percentage,
        // both right-aligned just before the button (no separator line).
        button(c, kListMenuX, kListMenuY, kListMenuW, kListMenuH, "Menu");
        if (battery >= 0) {
            int rightEdge = kListMenuX - 6;
            std::string b = std::to_string(battery) + "%";
            c.draw_text(rightEdge - Canvas::text_width(b), 20, b, 1);
            battery_icon(c, rightEdge - 22 - 2, 4, 22, 10, battery);
        }
        c.hline(0, layout::kSubH, Canvas::W, 1);

        auto notes = app.page_notes();
        if (notes.empty()) {
            c.draw_text(40, 96, "No notes yet", 1);
            c.draw_text(34, 114, "Tap Rec below", 1);
        }
        for (int i = 0; i < (int)notes.size(); ++i) {
            const auto& n = notes[i];
            int ry = layout::kRowTop + i * layout::kRowH;
            int mid = ry + layout::kRowH / 2;
            int ty = cy_for(ry, layout::kRowH);
            // per-row counter: 0001, 0002, ...
            char num[8];
            std::snprintf(num, sizeof(num), "%04d", app.page_first_index() + i + 1);
            c.draw_text(6, ty, num, 1);
            if (n.status != sapiens::NoteStatus::Transcribed) c.fill_disc(44, mid, 3, 1);
            if (n.favorite) c.fill_diamond(Canvas::W - 12, mid, 4, 1);
            if (!n.audio_key.empty()) {
                int px = Canvas::W - (n.favorite ? 30 : 14);
                for (int k = 0; k < 6; ++k)
                    for (int j = -k; j <= k; ++j) c.set(px + (5 - k), mid + j, 1);
            }
            int right_pad = (n.favorite ? 22 : 8) + (n.audio_key.empty() ? 0 : 14);
            int tx = 52;
            c.draw_text_scroll(tx, ty, Canvas::W - tx - right_pad, display_title(n),
                               now_, scroll_div_, 1);
            c.hline(4, ry + layout::kRowH - 1, Canvas::W - 8, 1);
        }

        // bottom controls: triangle Up / big REC / triangle Dn
        using namespace layout;
        bool paged = app.page_count() > 1;
        if (paged) {
            ibtn(c, 6, kBarY, 46, kBarH); ic_up(c, 29, kBarY + kBarH / 2, 14);
            ibtn(c, 142, kBarY, 46, kBarH); ic_down(c, 165, kBarY + kBarH / 2, 14);
            big_btn(c, 64, kBarY, 72, kBarH, "REC");
        } else {
            big_btn(c, 56, kBarY, 88, kBarH, "REC");
        }
    }

    void draw_recording(Canvas& c, const sapiens::AppController& app) {
        using namespace layout;
        c.fill_rect(kRecBandX, kRecBandY, kRecBandW, kRecBandH, 1);
        std::string rec = "REC";
        c.draw_text_big((Canvas::W - Canvas::text_width_big(rec)) / 2,
                        kRecBandY + (kRecBandH - Canvas::line_height_big()) / 2, rec, 0);
        // big recording timer
        std::string t = mmss(app.recording_elapsed_secs());
        c.draw_text_huge((Canvas::W - Canvas::text_width_huge(t)) / 2, 86, t, 1);
        big_btn(c, kStopX, kStopY, kStopW, kStopH, "STOP"); // big crisp Stop
    }

    void draw_detail(Canvas& c, const sapiens::AppController& app) {
        const auto& sel = app.selected_note();
        if (!sel) { c.draw_text(8, 60, "Note not found", 1); return; }
        c.draw_text_scroll(6, 22, Canvas::W - 12, display_title(*sel), now_, scroll_div_, 1);
        c.hline(6, 40, Canvas::W - 12, 1);
        if (sel->status != sapiens::NoteStatus::Transcribed) {
            c.draw_text(6, 60, "Pending transcription.", 1);
            c.draw_text(6, 78, "Appears after sync.", 1);
        } else {
            // Full transcript fills the space between the title and the buttons,
            // auto-scrolling vertically (infinite loop) at the configured speed.
            draw_vscroll_text(c, 6, 44, Canvas::W - 12, 110,
                              sapiens::text::to_ascii(sel->transcript),
                              app.transcript_scroll_ms());
        }
        // big icon-only actions: play | trash | back
        const int by = 156, bh = 38;
        bool audio = !sel->audio_key.empty();
        if (audio) {
            ibtn(c, 8, by, 56, bh);   ic_play(c, 36, by + bh / 2, 18);
            ibtn(c, 72, by, 56, bh);  ic_trash(c, 100, by + bh / 2, 16, 18);
            button(c, 136, by, 56, bh, "Back");
        } else {
            ibtn(c, 8, by, 90, bh);   ic_trash(c, 53, by + bh / 2, 16, 18);
            button(c, 104, by, 90, bh, "Back");
        }
    }

    void draw_player(Canvas& c, const sapiens::AppController& app) {
        using namespace layout;
        const auto& sel = app.selected_note();
        std::string title = sel ? display_title(*sel) : "Voice memo";
        c.draw_text_scroll(6, 24, Canvas::W - 12, title, now_, scroll_div_, 1);
        c.hline(6, 44, Canvas::W - 12, 1);
        std::string state = app.audio_playing() ? "Playing"
                            : app.audio_paused() ? "Paused" : "Stopped";
        c.draw_text(8, 60, state, 1);
        c.draw_rect(kPbX, kPbY, kPbW, kPbH, 1);
        int fillw = (int)((kPbW - 2) * app.audio_progress());
        if (fillw > 0) c.fill_rect(kPbX + 1, kPbY + 1, fillw, kPbH - 2, 1);
        c.draw_text(kPbX, kPbY - 16, mmss(app.audio_pos_secs()), 1);
        std::string tot = mmss(app.audio_total_secs());
        c.draw_text(kPbX + kPbW - Canvas::text_width(tot), kPbY - 16, tot, 1);
        ibtn(c, kPPx, kPPy, kPPw, kPPh);
        if (app.audio_playing()) ic_pause(c, kPPx + kPPw / 2, kPPy + kPPh / 2, 16);
        else ic_play(c, kPPx + kPPw / 2, kPPy + kPPh / 2, 16);
        ibtn(c, kStX, kStY, kStW, kStH);
        ic_stop(c, kStX + kStW / 2, kStY + kStH / 2, 16);
        button(c, kBkX, kBkY, kBkW, kBkH, "Back");
    }

    // Small ‹ value › stepper placed beside a title. Buttons are text-sized.
    void small_stepper(Canvas& c, int x, int y, const std::string& val) {
        ibtn(c, x, y, 26, 26);      c.tri_left(x + 13, y + 13, 8, 11, 1);
        ibtn(c, x + 58, y, 26, 26); c.tri_right(x + 71, y + 13, 8, 11, 1);
        int cx = x + 42; // centered between the two buttons
        c.draw_text(cx - Canvas::text_width(val) / 2,
                    y + (26 - Canvas::line_height()) / 2, val, 1);
    }

    void draw_settings(Canvas& c, const sapiens::AppController& app) {
        c.draw_text(6, 50, "Scroll speed", 1);
        small_stepper(c, 110, 46, std::to_string(app.scroll_speed()) + "/" +
                                  std::to_string(app.scroll_speed_max()));
        c.draw_text(6, 98, "Name format", 1);
        small_stepper(c, 110, 94, std::to_string(app.name_format() + 1) + "/" +
                                  std::to_string(app.name_format_count()));
        c.draw_text(6, 128, Canvas::elide(app.name_format_preview(), Canvas::W - 12), 1);
        button(c, 72, 160, 56, 30, "Back");
    }

    void draw_confirm(Canvas& c, const sapiens::AppController& app) {
        const auto& sel = app.selected_note();
        c.draw_text(6, 30, "Delete this note?", 1);
        if (sel) c.draw_text_scroll(6, 60, Canvas::W - 12,
                                    sel->title.empty() ? "Untitled" : sel->title,
                                    now_, scroll_div_, 1);
        c.draw_text(6, 86, "This cannot be undone.", 1);
        button(c, dlg::kDelX, dlg::kDelY, dlg::kDelW, dlg::kDelH, "Delete");
        button(c, dlg::kCanX, dlg::kCanY, dlg::kCanW, dlg::kCanH, "Cancel");
    }

    // Greedy word-wrap into lines that fit `max_w`.
    static std::vector<std::string> wrap_lines(const std::string& text, int max_w) {
        std::vector<std::string> lines;
        std::string line, word;
        auto push_word = [&]() {
            if (word.empty()) return;
            std::string cand = line.empty() ? word : line + " " + word;
            if (Canvas::text_width(cand) > max_w && !line.empty()) {
                lines.push_back(line);
                line = word;
            } else {
                line = cand;
            }
            word.clear();
        };
        for (char ch : text) {
            if (ch == ' ') push_word();
            else if (ch == '\n') { push_word(); lines.push_back(line); line.clear(); }
            else word += ch;
        }
        push_word();
        if (!line.empty()) lines.push_back(line);
        return lines;
    }

    // Draw `text` wrapped inside the box (x,y0,w,h). If it overflows, auto-scroll
    // vertically as an INFINITE loop (the text exits the top and re-enters from
    // the bottom, never bouncing) at the configured speed, driven by the scroll
    // clock `sms` (paused when the user taps the text). Clipped to the box.
    void draw_vscroll_text(Canvas& c, int x, int y0, int w, int h,
                           const std::string& text, std::int64_t sms) {
        auto lines = wrap_lines(text, w);
        const int lh = Canvas::line_height() + 2;
        const int cy0 = y0, cy1 = y0 + h;
        const int content_h = static_cast<int>(lines.size()) * lh;
        auto draw_at = [&](int baseY) {
            for (int i = 0; i < (int)lines.size(); ++i) {
                int ly = baseY + i * lh;
                if (ly >= cy1 || ly + lh <= cy0) continue;
                c.draw_text_vclip(x, ly, lines[i], 1, cy0, cy1);
            }
        };
        if (content_h <= h) { draw_at(y0); return; }
        int div = scroll_div_ < 1 ? 1 : scroll_div_;
        int gap = h;                       // a screen of blank, then it restarts
        int period = content_h + gap;      // from the bottom (infinite loop)
        int off = static_cast<int>((sms / div) % period);
        draw_at(y0 - off);                 // copy A scrolling up and out
        draw_at(y0 - off + period);        // copy B following in from below
    }
};

} // namespace sapiens::sim
