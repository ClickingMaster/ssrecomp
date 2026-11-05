// On-screen controls for phones and tablets: a D-pad, A and B, L and R, START and SELECT, and
// buttons for the pause menu and fast forward, drawn over the game (frontend/ui.h's Canvas) and
// pressed with any number of fingers. Shown while the last input was a touch.
#pragma once
#include <cmath>
#include <cstdint>
#include <string>

#include <SDL.h>

#include "ui.h"
#include "touchlayout.h"

namespace gbaw
{

namespace Touch
{

// The first ten are the GBA's keys, in mGBA's order (main.cpp's GbaKey).
enum Button { A, B, SELECT, START, RIGHT, LEFT, UP, DOWN, R, L, MENU, FAST, COUNT };

struct Layout
{
    int padX = 0, padY = 0, padR = 0; // the D-pad's middle and size
    int aX = 0, aY = 0, bX = 0, bY = 0, abR = 0, bR = 0;
    SDL_Rect l{}, r{}, start{}, select{}, menu{}, fast{};
};

// Where everything goes on a canvas of w x h (about 360 lines high): the D-pad bottom left, A
// and B bottom right, L and R at the top corners, START and SELECT at the bottom in the middle.
inline Layout Make(int w, int h)
{
    Layout t;
    t.padR = 50;
    t.padX = 22 + t.padR;
    t.padY = h - 26 - t.padR;
    t.abR = t.bR = 25;
    t.aX = w - 22 - t.abR;
    t.aY = h - 26 - t.padR - 14;
    t.bX = t.aX - 2 * t.abR - 10;
    t.bY = t.aY + 30;
    t.l = { 6, 34, 88, 30 };
    t.r = { w - 94, 34, 88, 30 };
    t.select = { w / 2 - 70, h - 24, 60, 18 };
    t.start = { w / 2 + 10, h - 24, 60, 18 };
    t.menu = { w / 2 - 52, 4, 48, 18 };
    t.fast = { w / 2 + 4, 4, 48, 18 };
    return t;
}

inline std::vector<TouchLayout::Control> Geometry(const Layout& t)
{
    std::vector<TouchLayout::Control> c = {
        { "DPAD", "D-PAD", (float)t.padX, (float)t.padY, (float)t.padR, (float)t.padR, false },
        { "A", "A", (float)t.aX, (float)t.aY, (float)t.abR, (float)t.abR, true },
        { "B", "B", (float)t.bX, (float)t.bY, (float)t.bR, (float)t.bR, true }
    };
    auto rect = [&](const char* key, const SDL_Rect& r) { c.push_back({key, key, r.x + r.w / 2.0f, r.y + r.h / 2.0f, r.w / 2.0f, r.h / 2.0f, false}); };
    rect("L", t.l); rect("R", t.r); rect("SELECT", t.select); rect("START", t.start); rect("MENU", t.menu); rect(">>", t.fast);
    return c;
}
inline std::vector<TouchLayout::Control> Defaults(int w, int h)
{
    const int scale = std::max(1, std::min(h / 300, w / 600));
    auto controls = Geometry(Make(w / scale, h / scale));
    for (auto& c : controls) { c.x *= scale; c.y *= scale; c.rx *= scale; c.ry *= scale; }
    return controls;
}
inline void Apply(Layout& t, const TouchLayout::Profile& profile, int w, int h)
{
    auto controls = profile.Apply(Geometry(t), w, h);
    t.padX = (int)controls[0].x; t.padY = (int)controls[0].y; t.padR = (int)controls[0].rx;
    t.aX = (int)controls[1].x; t.aY = (int)controls[1].y; t.abR = (int)controls[1].rx;
    t.bX = (int)controls[2].x; t.bY = (int)controls[2].y; t.bR = (int)controls[2].rx;
    SDL_Rect* rects[] = { &t.l, &t.r, &t.select, &t.start, &t.menu, &t.fast };
    for (int i = 0; i < 6; ++i)
    {
        const auto& c = controls[i + 3];
        *rects[i] = { (int)(c.x - c.rx), (int)(c.y - c.ry), (int)(c.rx * 2), (int)(c.ry * 2) };
    }
}

// The buttons a finger at (x, y) on the canvas presses: a bit for each Button.
inline uint32_t Hit(const Layout& t, float x, float y)
{
    uint32_t m = 0;
    const float dx = x - t.padX, dy = y - t.padY;
    const float d = std::sqrt(dx * dx + dy * dy);
    if (d < t.padR * 1.4f && d > t.padR * 0.15f)
    {
        // Eight directions: each of the four covers 135 degrees, so the diagonals press two.
        const float a = std::atan2(dy, dx), s = 3.14159265f / 8;
        if (std::fabs(a) < 3 * s) m |= 1u << RIGHT;
        if (std::fabs(a) > 5 * s) m |= 1u << LEFT;
        if (a > s && a < 7 * s) m |= 1u << DOWN;
        if (a < -s && a > -7 * s) m |= 1u << UP;
    }
    auto inCircle = [&](int cx, int cy, float r) { const float ex = x - cx, ey = y - cy; return ex * ex + ey * ey <= r * r; };
    if (inCircle(t.aX, t.aY, t.abR * 1.3f)) m |= 1u << A;
    if (inCircle(t.bX, t.bY, t.bR * 1.3f)) m |= 1u << B;
    auto inRect = [&](const SDL_Rect& r) { return x >= r.x - 6 && x < r.x + r.w + 6 && y >= r.y - 6 && y < r.y + r.h + 6; };
    if (inRect(t.l)) m |= 1u << L;
    if (inRect(t.r)) m |= 1u << R;
    if (inRect(t.start)) m |= 1u << START;
    if (inRect(t.select)) m |= 1u << SELECT;
    if (inRect(t.menu)) m |= 1u << MENU;
    if (inRect(t.fast)) m |= 1u << FAST;
    return m;
}

inline void Circle(Canvas& c, int cx, int cy, int r, uint32_t argb)
{
    for (int y = -r; y <= r; y++)
    {
        const int half = (int)std::sqrt((float)(r * r - y * y));
        c.Fill(cx - half, cy + y, 2 * half + 1, 1, argb);
    }
}

// Drawn see-through over the game; a pressed button is brighter.
inline void Draw(Canvas& c, const Layout& t, uint32_t held, bool fastOn)
{
    const uint32_t off = 0x60FFFFFF, on = 0xB0FFFFFF, text = 0xD0202020, edge = 0x50000000;
    auto col = [&](Button b) { return (held >> b) & 1 ? on : off; };
    // The D-pad: a cross, each arm lit when pressed, on a darker edge so it shows on light
    // ground too.
    const int arm = t.padR, thick = std::max(8, t.padR * 3 / 5);
    c.Fill(t.padX - thick / 2 - 2, t.padY - arm - 2, thick + 4, 2 * arm + 4, edge);
    c.Fill(t.padX - arm - 2, t.padY - thick / 2 - 2, 2 * arm + 4, thick + 4, edge);
    c.Fill(t.padX - thick / 2, t.padY - thick / 2, thick, thick, off);
    c.Fill(t.padX - thick / 2, t.padY - arm, thick, arm - thick / 2, col(UP));
    c.Fill(t.padX - thick / 2, t.padY + thick / 2, thick, arm - thick / 2, col(DOWN));
    c.Fill(t.padX - arm, t.padY - thick / 2, arm - thick / 2, thick, col(LEFT));
    c.Fill(t.padX + thick / 2, t.padY - thick / 2, arm - thick / 2, thick, col(RIGHT));
    Circle(c, t.aX, t.aY, t.abR + 2, edge);
    Circle(c, t.bX, t.bY, t.bR + 2, edge);
    Circle(c, t.aX, t.aY, t.abR, col(A));
    Circle(c, t.bX, t.bY, t.bR, col(B));
    c.TextCentred(t.aX, t.aY - 7, "A", 2, text);
    c.TextCentred(t.bX, t.bY - 7, "B", 2, text);
    auto button = [&](const SDL_Rect& r, const std::string& label, uint32_t argb) {
        c.Fill(r.x - 2, r.y - 2, r.w + 4, r.h + 4, edge);
        c.Fill(r.x, r.y, r.w, r.h, argb);
        c.TextCentred(r.x + r.w / 2, r.y + (r.h - 7) / 2, label, 1, text);
    };
    button(t.l, "L", col(L));
    button(t.r, "R", col(R));
    button(t.select, "SELECT", col(SELECT));
    button(t.start, "START", col(START));
    button(t.menu, "MENU", col(MENU));
    button(t.fast, ">>", fastOn ? on : col(FAST));
}

}

} // namespace gbaw
