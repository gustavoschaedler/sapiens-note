#pragma once
// A 200x200 ink-level framebuffer: each pixel is 0 (white) .. 255 (full black ink).
// Shapes use full ink; text is anti-aliased (alpha-blended) for smooth edges in
// the simulator. The firmware HAL thresholds these levels to 1-bit for e-paper.
// Font is baked from a TTF -> GeneratedFont.h.
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "GeneratedFont.h"
#include "GeneratedFontMed.h"
#include "GeneratedFontBig.h"
#include "GeneratedFontHuge.h"

namespace sapiens::sim {

class Canvas {
public:
    static constexpr int W = 200;
    static constexpr int H = 200;

    Canvas() : px_(W * H, 0) {}

    const std::uint8_t* data() const { return px_.data(); }

    // v is a boolean ink flag for shape APIs (0 = white, nonzero = full black).
    void clear(std::uint8_t v = 0) {
        std::uint8_t lvl = v ? 255 : 0;
        for (auto& p : px_) p = lvl;
    }

    void set(int x, int y, std::uint8_t v) {
        if (x < 0 || y < 0 || x >= W || y >= H) return;
        px_[static_cast<std::size_t>(y) * W + x] = v ? 255 : 0;
    }

    // Alpha-blend an ink target (0=white,255=black) with coverage alpha (0..255).
    void blend(int x, int y, std::uint8_t target, int alpha) {
        if (x < 0 || y < 0 || x >= W || y >= H || alpha <= 0) return;
        std::size_t i = static_cast<std::size_t>(y) * W + x;
        int e = px_[i];
        px_[i] = static_cast<std::uint8_t>(e + (target - e) * alpha / 255);
    }

    void fill_rect(int x, int y, int w, int h, std::uint8_t v) {
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) set(x + i, y + j, v);
    }

    void draw_rect(int x, int y, int w, int h, std::uint8_t v) {
        for (int i = 0; i < w; ++i) { set(x + i, y, v); set(x + i, y + h - 1, v); }
        for (int j = 0; j < h; ++j) { set(x, y + j, v); set(x + w - 1, y + j, v); }
    }

    // Rounded-rectangle outline with quarter-circle corners (radius r).
    void draw_round_rect(int x, int y, int w, int h, std::uint8_t v, int r = 7) {
        if (r * 2 > w) r = w / 2;
        if (r * 2 > h) r = h / 2;
        hline(x + r, y, w - 2 * r, v);            // top
        hline(x + r, y + h - 1, w - 2 * r, v);    // bottom
        for (int j = r; j < h - r; ++j) { set(x, y + j, v); set(x + w - 1, y + j, v); } // sides
        int cx0 = x + r, cy0 = y + r, cx1 = x + w - 1 - r, cy1 = y + h - 1 - r;
        for (int a = 0; a <= r; ++a) {
            int b = static_cast<int>(std::lround(std::sqrt((double)(r * r - a * a))));
            set(cx0 - a, cy0 - b, v); set(cx0 - b, cy0 - a, v); // top-left
            set(cx1 + a, cy0 - b, v); set(cx1 + b, cy0 - a, v); // top-right
            set(cx0 - a, cy1 + b, v); set(cx0 - b, cy1 + a, v); // bottom-left
            set(cx1 + a, cy1 + b, v); set(cx1 + b, cy1 + a, v); // bottom-right
        }
    }

    void hline(int x, int y, int w, std::uint8_t v) {
        for (int i = 0; i < w; ++i) set(x + i, y, v);
    }

    // Invert (white<->black) every pixel inside a rounded rect — the e-ink
    // color-invert click flash. Works for any content already drawn (text/icons).
    void invert_round_rect(int x, int y, int w, int h, int r = 7) {
        if (r * 2 > w) r = w / 2;
        if (r * 2 > h) r = h / 2;
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                if (!rr_inside(i, j, w, h, r)) continue;
                int px = x + i, py = y + j;
                if (px < 0 || py < 0 || px >= W || py >= H) continue;
                auto& v = px_[static_cast<std::size_t>(py) * W + px];
                v = static_cast<std::uint8_t>(255 - v);
            }
    }

    // True/false corner test for a filled rounded rect of radius r.
    static bool rr_inside(int i, int j, int w, int h, int r) {
        int cx = -1, cy = -1;
        if (i < r && j < r) { cx = r; cy = r; }
        else if (i >= w - r && j < r) { cx = w - 1 - r; cy = r; }
        else if (i < r && j >= h - r) { cx = r; cy = h - 1 - r; }
        else if (i >= w - r && j >= h - r) { cx = w - 1 - r; cy = h - 1 - r; }
        if (cx < 0) return true;
        int dx = i - cx, dy = j - cy;
        return dx * dx + dy * dy <= r * r;
    }

    // Filled rounded rect (v = ink flag: 0 white / nonzero black).
    void fill_round_rect(int x, int y, int w, int h, std::uint8_t v, int r = 7) {
        if (r * 2 > w) r = w / 2;
        if (r * 2 > h) r = h / 2;
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i)
                if (rr_inside(i, j, w, h, r)) set(x + i, y + j, v);
    }

    // Soft (alpha-blended gray) filled rounded rect — used for the simulator's 3D
    // drop shadows. The e-paper HAL thresholds these away on real hardware.
    void blend_fill_round_rect(int x, int y, int w, int h, std::uint8_t target,
                               int alpha, int r = 7) {
        if (r * 2 > w) r = w / 2;
        if (r * 2 > h) r = h / 2;
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i)
                if (rr_inside(i, j, w, h, r)) blend(x + i, y + j, target, alpha);
    }

    void fill_disc(int cx, int cy, int r, std::uint8_t v) {
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx)
                if (dx * dx + dy * dy <= r * r) set(cx + dx, cy + dy, v);
    }

    void fill_diamond(int cx, int cy, int r, std::uint8_t v) {
        for (int dy = -r; dy <= r; ++dy) {
            int span = r - (dy < 0 ? -dy : dy);
            for (int dx = -span; dx <= span; ++dx) set(cx + dx, cy + dy, v);
        }
    }

    // Filled triangles (icons). cx = horizontal center; (left/right) edge anchors.
    void tri_up(int cx, int top, int w, int h, std::uint8_t v) {
        for (int i = 0; i <= h; ++i) { int hw = w * i / (2 * h); hline(cx - hw, top + i, 2 * hw + 1, v); }
    }
    void tri_down(int cx, int top, int w, int h, std::uint8_t v) {
        for (int i = 0; i <= h; ++i) { int hw = w * (h - i) / (2 * h); hline(cx - hw, top + i, 2 * hw + 1, v); }
    }
    void tri_right(int left, int cy, int w, int h, std::uint8_t v) {
        for (int i = 0; i <= w; ++i) { int hh = h * (w - i) / (2 * w); for (int j = -hh; j <= hh; ++j) set(left + i, cy + j, v); }
    }
    void tri_left(int right, int cy, int w, int h, std::uint8_t v) {
        for (int i = 0; i <= w; ++i) { int hh = h * (w - i) / (2 * w); for (int j = -hh; j <= hh; ++j) set(right - i, cy + j, v); }
    }

    // --- Text (proportional, anti-aliased baked font) -----------------------
    // (x, y) is the top-left of the glyph cell. `color` selects ink target
    // (nonzero = black text, 0 = white text). `s` scales by an integer factor.
    int draw_glyph_raw(int x, int y, const std::uint8_t* cov, int adv, int cw,
                       int ch, std::uint8_t color, int s) {
        const std::uint8_t target = color ? 255 : 0;
        for (int r = 0; r < ch; ++r)
            for (int col = 0; col < cw; ++col) {
                int a = cov[r * cw + col];
                if (!a) continue;
                for (int sy = 0; sy < s; ++sy)
                    for (int sx = 0; sx < s; ++sx)
                        blend(x + col * s + sx, y + r * s + sy, target, a);
            }
        return adv * s;
    }
    int draw_glyph(int x, int y, char c, std::uint8_t color, int s = 1) {
        const auto& g = font::glyph(static_cast<unsigned char>(c));
        return draw_glyph_raw(x, y, g.cov, g.advance, font::CELL_W, font::CELL_H, color, s);
    }

    // --- medium text: a baked ~20px font (bigger menu labels), drawn 1:1 -------
    int draw_text_med(int x, int y, const std::string& text, std::uint8_t color = 1) {
        int cx = x;
        for (char c : text) {
            const auto& g = fontmed::glyph(static_cast<unsigned char>(c));
            cx += draw_glyph_raw(cx, y, g.cov, g.advance, fontmed::CELL_W, fontmed::CELL_H, color, 1);
        }
        return cx - x;
    }
    static int text_width_med(const std::string& text) {
        int w = 0;
        for (char c : text) w += fontmed::glyph(static_cast<unsigned char>(c)).advance;
        return w;
    }
    static int line_height_med() { return fontmed::CELL_H; }

    // --- big (high-quality) text: a separately baked larger font, drawn 1:1 ----
    int draw_text_big(int x, int y, const std::string& text, std::uint8_t color = 1) {
        int cx = x;
        for (char c : text) {
            const auto& g = fontbig::glyph(static_cast<unsigned char>(c));
            cx += draw_glyph_raw(cx, y, g.cov, g.advance, fontbig::CELL_W, fontbig::CELL_H, color, 1);
        }
        return cx - x;
    }
    static int text_width_big(const std::string& text) {
        int w = 0;
        for (char c : text) w += fontbig::glyph(static_cast<unsigned char>(c)).advance;
        return w;
    }
    static int line_height_big() { return fontbig::CELL_H; }

    // --- huge text (for the recording timer) --------------------------------
    int draw_text_huge(int x, int y, const std::string& text, std::uint8_t color = 1) {
        int cx = x;
        for (char c : text) {
            const auto& g = fonthuge::glyph(static_cast<unsigned char>(c));
            cx += draw_glyph_raw(cx, y, g.cov, g.advance, fonthuge::CELL_W, fonthuge::CELL_H, color, 1);
        }
        return cx - x;
    }
    static int text_width_huge(const std::string& text) {
        int w = 0;
        for (char c : text) w += fonthuge::glyph(static_cast<unsigned char>(c)).advance;
        return w;
    }
    static int line_height_huge() { return fonthuge::CELL_H; }

    int draw_text(int x, int y, const std::string& text, std::uint8_t color = 1,
                  int s = 1) {
        int cx = x;
        for (char c : text) cx += draw_glyph(cx, y, c, color, s);
        return cx - x;
    }

    // Like draw_glyph but only paints columns within [clip_x0, clip_x1).
    int draw_glyph_clip(int x, int y, char c, std::uint8_t color, int clip_x0,
                        int clip_x1, int s = 1) {
        const auto& g = font::glyph(static_cast<unsigned char>(c));
        const std::uint8_t target = color ? 255 : 0;
        for (int r = 0; r < font::CELL_H; ++r)
            for (int col = 0; col < font::CELL_W; ++col) {
                int a = g.cov[r * font::CELL_W + col];
                if (!a) continue;
                for (int sy = 0; sy < s; ++sy)
                    for (int sx = 0; sx < s; ++sx) {
                        int px = x + col * s + sx;
                        if (px < clip_x0 || px >= clip_x1) continue;
                        blend(px, y + r * s + sy, target, a);
                    }
            }
        return g.advance * s;
    }

    void draw_text_clip(int x, int y, const std::string& text, std::uint8_t color,
                        int clip_x0, int clip_x1, int s = 1) {
        int cx = x;
        for (char c : text) cx += draw_glyph_clip(cx, y, c, color, clip_x0, clip_x1, s);
    }

    // Like draw_glyph but only paints rows within [cy0, cy1) — for clipped
    // vertical scrolling (text stays inside its box).
    int draw_glyph_vclip(int x, int y, char c, std::uint8_t color, int cy0, int cy1) {
        const auto& g = font::glyph(static_cast<unsigned char>(c));
        const std::uint8_t target = color ? 255 : 0;
        for (int r = 0; r < font::CELL_H; ++r) {
            int py = y + r;
            if (py < cy0 || py >= cy1) continue;
            for (int col = 0; col < font::CELL_W; ++col) {
                int a = g.cov[r * font::CELL_W + col];
                if (a) blend(x + col, py, target, a);
            }
        }
        return g.advance;
    }
    void draw_text_vclip(int x, int y, const std::string& text, std::uint8_t color,
                         int cy0, int cy1) {
        int cx = x;
        for (char c : text) cx += draw_glyph_vclip(cx, y, c, color, cy0, cy1);
    }

    // Marquee: draw `text` within a window of width `w` at (x,y). If it overflows,
    // it scrolls left over time (now_ms); otherwise it's drawn statically.
    // `div` controls speed: ms-per-pixel (smaller = faster). From the Settings slider.
    void draw_text_scroll(int x, int y, int w, const std::string& text,
                          std::uint32_t now_ms, int div = 16, std::uint8_t color = 1,
                          int s = 1) {
        int tw = text_width(text, s);
        if (tw <= w) { draw_text(x, y, text, color, s); return; }
        if (div < 1) div = 1;
        const int gap = 28 * s;
        const int period = tw + gap;
        int off = static_cast<int>((now_ms / div) % static_cast<std::uint32_t>(period));
        draw_text_clip(x - off, y, text, color, x, x + w, s);
        draw_text_clip(x - off + period, y, text, color, x, x + w, s);
    }

    // Med-font marquee (mirrors draw_text_scroll but with the ~21px med font),
    // for the bigger note-list rows whose date-time titles often overflow.
    int draw_glyph_clip_med(int x, int y, char c, std::uint8_t color, int clip_x0,
                            int clip_x1) {
        const auto& g = fontmed::glyph(static_cast<unsigned char>(c));
        const std::uint8_t target = color ? 255 : 0;
        for (int r = 0; r < fontmed::CELL_H; ++r)
            for (int col = 0; col < fontmed::CELL_W; ++col) {
                int a = g.cov[r * fontmed::CELL_W + col];
                if (!a) continue;
                int px = x + col;
                if (px < clip_x0 || px >= clip_x1) continue;
                blend(px, y + r, target, a);
            }
        return g.advance;
    }
    void draw_text_clip_med(int x, int y, const std::string& text, std::uint8_t color,
                            int clip_x0, int clip_x1) {
        int cx = x;
        for (char c : text) cx += draw_glyph_clip_med(cx, y, c, color, clip_x0, clip_x1);
    }
    void draw_text_scroll_med(int x, int y, int w, const std::string& text,
                              std::uint32_t now_ms, int div = 16,
                              std::uint8_t color = 1) {
        int tw = text_width_med(text);
        if (tw <= w) { draw_text_med(x, y, text, color); return; }
        if (div < 1) div = 1;
        const int gap = 28;
        const int period = tw + gap;
        int off = static_cast<int>((now_ms / div) % static_cast<std::uint32_t>(period));
        draw_text_clip_med(x - off, y, text, color, x, x + w);
        draw_text_clip_med(x - off + period, y, text, color, x, x + w);
    }

    static int text_width(const std::string& text, int s = 1) {
        int w = 0;
        for (char c : text) w += font::glyph(static_cast<unsigned char>(c)).advance * s;
        return w;
    }

    static int line_height(int s = 1) { return font::CELL_H * s; }

    // Truncate to fit `max_w` pixels, appending an ellipsis if cut.
    static std::string elide(const std::string& text, int max_w, int s = 1) {
        if (text_width(text, s) <= max_w) return text;
        std::string out = text;
        while (!out.empty() && text_width(out + "...", s) > max_w) out.pop_back();
        return out + "...";
    }

private:
    std::vector<std::uint8_t> px_;
};

} // namespace sapiens::sim
