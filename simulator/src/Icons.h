#pragma once
// Shared icon drawing (generic over a plot callback) so the SDL renderer and the
// LVGL image use the exact same shapes.

namespace sapiens::sim::icons {

// A bold "return / undo" arrow inside a W x H box: big left arrowhead on top, a
// thick bar curving down on the right and back along the bottom (like the ↩ glyph).
template <class Plot>
void return_arrow(Plot plot, int W, int H) {
    int th = H / 4; if (th < 4) th = 4;                 // bold stroke
    int topY = H * 20 / 100;
    int botY = H - H * 20 / 100 - th;
    int rightX = W * 70 / 100;
    int arrowBaseX = W * 50 / 100;
    int leftBot = W * 26 / 100;
    auto rect = [&](int x0, int y0, int x1, int y1) {
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) plot(x, y);
    };
    rect(arrowBaseX, topY, rightX + th, topY + th);     // top bar
    rect(rightX, topY, rightX + th, botY + th);         // right vertical
    rect(leftBot, botY, rightX + th, botY + th);        // bottom bar
    // round the outer corner (top-right) with a small filled wedge
    int cr = th;                                        // corner radius ~ stroke
    int ccx = rightX + th - cr, ccy = topY + cr;
    for (int dy = -cr; dy <= 0; ++dy)
        for (int dx = 0; dx <= cr; ++dx)
            if (dx * dx + dy * dy <= cr * cr) plot(ccx + dx, ccy + dy);
    // big left arrowhead
    int apexX = W * 4 / 100, apexY = topY + th / 2, baseX = arrowBaseX, halfH = th * 2;
    for (int x = apexX; x <= baseX; ++x) {
        int hh = halfH * (baseX - x) / (baseX - apexX);
        for (int y = apexY - hh; y <= apexY + hh; ++y) plot(x, y);
    }
}

} // namespace sapiens::sim::icons
