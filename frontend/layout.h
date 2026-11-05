// Single-display layouts for the two DS screens.
#pragma once
#include <string>
#include <SDL.h>

enum class LayoutMode
{
    Merged,   // one view: a base screen with parts of the other drawn into it (see merged.h)
    Hybrid,   // one large screen with the other as a small inset beside it
    Side,     // both screens side by side at equal size
    Vertical, // classic stacked arrangement
    Top,      // only the top screen
    Bottom,   // only the bottom screen
};

struct Layout
{
    LayoutMode mode = LayoutMode::Merged;
    bool swapped = false; // in Hybrid, makes the bottom screen the large one
};

inline Layout LayoutFromName(const std::string& n)
{
    Layout l;
    if (n == "hybrid") l.mode = LayoutMode::Hybrid;
    else if (n == "side") l.mode = LayoutMode::Side;
    else if (n == "vertical") l.mode = LayoutMode::Vertical;
    else if (n == "top") l.mode = LayoutMode::Top;
    else if (n == "bottom") l.mode = LayoutMode::Bottom;
    return l;
}

inline Layout LayoutNext(Layout l)
{
    l.mode = (LayoutMode)(((int)l.mode + 1) % 6);
    return l;
}

// Size of the layout in DS pixels, used for the initial window size.
inline void LayoutNativeSize(const Layout& l, int& w, int& h)
{
    switch (l.mode)
    {
    case LayoutMode::Hybrid: w = 256 + 86; h = 192; break; // 16:9 overall
    case LayoutMode::Side: w = 512; h = 192; break;
    case LayoutMode::Vertical: w = 256; h = 384; break;
    default: w = 256; h = 192; break;
    }
}

// Fills rects[0] (top screen) and rects[1] (bottom screen) for a window of
// the given size. A rect with zero width is not drawn.
inline void LayoutRects(const Layout& l, int winW, int winH, SDL_Rect rects[2])
{
    SDL_Rect none = { 0, 0, 0, 0 };
    if (l.mode == LayoutMode::Hybrid)
    {
        // The large screen takes the full height (or width, on narrow windows). The small
        // one sits beside it in the leftover strip; if that strip is too thin to be
        // readable, it is drawn over the lower right corner of the large screen instead.
        int big = l.swapped ? 1 : 0;
        int bh = winH, bw = bh * 4 / 3;
        if (bw > winW)
        {
            bw = winW;
            bh = bw * 3 / 4;
        }
        int strip = winW - bw;
        int sw, sh, bx, sx, sy;
        if (strip >= bw / 5)
        {
            sw = strip < bw / 2 ? strip : bw / 2;
            sh = sw * 3 / 4;
            bx = (winW - bw - sw) / 2;
            sx = bx + bw;
            sy = (winH - bh) / 2 + bh - sh;
        }
        else
        {
            sw = bw * 3 / 10;
            sh = sw * 3 / 4;
            bx = (winW - bw) / 2;
            sx = bx + bw - sw;
            sy = (winH - bh) / 2 + bh - sh;
        }
        rects[big] = { bx, (winH - bh) / 2, bw, bh };
        rects[1 - big] = { sx, sy, sw, sh };
        return;
    }

    int nw, nh;
    LayoutNativeSize(l, nw, nh);
    float s = (float)winW / nw;
    if ((float)winH / nh < s) s = (float)winH / nh;
    int ox = (int)((winW - nw * s) / 2);
    int oy = (int)((winH - nh * s) / 2);
    auto R = [&](float x, float y, float w, float h) {
        SDL_Rect r = { ox + (int)(x * s), oy + (int)(y * s), (int)(w * s), (int)(h * s) };
        return r;
    };
    switch (l.mode)
    {
    case LayoutMode::Side:
        rects[0] = R(0, 0, 256, 192);
        rects[1] = R(256, 0, 256, 192);
        break;
    case LayoutMode::Vertical:
        rects[0] = R(0, 0, 256, 192);
        rects[1] = R(0, 192, 256, 192);
        break;
    case LayoutMode::Top:
        rects[0] = R(0, 0, 256, 192);
        rects[1] = none;
        break;
    default:
        rects[0] = none;
        rects[1] = R(0, 0, 256, 192);
        break;
    }
}

// In Hybrid the small screen may overlap the large one, so it has to be drawn last.
inline int LayoutDrawFirst(const Layout& l)
{
    return (l.mode == LayoutMode::Hybrid && l.swapped) ? 1 : 0;
}

// Maps a window position to touchscreen coordinates; false if outside the bottom screen.
inline bool LayoutTouch(const SDL_Rect& bottom, int mx, int my, int& tx, int& ty)
{
    if (bottom.w <= 0 || bottom.h <= 0) return false;
    if (mx < bottom.x || my < bottom.y || mx >= bottom.x + bottom.w || my >= bottom.y + bottom.h) return false;
    tx = (mx - bottom.x) * 256 / bottom.w;
    ty = (my - bottom.y) * 192 / bottom.h;
    return true;
}

// The same for a whole layout: in Hybrid with the bottom screen large, the top screen's inset may
// be drawn over its corner (LayoutDrawFirst), and a click on the inset is on the top screen.
inline bool LayoutTouch(const Layout& l, const SDL_Rect rects[2], int mx, int my, int& tx, int& ty)
{
    const SDL_Rect& top = rects[0];
    if (l.mode == LayoutMode::Hybrid && l.swapped && mx >= top.x && my >= top.y && mx < top.x + top.w && my < top.y + top.h)
        return false;
    return LayoutTouch(rects[1], mx, my, tx, ty);
}
