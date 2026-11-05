// Merged single-screen view.
//
// The view is built from rectangles cut out of the two DS screens. Usually one
// screen fills it and parts of the other are drawn over it, with chosen graphics
// layers (typically that screen's background) made transparent so only its buttons
// and panels appear. Which pieces are used depends on what the game is doing; see
// pickScene in main.cpp.
#pragma once
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <thread>
#include <vector>
#include <SDL.h>

// Layer classes, derived from the 2D renderer's per-pixel flags.
enum : uint8_t
{
    LAYER_BG0 = 0x01,
    LAYER_BG1 = 0x02,
    LAYER_BG2 = 0x04,
    LAYER_BG3 = 0x08,
    LAYER_OBJ = 0x10,
    LAYER_BACKDROP = 0x20,
    LAYER_3D = 0x40,
};

inline uint8_t LayerClass(uint8_t flag)
{
    if (flag & 0x40) return (flag & 0x80) ? LAYER_OBJ : LAYER_3D;
    if (flag & 0x90) return LAYER_OBJ;
    if (flag & 0x0F) return flag & 0x0F;
    return LAYER_BACKDROP;
}

// One piece of the merged view: a rectangle of one DS screen drawn somewhere in it.
// Positions are in base pixels: the 256x192 area a full screen occupies when the
// view is 4:3. On wider outputs the view extends left and right of that area.
struct Element
{
    int screen;           // 0 = top, 1 = bottom, -1 = a plain rectangle of colour keys[0] (sw x sh)
    int sx, sy, sw, sh;   // source rectangle on that screen
    float dx, dy;         // destination of its top left corner
    float scale;          // destination size per source pixel
    uint8_t drop;         // layer classes that are not drawn
    uint8_t alpha;        // opacity of what is drawn
    bool crisp;           // nearest-neighbour instead of filtered sampling
    // Source pixels at or beyond both of these coordinates are not drawn (cuts out a lower right corner).
    int cutX = 256, cutY = 192;
    // Source pixels of exactly these colours are not drawn (e.g. the ruling of a paper background).
    uint32_t keys[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
    // If set, colours within this distance per channel of keys[0] are not drawn either.
    int keyTolerance = 0;
    // If set, the keys only apply within this many source pixels of the piece's edge (to
    // trim the rim of a rounded button without touching the same colour inside it).
    int keyRing = 0;
    // If set, sprite pixels within this many source pixels of a corner (in both directions)
    // are not drawn: a cursor drawn as sprites at the corners of buttons whose labels are
    // sprites too.
    int dropSpriteCorner = 0;
    // Mirrors the piece left to right / top to bottom (to close a box the game leaves open
    // at the screen's edge with a copy of its other side).
    bool flipX = false, flipY = false;
    // Drawn this many times wider than `scale` makes it (a strip of one colour across, widened
    // as one piece: the battle buttons' plain middles).
    float stretchX = 1.0f;
    // Draws a dark outline around the piece, for a panel that floats over another picture.
    bool border = false;
    uint32_t borderColor = 0x282830;
    int borderWidth = 0;  // in 8ths of a base pixel; 0 = the usual 6
    float borderRadius = 0; // rounds the outline's corners: radius of its inner edge, in base pixels
    // Up to two more rectangles of the source (x0, y0, x1, y1, exclusive) that are not drawn:
    // where a part of the screen is drawn elsewhere instead (Black 2's HP boxes in a wide battle).
    // (They apply only over a widescreen world, which shows through them; without one the
    // piece is drawn whole, and pieces marked wideOnly, the parts drawn elsewhere, are not.)
    int holeCount = 0;
    int holes[2][4] = {};
    bool wideOnly = false;

    // Whether source pixel (x, y) is left out by the cut corner or a hole.
    bool Skips(int x, int y) const
    {
        if (x >= cutX && y >= cutY) return true;
        for (int i = 0; i < holeCount; i++)
            if (x >= holes[i][0] && y >= holes[i][1] && x < holes[i][2] && y < holes[i][3]) return true;
        return false;
    }
};

// The columns [x0, x1) a rounded rectangle covers in output row y (pixel centres), or
// x0 >= x1 if none. The rectangle is (rx, ry, rw, rh) with corner radius r.
inline void RoundedRowExtent(int rx, int ry, int rw, int rh, float r, int y, int& x0, int& x1)
{
    x0 = x1 = 0;
    if (y < ry || y >= ry + rh) return;
    float inset = 0;
    const float cy = y + 0.5f;
    float d = -1;
    if (cy < ry + r) d = ry + r - cy;
    else if (cy > ry + rh - r) d = cy - (ry + rh - r);
    if (d > 0) inset = r - std::sqrt(std::max(0.0f, r * r - d * d));
    x0 = rx + (int)(inset + 0.5f);
    x1 = rx + rw - (int)(inset + 0.5f);
}

// Draws one row of an outline of thickness t around (rx, ry, rw, rh) whose inner edge has
// corner radius r: calls fill(xa, xb) for each span of it in output row y.
template <typename F>
inline void OutlineRow(int rx, int ry, int rw, int rh, int t, float r, int y, F fill)
{
    int ox0, ox1, ix0, ix1;
    RoundedRowExtent(rx - t, ry - t, rw + 2 * t, rh + 2 * t, r > 0 ? r + t : 0, y, ox0, ox1);
    if (ox1 <= ox0) return;
    RoundedRowExtent(rx, ry, rw, rh, r, y, ix0, ix1);
    if (ix1 <= ix0) { fill(ox0, ox1); return; }
    if (ix0 > ox0) fill(ox0, ix0);
    if (ox1 > ix1) fill(ix1, ox1);
}

struct Scene
{
    const char* name = "top";
    std::vector<Element> items; // drawn in order, later ones on top
    // Colour behind the pieces: black, or sampled from this pixel of a screen so the
    // parts of the view no piece covers continue that screen's background.
    int bgScreen = -1, bgX = 0, bgY = 0;
    // The first piece is the top screen showing the 3D world: when a widescreen image of
    // that world is available it fills the view, and only the 2D parts of the piece (text
    // boxes and the like) are drawn over it.
    bool wide = false;
    // The first piece is the top screen in a battle: the bars beside it are filled with
    // each row's outermost pixel, darkening outwards, so that the sky, the ground and the
    // HP boxes that run off the screen carry on to the edge of the view instead of
    // stopping at black.
    bool edgeFill = false;
    // With `wide`: 2D reaching the top screen's edge is continued beside it only where it is
    // a fill (the same colour for 8 pixels: a wipe, a flash, a window), not a picture (a
    // snapshot of the world shown in 2D, which would turn into streaks), and a translucent
    // fill is blended with the wide world instead of the screen's own 3D (stripes otherwise).
    bool smartEdges = false;
    // Rows of a closed 2D window whose non-repeating border must stay inside the
    // original screen, including its otherwise-flat top and bottom strokes.
    uint8_t edgeWindow[2][192] = {};

    // With `wide`: this frame shows the top screen at 4:3 after all, while the widescreen
    // renderer keeps running (a picture in 2D covers the world; see MergedEdgesArePicture).
    bool narrow = false;
    // A single screen at full height on a wide view: the bars beside it in soft colours from
    // its edges (MergedNarrowBars) rather than one colour.
    bool softBars = false;
    // With `wide`: the game's weather particles (sprites over the world: sand, rain, snow) are
    // on the top screen; they are repeated beside it (the screen's sprites at their place one
    // screen width to the left and right), so that the weather reaches the edges of the view.
    // Only in rows where the world shows beside the screen on both sides: where 2D is
    // continued on either (an area's banner, a text box, a wipe; a banner's slanted edge
    // reaches one side before the other), other sprites (the banner's badge and words) would
    // be repeated with them.
    bool weatherObj = false;
    // Rows of each screen drawn on below its last line (belowscreen.h), which its pieces may
    // show: the screen images then have 192 + extraRows rows.
    int extraRows[2] = { 0, 0 };
    // A screen drawn again from its background layers (belowscreen.h, RenderScreenRows), all of
    // its rows and those below it, without the layers named here (bit n for background n) and
    // without blending between layers; its sprites as the game drew them. -1: as the game drew
    // it. For a menu's buttons and cards without the picture behind them, and without that
    // picture blended into them (the pieces then leave out LAYER_BACKDROP).
    int redrawSkip[2] = { -1, -1 };
    // With redrawSkip: the background whose vertical scroll is undone in that drawing (all the
    // layers drawn move with it), so that a list the game scrolls is always shown whole, from
    // its top, its highlight moving down it; the sprites move with it. -1: as scrolled.
    int redrawUnscroll[2] = { -1, -1 };
};


// A plain rectangle, for the backing of a group of pieces.
inline Element FillRect(float dx, float dy, int w, int h, uint32_t rgb)
{
    Element e = { -1, 0, 0, w, h, dx, dy, 1.0f, 0, 255, true };
    e.keys[0] = rgb;
    return e;
}

// How dark the edge fill is at the outer edge of the view (in 256ths of full brightness).
enum { EdgeFillOuter = 96 };

// The 3D world drawn across the whole view (see GPU3D_Wide.h), and the brightness the
// game currently applies to its screen (fades), which has to be applied to it as well.
struct WideImage
{
    const uint32_t* pixels = nullptr; // top byte 0 where nothing was drawn
    int w = 0, h = 0;
    int fadeMode = 0;   // 0 none, 1 towards white, 2 towards black
    int fade = 0;       // 0..16
    // Instead of the 3D world across the whole view: the whole top screen widened (see
    // battlewide.h), in base pixels from base x `baseX`, already with its fades. It replaces
    // the first piece, which is not drawn.
    bool placed = false;
    int baseX = 0;
};

struct ScreenImage
{
    const uint32_t* pixels;   // 256x192 XRGB (taller with Scene::extraRows)
    const uint8_t* flags;     // 256x192 renderer flags, may be null (likewise)
    // Each row's first and last pixel before blending, how it is blended with the 3D world,
    // and whether the pixels next to it match (GPU2D::SoftRenderer::EdgeColour, EdgeBlend and
    // EdgeFlat); may be null. See Scene::smartEdges.
    const uint32_t* edge = nullptr;
    const uint16_t* edgeBlend = nullptr;
    const uint8_t* edgeFlat = nullptr;
};

// The colour the edge fill continues on row `y` of a side (0 left, 1 right): the outermost of
// the screen's last few columns that isn't black. Some scenes leave a black column or two at
// the screen's edge (SoulSilver's evolution, its right side), which would make the whole bar
// black. frontend/gpucompose.h's DrawEdgeFill uses it too.
inline uint32_t EdgeFillColour(const ScreenImage& top, int y, int side)
{
    const uint32_t* row = top.pixels + y * 256;
    for (int i = 0; i < 4; i++)
    {
        const uint32_t c = row[side ? 255 - i : i];
        if (((c >> 16) & 0xFF) > 8 || ((c >> 8) & 0xFF) > 8 || (c & 0xFF) > 8) return c;
    }
    return row[side ? 255 : 0];
}

// The colour of the weather particle beside the top screen at base x bx (outside 0..255) and
// row sy, if there is one there (see Scene::weatherObj).
inline bool MergedWeatherPixel(const ScreenImage& top, int bx, int sy, uint32_t& rgb)
{
    const int sx = ((bx % 256) + 256) % 256;
    const size_t i = (size_t)sy * 256 + sx;
    if (LayerClass(top.flags[i]) != LAYER_OBJ) return false;
    rgb = top.pixels[i] & 0xFFFFFF;
    return true;
}

// Mapping between base pixels and output pixels.
struct MergedView
{
    float unit;       // output pixels per base pixel
    float originX;    // output position of base pixel (0, 0)
    float originY;
    float width;      // width of the whole view in base pixels (256 when the output is 4:3 or narrower)
};

// ASPECT RATIO: the widest the view may be, width / height (0: the output's own shape). A
// narrower one stands in the middle of the output, black bars beside it (MergedBlackBars).
inline float g_mergedAspect = 0;

// The output columns the view covers, after ASPECT RATIO: x0 to x1.
inline void MergedViewColumns(int outW, int outH, int& x0, int& x1)
{
    int w = outW;
    if (g_mergedAspect > 0 && outH > 0 && outW > outH * g_mergedAspect) w = (int)(outH * g_mergedAspect + 0.5f);
    x0 = (outW - w) / 2;
    x1 = x0 + w;
}

inline MergedView MergedMakeView(int outW, int outH)
{
    int x0, x1;
    MergedViewColumns(outW, outH, x0, x1);
    const int viewW = x1 - x0;
    MergedView v;
    v.unit = outH / 192.0f;
    if (viewW / 256.0f < v.unit) v.unit = viewW / 256.0f;
    v.originX = (outW - 256 * v.unit) / 2;
    v.originY = (outH - 192 * v.unit) / 2;
    v.width = viewW / v.unit;
    return v;
}

// What continues the top screen's row sy beside it in a wide view (side 0 left, 1 right):
// rgb * eva / 16 + the wide world * evb / 16 (eva 0, evb 16: nothing, the world shows).
// pattern: instead, the row's 16 pixels at that edge repeated outwards, as they are.
struct EdgeContinuation
{
    uint32_t rgb;
    int eva, evb;
    bool pattern = false;
    bool none() const { return eva == 0 && evb == 16 && !pattern; }
};

// The screen pixel that continues row sy at base x (left of 0 or right of 255) when the row
// continues as a pattern: the 16 pixels at that edge, repeated.
inline uint32_t MergedEdgePatternPixel(const ScreenImage& top, int sy, int x)
{
    const int m = x < 0 ? ((x % 16) + 16) % 16 : 240 + (x - 256) % 16;
    return top.pixels[sy * 256 + m] & 0xFFFFFF;
}

// A horizontal tile is continued only if the adjacent inward tile matches.
inline bool MergedEdgeTileRepeats(const ScreenImage& top, int y, int side)
{
    for (int k = 0; k < 16; ++k)
    {
        const int x = side ? 255 - k : k, inner = side ? x - 16 : x + 16;
        if (top.flags[y * 256 + x] != top.flags[y * 256 + inner] ||
            ((top.pixels[y * 256 + x] ^ top.pixels[y * 256 + inner]) & 0xFFFFFF)) return false;
    }
    return true;
}

// Find whole opaque background bands with a non-repeating edge, once per scene.
// Suppressing just their patterned rows leaves the window's flat horizontal strokes
// sticking out into the world. A genuinely tiled banner and a solid fade still extend.
inline void MergedPrepareEdgeWindows(Scene& scene, const ScreenImage* screens)
{
    for (auto& side : scene.edgeWindow) std::fill(std::begin(side), std::end(side), 0);
    if (!scene.smartEdges || scene.items.empty() || scene.items[0].screen < 0) return;
    const ScreenImage& top = screens[scene.items[0].screen];
    if (!top.flags || !top.pixels || !top.edgeBlend || !top.edgeFlat) return;
    for (int side = 0; side < 2; ++side)
    {
        const int x = side ? 255 : 0;
        for (int y = 0; y < 192;)
        {
            const uint8_t cls = LayerClass(top.flags[y * 256 + x]);
            if (!(cls & 0x0F) || (top.edgeBlend[y * 2 + side] >> 8) != 0) { ++y; continue; }
            const int first = y;
            bool window = false;
            do
            {
                if (!(top.edgeFlat[y * 2 + side] & 3))
                {
                    bool band = true;
                    const uint8_t flag = top.flags[y * 256 + x];
                    for (int k = 0; k < 16; ++k)
                    {
                        const int sx = side ? 255 - k : k;
                        if (top.flags[y * 256 + sx] != flag || scene.items[0].Skips(sx, y)) { band = false; break; }
                    }
                    if (band && !MergedEdgeTileRepeats(top, y, side)) window = true;
                }
                ++y;
            } while (y < 192 && LayerClass(top.flags[y * 256 + x]) == cls && (top.edgeBlend[y * 2 + side] >> 8) == 0);
            if (window) std::fill(scene.edgeWindow[side] + first, scene.edgeWindow[side] + y, 1);
        }
    }
}

// The column whose colour goes on beside the screen: the outermost one, or the next in when the
// outermost is the 3D world alone and the 2D layer is flat from there (a background scrolled by
// one pixel, as in the Fly animation's sky). The same choice as the one in GPU2D_Soft.cpp.
inline int MergedEdgeColumn(const ScreenImage& top, int sy, int side)
{
    const int x = side ? 255 : 0, dir = side ? -1 : 1;
    if (!top.flags || LayerClass(top.flags[sy * 256 + x]) != LAYER_3D) return x;
    const uint8_t flag = top.flags[sy * 256 + x + dir];
    if (LayerClass(flag) == LAYER_3D) return x;
    for (int k = 2; k < 9; k++)
        if (top.flags[sy * 256 + x + dir * k] != flag) return x;
    return x + dir;
}

inline EdgeContinuation MergedEdgeContinuation(const Scene& scene, const ScreenImage& top, int sy, int side)
{
    const int sx = MergedEdgeColumn(top, sy, side);
    const EdgeContinuation none = { 0, 0, 16 };
    // (Nor where the piece leaves the screen out: that part is drawn elsewhere.)
    if (scene.items[0].Skips(sx, sy) || scene.edgeWindow[side][sy]) return none;
    const uint8_t cls = LayerClass(top.flags[sy * 256 + sx]);
    // (Not a layer the scene leaves out: that one is not drawn on the screen either.)
    if (cls == LAYER_3D || (cls & scene.items[0].drop)) return none;
    const EdgeContinuation finished = { top.pixels[sy * 256 + sx] & 0xFFFFFF, 16, 0 };
    if (!scene.smartEdges || !top.edge || !top.edgeBlend || !top.edgeFlat)
    {
        // A sprite goes on only as a fill (a wipe, a flash): a snowflake or a rain drop on the
        // edge column would otherwise become a line to the edge of the view (SoulSilver's snow).
        if (cls == LAYER_OBJ)
            for (int k = 1; k < 8; k++)
            {
                const int x = side ? 255 - k : k;
                if (LayerClass(top.flags[sy * 256 + x]) != LAYER_OBJ || ((top.pixels[sy * 256 + x] ^ top.pixels[sy * 256 + sx]) & 0xFFFFFF))
                    return none;
            }
        return finished;
    }
    const uint16_t blend = top.edgeBlend[sy * 2 + side];
    const int eva = blend & 0xFF, evb = blend >> 8;
    if (!(top.edgeFlat[sy * 2 + side] & 1))
    {
        // Not one colour at the edge. A band of one background, not mixed with the world,
        // across the 16 pixels at the edge (the dotted banners with a place's name and the
        // season) goes on as its pattern; anything else (a sprite, a picture) leaves the
        // world showing.
        const uint8_t flag = top.flags[sy * 256 + sx];
        if (evb != 0 || (top.edgeFlat[sy * 2 + side] & 2) || !(cls & 0x0F)) return none;
        for (int k = 0; k < 16; k++)
        {
            const int x = side ? 255 - k : k;
            const int inner = side ? x - 16 : x + 16;
            // A frame edge or advance arrow can occupy one background just like a tiled
            // banner. Repeat only when the next inward tile actually has the same pattern;
            // otherwise Platinum's frame 18 grows extra borders and arrows in the margins.
            if (top.flags[sy * 256 + x] != flag || top.flags[sy * 256 + inner] != flag ||
                scene.items[0].Skips(x, sy) || scene.items[0].Skips(inner, sy) ||
                ((top.pixels[sy * 256 + x] ^ top.pixels[sy * 256 + inner]) & 0xFFFFFF)) return none;
        }
        EdgeContinuation p = finished;
        p.pattern = true;
        return p;
    }
    // Not mixed with the 3D world: as the screen shows it (with any fade or brightening).
    if (evb == 0) return finished;
    return { top.edge[sy * 2 + side] & 0xFFFFFF, eva, evb };
}

// Whether row sy of the top screen is nothing but black backdrop across its whole width: the 3D
// layer is switched off there, as a door's wipe does while it opens or closes. The widescreen
// world, which knows nothing of that, would show through the whole row (the 4:3 middle appeared
// at once while the sides were still being wiped), so the row is black across the view.
inline bool MergedBlankRow(const ScreenImage& top, int sy)
{
    if (!top.flags || !top.pixels || sy < 0 || sy >= 192) return false;
    for (int x = 0; x < 256; x++)
        if (LayerClass(top.flags[sy * 256 + x]) != LAYER_BACKDROP || (top.pixels[sy * 256 + x] & 0xFFFFFF)) return false;
    return true;
}

// Whether the weather's particles are repeated beside the top screen in row sy (see
// Scene::weatherObj): only where neither side continues 2D.
inline bool MergedWeatherRow(const Scene& scene, const ScreenImage& top, int sy)
{
    return MergedEdgeContinuation(scene, top, sy, 0).none() && MergedEdgeContinuation(scene, top, sy, 1).none();
}

// Separate sprites in the outer columns, rather than one tall shape such as the player or a
// follower, are a particle weather (snow, rain, sand). HeartGold and SoulSilver name no
// weather function, unlike Platinum and the Black games, so the picture is what says to
// repeat those sprites beside the screen.
inline bool MergedParticleWeather(const ScreenImage& top)
{
    if (!top.flags) return false;
    int runs = 0;
    for (int side = 0; side < 2; side++)
    {
        bool prev = false;
        for (int y = 0; y < 192; y++)
        {
            bool obj = false;
            for (int k = 0; k < 16 && !obj; k++)
            {
                const int x = side ? 255 - k : k;
                if (LayerClass(top.flags[y * 256 + x]) == LAYER_OBJ) obj = true;
            }
            if (obj && !prev) runs++;
            prev = obj;
        }
    }
    return runs >= 4;
}

// Whether a picture in a bitmap background (not a fill, not blended with the world) reaches a
// third or more of the top screen's left or right edge: the Surf and Fly cut-ins show a
// snapshot of the world that way, which no continuation beside the screen would match.
// (Tiled backgrounds, such as the sea beside a bridge, are scenery, not snapshots.)
inline bool MergedEdgesArePicture(const Scene& scene, const ScreenImage screens[2])
{
    if (scene.items.empty() || scene.items[0].screen < 0) return false;
    const ScreenImage& top = screens[scene.items[0].screen];
    if (!top.flags || !top.edgeBlend || !top.edgeFlat) return false;
    for (int side = 0; side < 2; side++)
    {
        int rows = 0;
        for (int y = 0; y < 192; y++)
        {
            const uint8_t cls = LayerClass(top.flags[y * 256 + (side ? 255 : 0)]);
            if (!scene.items[0].Skips(side ? 255 : 0, y) && cls != LAYER_3D && !(cls & scene.items[0].drop) && top.edgeFlat[y * 2 + side] == 2 && !(top.edgeBlend[y * 2 + side] >> 8))
                rows++;
        }
        if (rows >= 64) return true;
    }
    return false;
}

inline Element FullScreen(int screen)
{
    return { screen, 0, 0, 256, 192, 0, 0, 1.0f, 0, 255, true };
}

inline Element Crop(int screen, int sx, int sy, int sw, int sh)
{
    return { screen, sx, sy, sw, sh, 0, 0, 1.0f, 0, 255, false };
}

// A battle's question ("Use next Pokemon?") stays at full size on the top screen. The two
// answers from the touch screen sit at the right end of the message bar, under the text,
// so nothing covers the battle or the question.
inline Scene BattleChoiceScene(float viewWidth)
{
    Scene sc;
    sc.name = "battle-choice";
    sc.wide = viewWidth >= 300;
    sc.smartEdges = true;
    sc.items = { FullScreen(0) };
    const float right = 128 + viewWidth / 2 - 4;    // the bar's right end
    const float room = right - 170;                 // the question's text ends near x 166
    const float k = std::max(0.3f, std::min(0.42f, room / 252));
    const float h = 47 * k, gap = 2;
    float y = 148 + (41 - (2 * h + gap)) / 2;       // the bar: rows 147 to 189
    for (int top : { 33, 80 })                      // SWITCH POKEMON, then KEEP BATTLING
    {
        Element b = Crop(1, 2, top, 252, 47); // with the red cursor brackets
        b.scale = k;
        b.dx = right - 252 * k;
        b.dy = y;
        sc.items.push_back(b);
        y += h + gap;
    }
    return sc;
}

// Black and Black 2's Bag: the touch screen alone while browsing the pockets; once an item is
// chosen (its card on the top screen), both screens side by side.
inline Scene BagScene(float viewWidth, const ScreenImage* screens)
{
    const float viewLeft = 128 - viewWidth / 2, viewRight = 128 + viewWidth / 2;
    int text = 0;
    if (screens && screens[0].pixels)
        for (int y = 28; y < 152; y += 2)
            for (int x = 8; x < 248; x += 2)
            {
                const uint32_t c = screens[0].pixels[y * 256 + x];
                text += ((c >> 16) & 0xFF) > 200 && ((c >> 8) & 0xFF) > 200 && (c & 0xFF) > 200;
            }
    Scene sc;
    sc.name = "bag";
    Element list = FullScreen(1);
    sc.items = { list };
    if (text >= 12)
    {
        // An item chosen: both screens side by side, as the DS shows them (the card on the
        // top screen, the message and the choices on the touch screen), so that neither covers
        // the other; the rest of the view in the card's own blue.
        const float k = std::min(1.0f, viewWidth / 512);
        Element top = Crop(0, 0, 0, 256, 192), touch = Crop(1, 0, 0, 256, 192);
        top.scale = touch.scale = k;
        top.dx = 128 - 256 * k;
        touch.dx = 128;
        top.dy = touch.dy = 96 - 96 * k;
        top.crisp = touch.crisp = k == 1.0f;
        sc.name = "bag";
        sc.items = { top, touch };
        const uint32_t blue = screens && screens[0].pixels ? screens[0].pixels[100 * 256 + 4] & 0xFFFFFF : 0x103070;
        sc.items.insert(sc.items.begin(), FillRect(viewLeft - 1, -1, (int)std::ceil(viewWidth) + 3, 194, blue));
        return sc;
    }
    sc.bgScreen = 1;
    sc.bgX = 2;
    sc.bgY = 2;
    return sc;
}

// Arranges crops as one column (or one row), scaled together to fill the view.
// Black and Black 2's save screen (SAVE in the X menu): the question, YES / NO, the card of the
// saved game and the progress of the save are all on the touch screen, laid out alike in both
// games. Added to a scene of the overworld: the touch screen's message box (y 2-45) at full size
// along the bottom of the view, where the games' other messages are, and the rest of the touch
// screen (y 46-191: the card, YES / NO over its upper right corner, then when the game was last
// saved or the progress of the save) as a panel in the upper right corner. Each only while it is
// there: the card once its white row of Pokemon is drawn (not over the black of the screen
// before it), the box while it is white inside at its left and right ends, where no text goes
// (at the start and between the question and the save the games have a heading there instead).
inline void SaveScreenPieces(Scene& sc, bool wideView, float viewRight, const ScreenImage* screens)
{
    if (!screens || !screens[1].pixels) return;
    auto lum = [&](int x, int y) {
        const uint32_t c = screens[1].pixels[y * 256 + x];
        return (((c >> 16) & 0xFF) + ((c >> 8) & 0xFF) + (c & 0xFF)) / 3;
    };
    if (lum(20, 108) > 200)
    {
        const float scale = 0.6f, pad = 2;
        Element panel = Crop(1, 0, 46, 256, 146);
        panel.scale = scale;
        panel.dx = (wideView ? viewRight : 256.0f) - 256 * scale - pad - 2;
        panel.dy = pad + 2;
        panel.border = true;
        panel.crisp = true;
        sc.items.push_back(panel);
    }
    if (lum(5, 24) > 230 && lum(250, 24) > 230)
    {
        Element box = Crop(1, 0, 2, 256, 44);
        box.dy = 192 - 44;
        box.crisp = true;
        sc.items.push_back(box);
    }
}

inline Scene Stack(const char* name, std::vector<Element> crops, bool horizontal, float viewWidth)
{
    float w = 0, h = 0;
    for (const Element& e : crops)
    {
        if (horizontal)
        {
            w += e.sw;
            if (e.sh > h) h = (float)e.sh;
        }
        else
        {
            h += e.sh;
            if (e.sw > w) w = (float)e.sw;
        }
    }
    float scale = 192.0f / h;
    if (viewWidth / w < scale) scale = viewWidth / w;
    float x = 128 - w * scale / 2, y = 96 - h * scale / 2;
    Scene sc;
    sc.name = name;
    for (Element e : crops)
    {
        e.scale = scale;
        if (horizontal)
        {
            e.dx = x;
            e.dy = y + (h - e.sh) * scale / 2;
            x += e.sw * scale;
        }
        else
        {
            e.dx = x + (w - e.sw) * scale / 2;
            e.dy = y;
            y += e.sh * scale;
        }
        e.crisp = scale == 1.0f;
        sc.items.push_back(e);
    }
    return sc;
}

// A menu made of a few pieces of the screens (a list, a card) laid out side by side across the
// whole view, on a plain colour: every piece at one scale, as large as fits the view's width and
// height with the given gap between pieces and margin around them, their tops level, the group
// in the middle. Each piece's drop names the layer behind it on its screen (the game's
// decoration), which is left out, so the pieces stand on the plain colour whole and with clean
// edges instead of cut out of a picture. Text and outlines are drawn with nearest pixels.
inline Scene CleanMenu(const char* name, std::vector<Element> pieces, float viewWidth, uint32_t fill,
    float gap = 8, float margin = 8)
{
    float w = gap * (pieces.empty() ? 0 : pieces.size() - 1), h = 0;
    for (const Element& e : pieces)
    {
        w += e.sw;
        h = std::max(h, (float)e.sh);
    }
    const float room = viewWidth - 2 * margin;
    float scale = std::min((room - gap * (pieces.size() - 1)) / (w - gap * (pieces.size() - 1)),
                           (192.0f - 2 * margin) / h);
    float x = 128 - (w - gap * (pieces.size() - 1)) * scale / 2 - gap * (pieces.size() - 1) / 2;
    const float y = 96 - h * scale / 2;
    Scene sc;
    sc.name = name;
    sc.items.push_back(FillRect(128 - viewWidth / 2 - 1, -1, (int)viewWidth + 3, 194, fill));
    for (Element e : pieces)
    {
        e.scale = scale;
        e.dx = x;
        e.dy = y;
        e.crisp = true;
        x += e.sw * scale + gap;
        sc.items.push_back(e);
    }
    return sc;
}

// Puts a smaller side panel to the left of a main piece, sized so that together they fill
// the width of a wide view; on a view too narrow for that, stacks the panel above instead.
inline Scene SideBySide(const char* name, Element side, Element mainEl, float viewWidth)
{
    if (viewWidth < 300) return Stack(name, { side, mainEl }, false, viewWidth);
    // The side panel is drawn at 0.7 of the main piece's scale unless there is room for more.
    float sm = viewWidth / (mainEl.sw + 0.7f * side.sw);
    if (192.0f / mainEl.sh < sm) sm = 192.0f / mainEl.sh;
    float ss = (viewWidth - mainEl.sw * sm) / side.sw;
    if (ss > sm) ss = sm;
    float x = 128 - (side.sw * ss + mainEl.sw * sm) / 2;
    side.scale = ss;
    side.dx = x;
    side.dy = 96 - side.sh * ss / 2;
    side.crisp = false;
    mainEl.scale = sm;
    mainEl.dx = x + side.sw * ss;
    mainEl.dy = 96 - mainEl.sh * sm / 2;
    mainEl.crisp = sm == 1.0f;
    Scene sc;
    sc.name = name;
    sc.items = { side, mainEl };
    return sc;
}

// Builds an overlay from whatever is left of a screen once the given layers are
// dropped: finds the bounding box of the remaining pixels (ignoring the corner at and
// beyond cutX, cutY), scales
// it down if it is larger than maxW x maxH, and puts it where it hides the fewest
// pixels of the `protect` layers on the screen underneath. Returns false if nothing
// is left to draw.
inline bool SmartOverlay(const ScreenImage screens[2], int screen, int cutX, int cutY, uint8_t drop,
    float maxW, float maxH, uint8_t protect, uint8_t alpha, Element& out)
{
    const uint8_t* flags = screens[screen].flags;
    if (!flags) return false;
    int x0 = 256, y0 = 192, x1 = -1, y1 = -1;
    for (int y = 0; y < 192; y++)
        for (int x = 0; x < 256; x++)
            if (!(x >= cutX && y >= cutY) && !(LayerClass(flags[y * 256 + x]) & drop))
            {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    if (x1 < x0 || y1 < y0) return false;
    // Snap outwards to a coarse grid so small animations do not move the overlay.
    x0 &= ~7;
    y0 &= ~7;
    x1 = (x1 | 7) < 255 ? (x1 | 7) : 255;
    y1 = (y1 | 7) < 191 ? (y1 | 7) : 191;
    int sw = x1 - x0 + 1, sh = y1 - y0 + 1;

    float fit = 1.0f;
    if (maxW / sw < fit) fit = maxW / sw;
    if (maxH / sh < fit) fit = maxH / sh;

    // Candidate anchors: a 3x3 grid, scored by how much protected content they cover.
    // If every position at full size hides a lot, smaller sizes are tried as well.
    const uint8_t* under = screens[1 - screen].flags;
    float best = 1e30f, bestX = 0, bestY = 0, scale = fit;
    static const float shrink[] = { 1.0f, 0.8f, 0.65f };
    for (float k : shrink)
    {
        float sc = fit * k, w = sw * sc, h = sh * sc;
        for (int ay = 0; ay < 3; ay++)
            for (int ax = 0; ax < 3; ax++)
            {
                float px = (256 - w) * ax / 2, py = (192 - h) * ay / 2;
                float cost = 0;
                if (under)
                    for (int y = (int)py; y < (int)(py + h) && y < 192; y += 2)
                        for (int x = (int)px; x < (int)(px + w) && x < 256; x += 2)
                            if (LayerClass(under[y * 256 + x]) & protect) cost += 1;
                // Mild preferences: larger, to the right, and off the centre.
                cost += (1.0f - k) * 40 + (2 - ax) * 0.5f + (ax == 1 && ay == 1 ? 2.0f : 0.0f);
                if (cost < best)
                {
                    best = cost;
                    bestX = px;
                    bestY = py;
                    scale = sc;
                }
            }
    }
    out = { screen, x0, y0, sw, sh, bestX, bestY, scale, drop, alpha, false, cutX, cutY };
    return true;
}

inline bool MergedKeyed(const Element& el, uint32_t rgb)
{
    if (rgb == el.keys[0] || rgb == el.keys[1] || rgb == el.keys[2] || rgb == el.keys[3]) return true;
    if (!el.keyTolerance) return false;
    int t = el.keyTolerance, k = (int)el.keys[0];
    return std::abs((int)((rgb >> 16) & 0xFF) - ((k >> 16) & 0xFF)) <= t &&
        std::abs((int)((rgb >> 8) & 0xFF) - ((k >> 8) & 0xFF)) <= t && std::abs((int)(rgb & 0xFF) - (k & 0xFF)) <= t;
}

// With `smooth` off every piece is drawn with nearest-pixel sampling (hard edges, and fast);
// with it on, pieces that are not marked crisp are filtered. The rows of the picture are
// shared out between a few threads.
// Whether the widescreen world has something drawn where the top screen's pixel (sx, sy) is
// shown. Over the world, the top screen's backdrop (the colour behind everything) is left out
// wherever the world has something: the DS's own 3D can leave a hairline gap where two
// polygons meet (along the road of Skyarrow Bridge), and the backdrop showing through it there
// would be drawn over the world's floor, enlarged into a line.
inline bool MergedWideCovers(const WideImage& wide, const MergedView& v, int outW, int outH, int sx, int sy)
{
    const int x = (int)((v.originX + (sx + 0.5f) * v.unit) * wide.w / outW);
    const int y = (int)((v.originY + (sy + 0.5f) * v.unit) * wide.h / outH);
    if (x < 0 || x >= wide.w || y < 0 || y >= wide.h) return false;
    return (wide.pixels[(size_t)y * wide.w + x] >> 24) != 0;
}

// The widescreen 3D picture with its holes beside the screen filled: where the game drew
// nothing further out (a white flash drawn as one polygon the size of the DS's view, the
// ground ending where the game stopped drawing it), each row goes on in the last colour drawn
// on the way out from the screen's edge. Rows that have nothing drawn at the edge itself (the
// sky of a 2D background behind the world) are left as they are: what continues there is the
// screen's own edge (MergedEdgeContinuation). In px: a copy; screenX0, screenX1: the columns
// of the picture where the DS's screen begins and ends.
inline const uint32_t* MergedFillWideHoles(const WideImage& wide, int screenX0, int screenX1, std::vector<uint32_t>& px)
{
    const int w = wide.w, h = wide.h;
    screenX0 = std::clamp(screenX0, 0, w);
    screenX1 = std::clamp(screenX1, 0, w);
    bool holes = false;
    for (int y = 0; y < h && !holes; y++)
    {
        const uint32_t* row = wide.pixels + (size_t)y * w;
        holes = (screenX0 > 0 && !(row[0] >> 24)) || (screenX1 < w && !(row[w - 1] >> 24));
    }
    if (!holes) return wide.pixels;
    // Not where a room ends: indoors, a floor that ends inside the DS's own view has nothing
    // under it down to the bottom of the picture, while outdoors the ground always reaches the
    // bottom (the camera looks down at it). There the gap beside a wall is the room's real end,
    // and going on in the wall's last colour drew it out into stripes (GitHub #60).
    for (int x = screenX0; x < screenX1; x += 4)
    {
        if (wide.pixels[(size_t)(h - 1) * w + x] >> 24) continue;
        for (int y = h - 2; y >= 0; y--)
            if (wide.pixels[(size_t)y * w + x] >> 24) return wide.pixels;
    }
    px.assign(wide.pixels, wide.pixels + (size_t)w * h);
    for (int y = 0; y < h; y++)
    {
        uint32_t* row = &px[(size_t)y * w];
        uint32_t last = 0;
        for (int x = screenX0 - 1; x >= 0; x--)
        {
            if (row[x] >> 24) last = row[x];
            else if (last) row[x] = last | 0xFF000000;
        }
        last = 0;
        for (int x = screenX1; x < w; x++)
        {
            if (row[x] >> 24) last = row[x];
            else if (last) row[x] = last | 0xFF000000;
        }
    }
    return px.data();
}

// A picture shown 4:3 on a wide view (Scene::narrow, in the middle of the world: a battle's
// opening with a snapshot of the screen, a flash, a cut-in): the bars beside it go on in soft
// colours from the picture's edges, each row the average of the 8 pixels at that edge over
// the 17 rows around it, rather than black, so that the view does not seem to shrink.
// calm: the bars beside a 2D picture that covers the world for a moment (a door's zoom, a key
// item's cut-in, Fly): averaged over 81 rows instead of 17 and darkened by a third, so that the
// trees and houses at the picture's edges are not drawn out into streaks across the bars.
inline void MergedNarrowBars(const ScreenImage& s, uint32_t left[192], uint32_t right[192], bool calm = false)
{
    const int reach = calm ? 40 : 8;
    const uint32_t keep = calm ? 170 : 256; // (in 256ths)
    for (int side = 0; side < 2; side++)
    {
        uint32_t rowSum[192][3];
        for (int y = 0; y < 192; y++)
        {
            uint32_t r = 0, g = 0, b = 0;
            for (int k = 0; k < 8; k++)
            {
                const uint32_t c = s.pixels[y * 256 + (side ? 255 - k : k)];
                r += (c >> 16) & 0xFF;
                g += (c >> 8) & 0xFF;
                b += c & 0xFF;
            }
            rowSum[y][0] = r;
            rowSum[y][1] = g;
            rowSum[y][2] = b;
        }
        uint32_t* out = side ? right : left;
        for (int y = 0; y < 192; y++)
        {
            uint32_t r = 0, g = 0, b = 0, n = 0;
            for (int k = std::max(0, y - reach); k <= std::min(191, y + reach); k++, n += 8)
            {
                r += rowSum[k][0];
                g += rowSum[k][1];
                b += rowSum[k][2];
            }
            out[y] = 0xFF000000 | (r / n * keep >> 8) << 16 | (g / n * keep >> 8) << 8 | (b / n * keep >> 8);
        }
    }
}

// Black where ASPECT RATIO leaves the output beside the view.
inline void MergedBlackBars(std::vector<uint32_t>& out, int outW, int outH)
{
    int x0, x1;
    MergedViewColumns(outW, outH, x0, x1);
    if (x0 <= 0 && x1 >= outW) return;
    for (int y = 0; y < outH; y++)
    {
        uint32_t* row = &out[(size_t)y * outW];
        std::fill(row, row + x0, 0xFF000000u);
        std::fill(row + x1, row + outW, 0xFF000000u);
    }
}

inline void MergedCompose(const Scene& scene, const ScreenImage screens[2], int outW, int outH, std::vector<uint32_t>& out,
    bool smooth = false, const WideImage* wide = nullptr)
{
    uint32_t bg = 0xFF000000;
    if (scene.bgScreen >= 0) bg |= screens[scene.bgScreen].pixels[scene.bgY * 256 + scene.bgX] & 0xFFFFFF;
    const bool useWide = scene.wide && !scene.narrow && wide && wide->pixels && wide->w > 0 && wide->h > 0;
    out.resize((size_t)outW * outH);
    const MergedView v = MergedMakeView(outW, outH);

    const bool placed = useWide && wide->placed;
    std::vector<uint32_t> wideFilled;
    const uint32_t* widePx = useWide && !placed
        ? MergedFillWideHoles(*wide, (int)std::floor(v.originX * wide->w / outW),
              (int)std::ceil((v.originX + 256 * v.unit) * wide->w / outW), wideFilled)
        : nullptr;
    std::vector<int> wcol;
    if (useWide)
    {
        wcol.resize(outW);
        for (int x = 0; x < outW; x++)
            wcol[x] = placed ? (int)std::floor((x + 0.5f - v.originX) / v.unit) - wide->baseX : (int)((int64_t)x * wide->w / outW);
    }

    // Each piece with its place in the output and, for nearest-pixel drawing, the source
    // column of every output column.
    struct Piece
    {
        Element el;
        float s, sx; // scale down and across
        int x0, y0, w, h, xa, xb, border;
        bool nearest;
        std::vector<int> col;
    };
    std::vector<Piece> pieces;
    pieces.reserve(scene.items.size());
    for (const Element& item : scene.items)
    {
        if (placed && &item == &scene.items[0]) continue;
        if (item.wideOnly && !useWide) continue;
        Piece p;
        p.el = item;
        // Over a widescreen world only the 2D parts of the top screen are drawn.
        if (useWide && !placed && pieces.empty()) p.el.drop |= LAYER_3D;
        if (!useWide) p.el.holeCount = 0;
        p.s = v.unit * p.el.scale;
        p.sx = p.s * p.el.stretchX;
        p.x0 = (int)(v.originX + p.el.dx * v.unit);
        p.y0 = (int)(v.originY + p.el.dy * v.unit);
        p.w = (int)(p.el.sw * p.sx + 0.5f);
        p.h = (int)(p.el.sh * p.s + 0.5f);
        p.border = 0;
        if (p.el.border)
        {
            p.border = (int)(v.unit * (p.el.borderWidth ? p.el.borderWidth : 6) / 8.0f + 0.5f);
            if (p.border < 1) p.border = 1;
        }
        p.nearest = p.el.crisp || !smooth;
        p.xa = p.x0 < 0 ? -p.x0 : 0;
        p.xb = p.x0 + p.w > outW ? outW - p.x0 : p.w;
        if (p.nearest && p.xb > p.xa)
        {
            p.col.resize(p.w);
            for (int x = p.xa; x < p.xb; x++)
            {
                int sx = (int)((x + 0.5f) / p.sx);
                if (sx >= p.el.sw) sx = p.el.sw - 1;
                p.col[x] = p.el.sx + (p.el.flipX ? p.el.sw - 1 - sx : sx);
            }
        }
        pieces.push_back(std::move(p));
    }

    uint32_t narrowBar[2][192];
    const bool narrowBars = (scene.softBars || (scene.wide && scene.narrow)) && !scene.items.empty() && scene.items[0].screen >= 0 &&
        v.originX > 0;
    if (narrowBars) MergedNarrowBars(screens[scene.items[0].screen], narrowBar[0], narrowBar[1], scene.wide && scene.narrow);

    auto rows = [&](int ya, int yb) {
        // Background: one colour, or the widescreen 3D picture with the game's fade applied.
        if (!useWide)
        {
            std::fill(out.begin() + (size_t)ya * outW, out.begin() + (size_t)yb * outW, bg);
            if (narrowBars)
                for (int y = ya; y < yb; y++)
                {
                    const int sy = std::clamp((int)((y - v.originY) / v.unit), 0, 191);
                    uint32_t* dst = &out[(size_t)y * outW];
                    std::fill(dst, dst + std::min((int)v.originX, outW), narrowBar[0][sy]);
                    std::fill(dst + std::max((int)(v.originX + 256 * v.unit), 0), dst + outW, narrowBar[1][sy]);
                }
            const ScreenImage& top = screens[scene.items.empty() || scene.items[0].screen < 0 ? 0 : scene.items[0].screen];
            const int left = (int)v.originX, right = (int)(v.originX + 256 * v.unit);
            if (scene.edgeFill && left > 0)
                for (int y = ya; y < yb; y++)
                {
                    int sy = (int)((y - v.originY) / v.unit);
                    if (sy < 0 || sy >= 192) continue;
                    uint32_t* dst = &out[(size_t)y * outW];
                    for (int side = 0; side < 2; side++)
                    {
                        const uint32_t c = EdgeFillColour(top, sy, side);
                        const int x0 = side ? right : 0, x1 = side ? outW : left;
                        for (int x = x0; x < x1; x++)
                        {
                            // 256 beside the screen, EdgeFillOuter at the edge of the view.
                            const int d = side ? x - right : left - 1 - x, span = side ? outW - right : left;
                            const uint32_t k = 256 - (256 - EdgeFillOuter) * d / (span > 1 ? span - 1 : 1);
                            dst[x] = 0xFF000000 | ((((c >> 16) & 0xFF) * k >> 8) << 16) | ((((c >> 8) & 0xFF) * k >> 8) << 8) | ((c & 0xFF) * k >> 8);
                        }
                    }
                }
        }
        else if (placed)
        {
            for (int y = ya; y < yb; y++)
            {
                uint32_t* dst = &out[(size_t)y * outW];
                const int sy = (int)std::floor((y + 0.5f - v.originY) / v.unit);
                if (sy < 0 || sy >= wide->h)
                {
                    std::fill(dst, dst + outW, 0xFF000000);
                    continue;
                }
                const uint32_t* row = wide->pixels + (size_t)sy * wide->w;
                for (int x = 0; x < outW; x++)
                {
                    const int c = wcol[x];
                    dst[x] = (c >= 0 && c < wide->w) ? (0xFF000000 | row[c]) : 0xFF000000;
                }
            }
        }
        else
        {
            const int f = wide->fade, mode = wide->fadeMode;
            for (int y = ya; y < yb; y++)
            {
                const uint32_t* row = widePx + (size_t)((int64_t)y * wide->h / outH) * wide->w;
                uint32_t* dst = &out[(size_t)y * outW];
                if (!mode || !f)
                    for (int x = 0; x < outW; x++)
                    {
                        uint32_t c = row[wcol[x]];
                        dst[x] = (c >> 24) ? (0xFF000000 | c) : 0xFF000000;
                    }
                else
                    for (int x = 0; x < outW; x++)
                    {
                        uint32_t c = row[wcol[x]];
                        if (!(c >> 24)) c = 0;
                        uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
                        if (mode == 1)
                        {
                            r += (255 - r) * f / 16;
                            g += (255 - g) * f / 16;
                            b += (255 - b) * f / 16;
                        }
                        else
                        {
                            r = r * (16 - f) / 16;
                            g = g * (16 - f) / 16;
                            b = b * (16 - f) / 16;
                        }
                        dst[x] = 0xFF000000 | (r << 16) | (g << 8) | b;
                    }
                // Where something 2D reaches an edge of the top screen (a wipe closing in, a
                // flash, a full-screen cover), it is continued to the edge of the view;
                // otherwise the world would still show beside it.
                const ScreenImage& top = screens[scene.items[0].screen];
                if (top.flags)
                {
                    int sy = (int)((y - v.originY) / v.unit);
                    if (sy >= 0 && sy < 192)
                    {
                        if (MergedBlankRow(top, sy)) std::fill(dst, dst + outW, 0xFF000000);
                        const int left = (int)v.originX, right = (int)(v.originX + 256 * v.unit);
                        for (int side = 0; side < 2; side++)
                        {
                            const EdgeContinuation c = MergedEdgeContinuation(scene, top, sy, side);
                            if (c.none()) continue;
                            const int xa = side ? std::max(right, 0) : 0, xb = side ? outW : std::min(left, outW);
                            if (c.pattern)
                            {
                                for (int x = xa; x < xb; x++)
                                {
                                    const int bx = (int)std::floor((x - v.originX) / v.unit);
                                    dst[x] = 0xFF000000 | MergedEdgePatternPixel(top, sy, side ? std::max(bx, 256) : std::min(bx, -1));
                                }
                                continue;
                            }
                            if (c.evb == 0 && c.eva == 16)
                            {
                                if (xa < xb) std::fill(dst + xa, dst + xb, 0xFF000000 | c.rgb);
                                continue;
                            }
                            const uint32_t r = ((c.rgb >> 16) & 0xFF) * c.eva, g = ((c.rgb >> 8) & 0xFF) * c.eva, b = (c.rgb & 0xFF) * c.eva;
                            for (int x = xa; x < xb; x++)
                            {
                                const uint32_t d = dst[x];
                                const uint32_t nr = std::min<uint32_t>(255, (r + ((d >> 16) & 0xFF) * c.evb) / 16);
                                const uint32_t ng = std::min<uint32_t>(255, (g + ((d >> 8) & 0xFF) * c.evb) / 16);
                                const uint32_t nb = std::min<uint32_t>(255, (b + (d & 0xFF) * c.evb) / 16);
                                dst[x] = 0xFF000000 | nr << 16 | ng << 8 | nb;
                            }
                        }
                        // The weather's particles beside the screen (Scene::weatherObj).
                        if (scene.weatherObj)
                            for (int side = 0; side < 2; side++)
                            {
                                if (!MergedWeatherRow(scene, top, sy)) break;
                                const int xa = side ? std::max(right, 0) : 0, xb = side ? outW : std::min(left, outW);
                                for (int x = xa; x < xb; x++)
                                {
                                    uint32_t rgb;
                                    if (MergedWeatherPixel(top, (int)std::floor((x - v.originX) / v.unit), sy, rgb))
                                        dst[x] = 0xFF000000 | rgb;
                                }
                            }
                    }
                }
            }
        }

        for (const Piece& p : pieces)
        {
            const Element& el = p.el;
            const int x0 = p.x0, y0 = p.y0, w = p.w, h = p.h;
            if (p.border)
            {
                const int t = p.border;
                const float r = el.borderRadius * v.unit;
                for (int y = std::max(ya, y0 - t); y < std::min(yb, y0 + h + t); y++)
                    OutlineRow(x0, y0, w, h, t, r, y, [&](int xa, int xb) {
                        for (int x = std::max(xa, 0); x < std::min(xb, outW); x++) out[(size_t)y * outW + x] = 0xFF000000 | el.borderColor;
                    });
            }
            if (el.screen < 0)
            {
                // A rectangle of one colour, with its corners rounded like its outline.
                const float r = el.borderRadius * v.unit;
                for (int y = std::max(ya, y0); y < std::min(yb, y0 + h); y++)
                {
                    int xa = x0, xb = x0 + w;
                    if (r > 0) RoundedRowExtent(x0, y0, w, h, r, y, xa, xb);
                    if (el.alpha == 255)
                        for (int x = std::max(0, xa); x < std::min(outW, xb); x++)
                            out[(size_t)y * outW + x] = 0xFF000000 | el.keys[0];
                    else
                    {
                        // (Translucent: a shade over what is below, e.g. a button not chosen.)
                        const uint32_t al = el.alpha, ia = 255 - al, c = el.keys[0];
                        for (int x = std::max(0, xa); x < std::min(outW, xb); x++)
                        {
                            uint32_t& d = out[(size_t)y * outW + x];
                            const uint32_t r = (((d >> 16) & 0xFF) * ia + ((c >> 16) & 0xFF) * al) / 255;
                            const uint32_t g = (((d >> 8) & 0xFF) * ia + ((c >> 8) & 0xFF) * al) / 255;
                            const uint32_t b = ((d & 0xFF) * ia + (c & 0xFF) * al) / 255;
                            d = 0xFF000000 | (r << 16) | (g << 8) | b;
                        }
                    }
                }
                continue;
            }
            const ScreenImage& src = screens[el.screen];
            const float s = p.s, sX = p.sx;
            // (Over the world, the first piece's backdrop where the world has something is left
            // out: see MergedWideCovers.)
            const bool overWorld = useWide && !placed && &p == &pieces[0];
            const uint32_t* widePxRaw = overWorld ? wide->pixels : nullptr;

            const int oya = std::max(ya, y0), oyb = std::min(yb, y0 + h);
            if (p.nearest)
            {
                // Nearest-pixel path: one table lookup per output pixel. This is the path used
                // unless smooth scaling is on, and it has to stay cheap at 1440p and above.
                const int xa = p.xa, xb = p.xb;
                if (xb <= xa) continue;
                const int* col = p.col.data();
                const bool keyed = el.keys[0] != 0xFFFFFFFF;
                const bool spriteKey = el.dropSpriteCorner > 0 && src.flags;
                const bool plain = !(el.drop && src.flags) && !keyed && !spriteKey && el.alpha == 255 && el.cutX >= 256 && !el.holeCount;
                for (int oy = oya; oy < oyb; oy++)
                {
                    int sy = (int)((oy - y0 + 0.5f) / s);
                    if (sy >= el.sh) sy = el.sh - 1;
                    if (el.flipY) sy = el.sh - 1 - sy;
                    sy += el.sy;
                    const uint32_t* row = src.pixels + sy * 256;
                    const uint8_t* frow = src.flags ? src.flags + sy * 256 : nullptr;
                    uint32_t* dst = out.data() + (ptrdiff_t)oy * outW + x0;
                    if (plain)
                    {
                        for (int x = xa; x < xb; x++) dst[x] = 0xFF000000 | row[col[x]];
                        continue;
                    }
                    for (int x = xa; x < xb; x++)
                    {
                        int sx = col[x];
                        if (el.Skips(sx, sy)) continue;
                        if (el.drop && frow && (LayerClass(frow[sx]) & el.drop)) continue;
                        if (overWorld && frow && LayerClass(frow[sx]) == LAYER_BACKDROP &&
                            (widePxRaw[(size_t)((int64_t)oy * wide->h / outH) * wide->w + wcol[x0 + x]] >> 24))
                            continue;
                        uint32_t c = row[sx];
                        if (spriteKey && LayerClass(frow[sx]) == LAYER_OBJ &&
                            std::min(sx - el.sx, el.sx + el.sw - 1 - sx) < el.dropSpriteCorner &&
                            std::min(sy - el.sy, el.sy + el.sh - 1 - sy) < el.dropSpriteCorner)
                            continue;
                        if (keyed && MergedKeyed(el, c & 0xFFFFFF) &&
                            (!el.keyRing || std::min({ sx - el.sx, el.sx + el.sw - 1 - sx, sy - el.sy, el.sy + el.sh - 1 - sy }) < el.keyRing))
                            continue;
                        if (el.alpha == 255)
                            dst[x] = 0xFF000000 | c;
                        else
                        {
                            uint32_t d = dst[x], al = el.alpha, ia = 255 - al;
                            uint32_t r = (((c >> 16) & 0xFF) * al + ((d >> 16) & 0xFF) * ia) / 255;
                            uint32_t g = (((c >> 8) & 0xFF) * al + ((d >> 8) & 0xFF) * ia) / 255;
                            uint32_t bl = ((c & 0xFF) * al + (d & 0xFF) * ia) / 255;
                            dst[x] = 0xFF000000 | (r << 16) | (g << 8) | bl;
                        }
                    }
                }
                continue;
            }

            for (int oy = oya; oy < oyb; oy++)
            {
                // Filtered sample position in source pixels, centred.
                float fy = (oy - y0 + 0.5f) / s - 0.5f;
                if (fy < 0) fy = 0;
                int iy = (int)fy;
                float wy = fy - iy;
                int iy1 = iy + 1 < el.sh ? iy + 1 : iy;
                for (int x = 0; x < w; x++)
                {
                    int ox = x0 + x;
                    if (ox < 0 || ox >= outW) continue;
                    float fx = (x + 0.5f) / sX - 0.5f;
                    if (fx < 0) fx = 0;
                    int ix = (int)fx;
                    float wx = fx - ix;
                    int ix1 = ix + 1 < el.sw ? ix + 1 : ix;

                    float acc[3] = { 0, 0, 0 }, cover = 0;
                    const int px[4] = { ix, ix1, ix, ix1 };
                    const int py[4] = { iy, iy, iy1, iy1 };
                    const float wt[4] = { (1 - wx) * (1 - wy), wx * (1 - wy), (1 - wx) * wy, wx * wy };
                    for (int k = 0; k < 4; k++)
                    {
                        int srcX = el.sx + (el.flipX ? el.sw - 1 - px[k] : px[k]);
                        int srcY = el.sy + (el.flipY ? el.sh - 1 - py[k] : py[k]);
                        if (el.Skips(srcX, srcY)) continue;
                        int si = srcY * 256 + srcX;
                        if (el.drop && src.flags && (LayerClass(src.flags[si]) & el.drop)) continue;
                        if (overWorld && src.flags && LayerClass(src.flags[si]) == LAYER_BACKDROP &&
                            MergedWideCovers(*wide, v, outW, outH, srcX, srcY))
                            continue;
                        uint32_t c = src.pixels[si];
                        if (MergedKeyed(el, c & 0xFFFFFF)) continue;
                        acc[0] += ((c >> 16) & 0xFF) * wt[k];
                        acc[1] += ((c >> 8) & 0xFF) * wt[k];
                        acc[2] += (c & 0xFF) * wt[k];
                        cover += wt[k];
                    }
                    if (cover <= 0.001f) continue;

                    uint32_t& d = out[(size_t)oy * outW + ox];
                    float a = cover * el.alpha / 255.0f;
                    float dr = ((d >> 16) & 0xFF) * (1 - a) + (acc[0] / cover) * a;
                    float dg = ((d >> 8) & 0xFF) * (1 - a) + (acc[1] / cover) * a;
                    float db = (d & 0xFF) * (1 - a) + (acc[2] / cover) * a;
                    d = 0xFF000000 | ((uint32_t)dr << 16) | ((uint32_t)dg << 8) | (uint32_t)db;
                }
            }
        }
    };

    // Small pictures are not worth starting threads for.
    int threads = (size_t)outW * outH < 400000 ? 1 : (int)std::thread::hardware_concurrency() / 2;
    if (threads < 1) threads = 1;
    if (threads > 6) threads = 6;
    std::vector<std::thread> pool;
    for (int i = 1; i < threads; i++) pool.emplace_back(rows, outH * i / threads, outH * (i + 1) / threads);
    rows(0, outH / threads);
    for (auto& t : pool) t.join();
}

// Maps a position in base pixels to touchscreen coordinates, through whichever
// bottom-screen element is topmost there.
inline bool MergedTouch(const Scene& scene, float bx, float by, int& tx, int& ty)
{
    for (int i = (int)scene.items.size() - 1; i >= 0; i--)
    {
        const Element& el = scene.items[i];
        float lx = (bx - el.dx) / (el.scale * el.stretchX), ly = (by - el.dy) / el.scale;
        if (lx < 0 || ly < 0 || lx >= el.sw || ly >= el.sh) continue;
        if (el.screen != 1) continue; // top-screen elements and plain rectangles are not touchable but do not block
        tx = el.sx + (int)lx;
        ty = el.sy + (int)ly;
        return true;
    }
    return false;
}
