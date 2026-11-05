// The program's own menus, drawn in software: the game list, the settings page, the pause
// menu and messages. Drawn into an ARGB picture (Canvas) of a fixed small size that is scaled
// up to the window with sharp pixels, so the menus look the same at 720p and at 4K. The same
// look as Pokemon DS Widescreen's menus (its 5x7 letters).
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// (In a namespace of its own: built into the DS launcher, whose Canvas and Menu are others.)
namespace gbaw
{

struct Canvas
{
    int w = 0, h = 0;
    std::vector<uint32_t> px; // ARGB; alpha 0 shows the game underneath

    void Resize(int nw, int nh)
    {
        if (nw != w || nh != h)
        {
            w = nw;
            h = nh;
            px.assign((size_t)w * h, 0);
        }
    }
    void Clear() { std::fill(px.begin(), px.end(), 0); }

    void Fill(int x, int y, int rw, int rh, uint32_t argb)
    {
        const int x0 = std::max(x, 0), y0 = std::max(y, 0);
        const int x1 = std::min(x + rw, w), y1 = std::min(y + rh, h);
        for (int yy = y0; yy < y1; yy++)
            std::fill(px.begin() + (size_t)yy * w + x0, px.begin() + (size_t)yy * w + std::max(x0, x1), argb);
    }
    // A frame of thickness t around a rectangle (outside it).
    void Frame(int x, int y, int rw, int rh, int t, uint32_t argb)
    {
        Fill(x - t, y - t, rw + 2 * t, t, argb);
        Fill(x - t, y + rh, rw + 2 * t, t, argb);
        Fill(x - t, y, t, rh, argb);
        Fill(x + rw, y, t, rh, argb);
    }

    // 5x7 letters, one row per byte, bit 4 the leftmost pixel.
    static const uint8_t* Glyph(char c)
    {
        static const uint8_t font[][7] = {
            { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, // A
            { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E }, // B
            { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }, // C
            { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E }, // D
            { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }, // E
            { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 }, // F
            { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F }, // G
            { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, // H
            { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }, // I
            { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C }, // J
            { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, // K
            { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F }, // L
            { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 }, // M
            { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 }, // N
            { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, // O
            { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 }, // P
            { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D }, // Q
            { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 }, // R
            { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E }, // S
            { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, // T
            { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, // U
            { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 }, // V
            { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 }, // W
            { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 }, // X
            { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 }, // Y
            { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F }, // Z
            { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, // 0
            { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E }, // 1
            { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }, // 2
            { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E }, // 3
            { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, // 4
            { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E }, // 5
            { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, // 6
            { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }, // 7
            { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, // 8
            { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C }, // 9
            { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 }, // :
            { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 }, // -
            { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 }, // +
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06 }, // .
            { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 }, // /
            { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 }, // <
            { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 }, // >
            { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 }, // !
            { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 }, // (
            { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 }, // )
            { 0x00, 0x00, 0x00, 0x00, 0x06, 0x02, 0x04 }, // ,
            { 0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00 }, // '
            { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 }, // ?
            { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F }, // _
            { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 }, // %
            { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00 }, // =
            { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x08 }, // ;
            // PlayStation's button symbols: cross, circle, square, triangle (smaller than the
            // letters, so the cross and the circle aren't taken for an X and an O).
            { 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x00 },
            { 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E, 0x00 },
            { 0x00, 0x1F, 0x11, 0x11, 0x11, 0x1F, 0x00 },
            { 0x00, 0x04, 0x0A, 0x0A, 0x11, 0x1F, 0x00 },
        };
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c >= 'A' && c <= 'Z') return font[c - 'A'];
        if (c >= '0' && c <= '9') return font[26 + c - '0'];
        static const char punct[] = ":-+./<>!(),'?_%=;\x01\x02\x03\x04";
        const char* p = c ? strchr(punct, c) : nullptr;
        return p ? font[36 + (p - punct)] : nullptr;
    }

    static int TextWidth(const std::string& s, int scale) { return s.empty() ? 0 : ((int)s.size() * 6 - 1) * scale; }

    void Text(int x, int y, const std::string& s, int scale, uint32_t argb)
    {
        for (char c : s)
        {
            if (const uint8_t* g = Glyph(c))
                for (int row = 0; row < 7; row++)
                    for (int col = 0; col < 5; col++)
                        if (g[row] & (0x10 >> col)) Fill(x + col * scale, y + row * scale, scale, scale, argb);
            x += 6 * scale;
        }
    }
    void TextCentred(int cx, int y, const std::string& s, int scale, uint32_t argb) { Text(cx - TextWidth(s, scale) / 2, y, s, scale, argb); }
};

// PlayStation's button symbols, as characters of the font.
#define PS_CROSS "\x01"
#define PS_CIRCLE "\x02"
#define PS_SQUARE "\x03"
#define PS_TRIANGLE "\x04"

// Words to lines of at most `chars` letters.
inline std::vector<std::string> Wrap(const std::string& text, int chars)
{
    std::vector<std::string> lines;
    std::string line, word;
    auto flush = [&]() {
        if (word.empty()) return;
        if (!line.empty() && (int)(line.size() + 1 + word.size()) > chars)
        {
            lines.push_back(line);
            line.clear();
        }
        line += (line.empty() ? "" : " ") + word;
        word.clear();
    };
    for (char ch : text)
        if (ch == ' ') flush();
        else word += ch;
    flush();
    if (!line.empty()) lines.push_back(line);
    return lines;
}

// The colours of the menus.
namespace Colour
{
#ifdef GBAW_EMBEDDED
// (Inside the all-in-one launcher: its near-black, white text and red details.)
constexpr uint32_t Back = 0xFF0B0B0C;
constexpr uint32_t Band = 0xFF141416;
constexpr uint32_t BandLine = 0xFFE0242C;
constexpr uint32_t Title = 0xFFFFFFFF;
constexpr uint32_t Text = 0xFFB8B8BC;
constexpr uint32_t Bright = 0xFFFFFFFF;
constexpr uint32_t Hint = 0xFF8A8A90;
constexpr uint32_t Chosen = 0xFFE0242C;
constexpr uint32_t Box = 0xFF1A1A1D;
constexpr uint32_t BoxEdge = 0xFFE0242C;
constexpr uint32_t Shade = 0xA0000000;
constexpr uint32_t Warn = 0xFFE0242C;
#else
constexpr uint32_t Back = 0xFF0B0F18;     // the pages' background
constexpr uint32_t Band = 0xFF152036;     // the title band
constexpr uint32_t BandLine = 0xFF3D6FD1; // under it
constexpr uint32_t Title = 0xFFFFD75E;
constexpr uint32_t Text = 0xFFC8D0E0;
constexpr uint32_t Bright = 0xFFFFFFFF;
constexpr uint32_t Hint = 0xFF8FA0C0;
constexpr uint32_t Chosen = 0xFF3D6FD1;   // the chosen row
constexpr uint32_t Box = 0xFF1C2A44;      // the pause menu's box
constexpr uint32_t BoxEdge = 0xFFE8E8E8;
constexpr uint32_t Shade = 0xA0000000;    // over the game while the pause menu is open
constexpr uint32_t Warn = 0xFFFF8A5E;
#endif
}

// A rectangle on the canvas (where something was drawn, for taps).
struct UiRect
{
    int x = 0, y = 0, w = 0, h = 0;
    bool Has(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

struct MenuItem
{
    std::string label;
    std::string value; // shown on the right; a setting has < > around it
    bool fixed = false; // a status: `value` shown as it is, without < >
};

// The pause menu: a box in the middle over the darkened game. `big` (for fingers): text twice
// the size and rows tall enough to tap.
struct Menu
{
    int selected = 0;
    std::vector<MenuItem> items;
    // Where the box and each row were last drawn (for taps). rowItem[i] is the items index of
    // that row (a long list is scrolled, so a row is not always items[i]).
    mutable UiRect box;
    mutable std::vector<UiRect> rows;
    mutable std::vector<int> rowItem;

    void Draw(Canvas& c, const std::string& title, const std::string& hint, bool big = false) const
    {
        const int n = (int)items.size();
        const int s = big ? 2 : 1;
        const int lineH = big ? std::max(12, std::min(30, (c.h - 40) / std::max(1, n + 1))) : 12;
        auto shown = [](const MenuItem& it) {
            if (it.value.empty()) return std::string();
            return it.fixed ? it.value : "< " + it.value + " >";
        };
        int widest = 0;
        for (const MenuItem& it : items)
        {
            const std::string v = shown(it);
            widest = std::max(widest, Canvas::TextWidth(it.label, s) + (v.empty() ? 0 : Canvas::TextWidth(v, s) + 16 * s));
        }
        const int boxW = std::min(std::max({ 220, Canvas::TextWidth(hint, 1) + 16, widest + 16 }), c.w - 8);
        const int headH = 7 * s + 10, footH = 18;
        // A list longer than the screen shows the rows around the chosen one.
        int visible = n, first = 0;
        const int maxH = std::max(lineH + headH + footH, c.h - 4);
        if (n > 0 && headH + lineH * n + footH > maxH)
        {
            visible = std::max(1, (maxH - headH - footH) / lineH);
            if (visible > n) visible = n;
            first = selected - visible / 2;
            if (first < 0) first = 0;
            if (first + visible > n) first = std::max(0, n - visible);
        }
        const int boxH = headH + lineH * visible + footH;
        const int bx = (c.w - boxW) / 2, by = std::max(2, (c.h - boxH) / 2);
        c.Fill(0, 0, c.w, c.h, Colour::Shade);
        c.Frame(bx, by, boxW, boxH, 1, Colour::BoxEdge);
        c.Fill(bx, by, boxW, boxH, Colour::Box);
        c.TextCentred(bx + boxW / 2, by + 5, title, s, Colour::Title);
        box = { bx, by, boxW, boxH };
        rows.clear();
        rowItem.clear();
        int y = by + headH;
        for (int i = 0; i < visible; i++)
        {
            const int item = first + i;
            const bool sel = item == selected;
            rows.push_back({ bx + 2, y, boxW - 4, lineH });
            rowItem.push_back(item);
            if (sel) c.Fill(bx + 2, y, boxW - 4, lineH, Colour::Chosen);
            const uint32_t col = sel ? Colour::Bright : Colour::Text;
            const int ty = y + (lineH - 7 * s) / 2;
            c.Text(bx + 4 + 4 * s, ty, items[item].label, s, col);
            const std::string v = shown(items[item]);
            if (!v.empty()) c.Text(bx + boxW - 4 - 4 * s - Canvas::TextWidth(v, s), ty, v, s, col);
            y += lineH;
        }
        c.TextCentred(bx + boxW / 2, y + 6, hint, 1, Colour::Hint);
    }
};

} // namespace gbaw
