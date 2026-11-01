// Battles drawn across a wide view.
//
// A battle's top screen is a background map (with the two platforms on it), the Pokémon
// in 3D, and the HP boxes as sprites that run off the left and right edges of the screen.
// The background map is 512 pixels wide and the game only shows its left half; the right
// half is more of the same sky and ground. So the picture is widened by drawing that map
// beyond both edges of the screen (wrapping around it as the hardware would), and the HP
// boxes, which belong to the edges of the screen, are moved out to the edges of the view.
// Everything else is the screen exactly as the game drew it.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include "NDS.h"
#include "GPU2D_Soft.h"
#include "merged.h"

class BattleWide
{
public:
    struct Layer { int num, prio; bool c256; int mapH; melonDS::u32 charBase, mapBase; };

    // Everything the picture is made from, as the frame left it: taken by Grab right after the
    // frame (quick), used by Render (the slow part). With `keep`, Grab copies it all, so that
    // Render can run while the next frame is emulated and changes the console's state (the
    // pipeline's drawing job); without, it points at the console's state.
    struct Capture
    {
        const melonDS::NDS* nds = nullptr; // set: VRAM read from the console, not the copies
        int unitNum = 0, extend = 0;
        float viewWidth = 0;
        melonDS::u32 dispcnt = 0;
        melonDS::u16 bgcnt[4] = {};
        melonDS::u8 shown = 0;
        Layer layers[4] = {};
        int nLayers = 0;
        uint8_t windowLayers = 0, layerMask = 0;
        const uint32_t* pixels = nullptr;   // the top screen
        const uint8_t* flags = nullptr;     // and its layer flags
        const melonDS::u8* objPrio = nullptr;
        const melonDS::u16 (*lineScroll)[4][2] = nullptr;
        const melonDS::u16 (*lineBlend)[3] = nullptr;
        const melonDS::u8* pal = nullptr;   // BG at 0, sprites at 0x200
        const melonDS::u8* oam = nullptr;
        WideImage world;                    // pixels null: none
        // The copies (keep).
        std::vector<uint32_t> pixelsCopy, worldCopy;
        std::vector<uint8_t> flagsCopy, objPrioCopy, bgVram, objVram;
        melonDS::u16 lineScrollCopy[192][4][2];
        melonDS::u16 lineBlendCopy[192][3];
        melonDS::u8 palCopy[0x400], oamCopy[0x400];

        melonDS::u16 Bg16(melonDS::u32 a) const
        {
            if (nds) return unitNum == 0 ? nds->GPU.ReadVRAM_ABG<melonDS::u16>(a) : nds->GPU.ReadVRAM_BBG<melonDS::u16>(a);
            melonDS::u16 v;
            memcpy(&v, &bgVram[a & (bgVram.size() - 1) & ~1u], 2);
            return v;
        }
        melonDS::u8 Bg8(melonDS::u32 a) const
        {
            if (nds) return unitNum == 0 ? nds->GPU.ReadVRAM_ABG<melonDS::u8>(a) : nds->GPU.ReadVRAM_BBG<melonDS::u8>(a);
            return bgVram[a & (bgVram.size() - 1)];
        }
        melonDS::u8 Obj8(melonDS::u32 a) const
        {
            if (nds) return nds->GPU.ReadVRAM_OBJ<melonDS::u8>(a);
            return objVram[a & (objVram.size() - 1)];
        }
    };

    // Builds the picture for a view `extend` base pixels wider than the screen on each side.
    // `world` is the widescreen 3D picture across the whole view (`viewWidth` base pixels),
    // if there is one: beside the screen the 3D parts of the battle come from it (the grass
    // sweeping past as a battle opens, a Pokémon or trainer crossing the edge). Returns
    // false (and the battle is drawn as before) if the top screen is not built the way
    // described above.
    bool Build(melonDS::NDS& nds, const ScreenImage& top, int extend, const WideImage* world, float viewWidth, WideImage& out, int unitNum)
    {
        if (!Grab(nds, top, extend, world, viewWidth, unitNum, false)) return false;
        Render(out);
        return true;
    }

    // The two halves of Build. Grab returns false if the top screen is not a battle's (Render
    // must then not be called); the capture stays valid until the next Grab.
    bool Grab(melonDS::NDS& nds, const ScreenImage& top, int extend, const WideImage* world, float viewWidth, int unitNum, bool keep)
    {
        using namespace melonDS;
        Capture& C = Cap;
        if (extend <= 0 || !top.flags) return false;
        const GPU2D::Unit& unit = unitNum == 0 ? nds.GPU.GPU2D_A : nds.GPU.GPU2D_B;
        const u32 dispcnt = unit.DispCnt;
        if ((dispcnt & 7) != 0 || ((dispcnt >> 16) & 3) != 1) return false; // all text layers, normal display

        if ((dispcnt & (1u << 30))) return false;                   // extended palettes: not handled

        // Every layer that is 512 pixels wide is drawn beyond the screen (the main background,
        // and in the opening of a battle the grass sweeping past); a narrower one would only
        // repeat itself. Front to back, as the 2D engine stacks them.
        // With windows on, the area beside the screen is outside them all, and shows only what
        // the game lets through there (a move's effect is shown through a window shaped like
        // the Pokémon, with its background hidden elsewhere).
        const u8 shown = (dispcnt & 0xE000) ? unit.WinCnt[2] : 0x3F;
        Layer* layers = C.layers;
        int nLayers = 0;
        for (int prio = 0; prio < 4; prio++)
            for (int b = 0; b < 4; b++)
            {
                const u16 cnt = unit.BGCnt[b];
                if (!(dispcnt & (0x100 << b)) || !(shown & (1 << b)) || (b == 0 && (dispcnt & 8)) || (cnt & 3) != prio || !(cnt & 0x4000)) continue;
                Layer& l = layers[nLayers++];
                l.num = b;
                l.prio = prio;
                l.c256 = cnt & 0x80;
                l.mapH = (cnt & 0x8000) ? 512 : 256;
                l.charBase = ((cnt >> 2) & 0xF) * 0x4000;
                l.mapBase = ((cnt >> 8) & 0x1F) * 0x800;
                if (unitNum == 0)
                {
                    l.charBase += ((dispcnt >> 24) & 7) * 0x10000;
                    l.mapBase += ((dispcnt >> 27) & 7) * 0x10000;
                }
            }
        // Windows (the stats shown after a level up): a background whose pixels on the screen
        // fill the rectangle they are in, away from both edges of the screen, whatever the
        // size of its map. They are not continued beyond the screen, stay where the game
        // put them, in front, and are not moved with the wild Pokémon's side (which would
        // cut one in two at the bottom of its rows).
        uint8_t windowLayers = 0;
        {
            // One pass over the screen for all four backgrounds.
            int n[4] = {}, x0[4] = { 256, 256, 256, 256 }, x1[4] = { -1, -1, -1, -1 };
            int y0[4] = { 192, 192, 192, 192 }, y1[4] = { -1, -1, -1, -1 };
            static const int8_t single[16] = { -1, 0, 1, -1, 2, -1, -1, -1, 3, -1, -1, -1, -1, -1, -1, -1 };
            for (int y = 0; y < 192; y++)
                for (int x = 0; x < 256; x++)
                {
                    const uint8_t c = LayerClass(top.flags[y * 256 + x]);
                    if (c > 0x0F) continue;
                    const int b = single[c];
                    if (b < 0) continue;
                    n[b]++;
                    x0[b] = std::min(x0[b], x); x1[b] = std::max(x1[b], x);
                    y0[b] = std::min(y0[b], y); y1[b] = std::max(y1[b], y);
                }
            for (int b = 0; b < 4; b++)
                if (n[b] >= 256 && x0[b] > 0 && x1[b] < 255 && n[b] * 10 >= (x1[b] - x0[b] + 1) * (y1[b] - y0[b] + 1) * 9)
                    windowLayers |= 1 << b;
        }
        {
            int kept = 0;
            for (int i = 0; i < nLayers; i++)
                if (!(windowLayers & (1 << layers[i].num))) layers[kept++] = layers[i];
            nLayers = kept;
        }
        uint8_t layerMask = 0;
        for (int i = 0; i < nLayers; i++) layerMask |= 1 << layers[i].num;
        if (!nLayers) return false;                                 // nothing more to show beside the screen

        C.unitNum = unitNum;
        C.extend = extend;
        C.viewWidth = viewWidth;
        C.dispcnt = dispcnt;
        for (int b = 0; b < 4; b++) C.bgcnt[b] = unit.BGCnt[b];
        C.shown = shown;
        C.nLayers = nLayers;
        C.windowLayers = windowLayers;
        C.layerMask = layerMask;
        // The palettes, the scroll, the blending and the sprites the frame was drawn with
        // (the game changes them for the next one before this runs; with the current ones the
        // sides of the view would be a frame ahead of the screen in a fade).
        auto renderer = static_cast<GPU2D::SoftRenderer*>(&nds.GPU.GetRenderer2D());
        C.world = world ? *world : WideImage();
        if (!keep)
        {
            C.nds = &nds;
            C.pixels = top.pixels;
            C.flags = top.flags;
            C.objPrio = renderer->ObjPrio[unitNum];
            C.lineScroll = renderer->LineScroll[unitNum];
            C.lineBlend = renderer->LineBlend[unitNum];
            C.pal = renderer->PalSnap[unitNum];
            C.oam = renderer->OAMSnap[unitNum];
            return true;
        }
        C.nds = nullptr;
        C.pixelsCopy.assign(top.pixels, top.pixels + 256 * 192);
        C.flagsCopy.assign(top.flags, top.flags + 256 * 192);
        C.objPrioCopy.assign(renderer->ObjPrio[unitNum], renderer->ObjPrio[unitNum] + 256 * 192);
        memcpy(C.lineScrollCopy, renderer->LineScroll[unitNum], sizeof(C.lineScrollCopy));
        memcpy(C.lineBlendCopy, renderer->LineBlend[unitNum], sizeof(C.lineBlendCopy));
        memcpy(C.palCopy, renderer->PalSnap[unitNum], sizeof(C.palCopy));
        memcpy(C.oamCopy, renderer->OAMSnap[unitNum], sizeof(C.oamCopy));
        C.pixels = C.pixelsCopy.data();
        C.flags = C.flagsCopy.data();
        C.objPrio = C.objPrioCopy.data();
        C.lineScroll = C.lineScrollCopy;
        C.lineBlend = C.lineBlendCopy;
        C.pal = C.palCopy;
        C.oam = C.oamCopy;
        if (C.world.pixels && C.world.w > 0 && C.world.h > 0)
        {
            C.worldCopy.assign(C.world.pixels, C.world.pixels + (size_t)C.world.w * C.world.h);
            C.world.pixels = C.worldCopy.data();
        }
        // VRAM: the 16 KB pages of the backgrounds' maps and tiles, and the sprites' VRAM.
        const u32 bgSize = unitNum == 0 ? 512 * 1024 : 128 * 1024, objSize = unitNum == 0 ? 256 * 1024 : 128 * 1024;
        C.bgVram.resize(bgSize);
        C.objVram.resize(objSize);
        bool page[32] = {};
        for (int i = 0; i < nLayers; i++)
        {
            const Layer& l = layers[i];
            auto mark = [&](u32 from, u32 len) {
                for (u32 a = from & ~0x3FFFu; a < from + len; a += 0x4000) page[(a & (bgSize - 1)) >> 14] = true;
            };
            mark(l.charBase, (l.c256 ? 64 : 32) * 1024);
            mark(l.mapBase, 0x800 * (l.mapH == 512 ? 4 : 2));
        }
        for (u32 p = 0; p < bgSize >> 14; p++)
            if (page[p]) CopyPage(&C.bgVram[p << 14], unitNum == 0 ? nds.GPU.VRAMPtr_ABG[p] : nds.GPU.VRAMPtr_BBG[p],
                [&](u32 a) { return unitNum == 0 ? nds.GPU.ReadVRAM_ABG<u32>(a) : nds.GPU.ReadVRAM_BBG<u32>(a); }, p << 14);
        const u32 objBase = unitNum == 0 ? 0x06400000 : 0x06600000;
        for (u32 p = 0; p < objSize >> 14; p++)
            CopyPage(&C.objVram[p << 14], unitNum == 0 ? nds.GPU.VRAMPtr_AOBJ[p] : nds.GPU.VRAMPtr_BOBJ[p],
                [&](u32 a) { return nds.GPU.ReadVRAM_OBJ<u32>(objBase + a); }, p << 14);
        return true;
    }

    // Draws the picture from the last Grab into `out` (valid until the next Render).
    void Render(WideImage& out)
    {
        using namespace melonDS;
        const Capture& C = Cap;
        const int unitNum = C.unitNum, extend = C.extend;
        const float viewWidth = C.viewWidth;
        const u32 dispcnt = C.dispcnt;
        const u8 shown = C.shown;
        const Layer* layers = C.layers;
        const int nLayers = C.nLayers;
        const uint8_t windowLayers = C.windowLayers, layerMask = C.layerMask;
        ScreenImage top {};
        top.pixels = C.pixels;
        top.flags = C.flags;
        const WideImage* world = C.world.pixels ? &C.world : nullptr;
        auto vram16 = [&](u32 a) -> u16 { return C.Bg16(a); };
        auto vram8 = [&](u32 a) -> u8 { return C.Bg8(a); };
        const u8* palSnap = C.pal;
        auto palette = [&](u32 off) -> u16 { u16 v; memcpy(&v, palSnap + (off & 0x3FE), 2); return v; };

        // A colour with the effects the game applies to the 2D layer it is on (target: BG0-3,
        // 4 the sprites, 5 the backdrop): the brightness effect if the layer is one of its
        // targets, then the master brightness, as the 2D engine does, converted to 8 bits per
        // channel as its output is. With the registers each line was drawn with.
        const u16 (*lineBlend)[3] = C.lineBlend;
        // The same colour on the same layer comes back pixel after pixel along a row (the sky,
        // the ground): the last one worked out for each layer is kept.
        struct ColourMemo { int y = -1; u16 c = 0; uint32_t out = 0; } colourMemo[6];
        auto colourOf = [&](u16 c, int target, int y) -> uint32_t {
            const u16 blend = lineBlend[y][0];
            const int effect = (blend & (1 << target)) && (shown & 0x20) ? (blend >> 6) & 3 : 0;
            const int evy = std::min<int>(16, lineBlend[y][1]);
            const int mbMode = (lineBlend[y][2] >> 14) & 3, mb = std::min(16, lineBlend[y][2] & 0x1F);
            int ch[3] = { (c & 0x1F) << 1, ((c >> 5) & 0x1F) << 1, ((c >> 10) & 0x1F) << 1 };
            for (int& v : ch)
            {
                if (effect == 2) v += ((63 - v) * evy + 8) >> 4;
                else if (effect == 3) v -= (v * evy + 7) >> 4;
                if (mbMode == 1) v += ((63 - v) * mb) >> 4;
                else if (mbMode == 2) v -= (v * mb + 15) >> 4;
            }
            auto to8 = [](int v) { return (uint32_t)((v << 2) | (v >> 4)); };
            return 0xFF000000 | (to8(ch[0]) << 16) | (to8(ch[1]) << 8) | to8(ch[2]);
        };
        auto colour = [&](u16 c, int target, int y) -> uint32_t {
            ColourMemo& m = colourMemo[target];
            if (m.y != y || m.c != c)
            {
                m.y = y;
                m.c = c;
                m.out = colourOf(c, target, y);
            }
            return m.out;
        };
        const u16 backdropColour = palette(0);

        // The sprites other than the HP boxes (which are the only ones at the front, priority
        // 0): the trainer, and the platforms the Pokémon stand on as they slide in at the start
        // of a battle. They are drawn here too: beyond the edges of the screen, where the 2D
        // engine does not draw them, and under an HP box moved away from the edge.
        const int outW = 256 + 2 * extend;
        Back.assign((size_t)outW * 192, 0);
        BackPrio.assign((size_t)outW * 192, 4);
        // Where the wild Pokémon's platform is while it is a sprite (as it slides in).
        int platLeft = 1 << 20, platRight = -(1 << 20);
        Boxes.clear();
        auto outIndex = [&](int x, int y) { return y * outW + x + extend; };
        {
            static const uint8_t sizes[4][4][2] = {
                { { 8, 8 }, { 16, 16 }, { 32, 32 }, { 64, 64 } },
                { { 16, 8 }, { 32, 8 }, { 32, 16 }, { 64, 32 } },
                { { 8, 16 }, { 8, 32 }, { 16, 32 }, { 32, 64 } },
                { { 8, 8 }, { 8, 8 }, { 8, 8 }, { 8, 8 } } };
            const u16* oam = (const u16*)C.oam;
            const u32 objBase = unitNum == 0 ? 0x06400000 : 0x06600000;
            const u32 objPal = 0x200;
            for (int o = 127; o >= 0; o--)                                  // the first sprite in front
            {
                const u16 a0 = oam[o * 4], a1 = oam[o * 4 + 1], a2 = oam[o * 4 + 2];
                if ((a0 & 0x300) == 0x200 || (a0 >> 14) == 3) continue;     // hidden
                int sw = sizes[a0 >> 14][a1 >> 14][0], sh = sizes[a0 >> 14][a1 >> 14][1];
                const bool affine = a0 & 0x100;
                if ((a0 & 0x300) == 0x300) { sw *= 2; sh *= 2; }            // double-size rotation
                int ox = a1 & 0x1FF, oy = a0 & 0xFF;
                if (ox >= 256) ox -= 512;
                if (oy + sh > 256) oy -= 256;
                const int objPrio = (a2 >> 10) & 3;
                const bool front = objPrio == 0;
                if (objPrio == 3 && oy + sh / 2 < 100 && !affine)
                {
                    platLeft = std::min(platLeft, ox);
                    platRight = std::max(platRight, ox + sw);
                }
                const int mode = (a0 >> 10) & 3;
                const bool c256 = a0 & 0x2000;
                if (mode == 2) continue;                                    // a window, not a picture
                if (front || !(shown & 0x10)) continue;                     // an HP box, or hidden outside the windows
                if (affine || mode == 3 || (c256 && (dispcnt & (1u << 31)))) continue; // not decoded here
                if (objPrio != 3) Boxes.push_back({ ox, oy, sw, sh });
                const u32 tile = a2 & 0x3FF;
                for (int y = std::max(0, oy); y < std::min(192, oy + sh); y++)
                    for (int x = std::max(ox, -extend); x < std::min(ox + sw, 256 + extend); x++)
                    {
                        int px = x - ox, py = y - oy;
                        if (a1 & 0x1000) px = sw - 1 - px;
                        if (a1 & 0x2000) py = sh - 1 - py;
                        // The tile and the pixel in it, in one- or two-dimensional mapping.
                        const int tx = px >> 3, ty = py >> 3, bpp = c256 ? 8 : 4;
                        u32 addr;
                        if (dispcnt & 0x10)
                            addr = tile * (32u << ((dispcnt >> 20) & 3)) + (ty * (sw >> 3) + tx) * 8 * bpp;
                        else
                            addr = (tile & (c256 ? 0x3FE : 0x3FF)) * 32 + (ty * 32 * 32 + tx * 8 * bpp);
                        addr += ((py & 7) * 8 + (px & 7)) * bpp / 8;
                        if (unitNum == 0) addr &= 0x3FFFF; else addr &= 0x1FFFF;
                        const u8 v = C.Obj8(objBase + addr);
                        const int idx = c256 ? v : ((px & 1) ? v >> 4 : v & 0xF);
                        if (!idx) continue;
                        if (objPrio <= BackPrio[outIndex(x, y)])
                        {
                            BackPrio[outIndex(x, y)] = objPrio;
                            Back[outIndex(x, y)] = colour(palette(objPal + (c256 ? idx : idx + (a2 >> 12) * 16) * 2), 4, y);
                        }
                    }
            }
        }

        // A trainer leaving the screen (the player, after throwing the Poké Ball) is hidden by
        // the game once it is past the edge of the screen, before it is past the edge of the
        // view: it is kept going, at the speed it had, until it is out of the view.
        for (int side = 0; side < 2; side++)
        {
            Ghost& g = Ghosts[side];
            // The sprites across this edge, with those touching them.
            int x0 = 1 << 20, x1 = -(1 << 20), y0 = 1 << 20, y1 = -(1 << 20);
            for (const Box& b : Boxes)
                if (side == 0 ? b.x < 0 : b.x + b.w > 256)
                {
                    x0 = std::min(x0, b.x); x1 = std::max(x1, b.x + b.w);
                    y0 = std::min(y0, b.y); y1 = std::max(y1, b.y + b.h);
                }
            if (x1 > x0)
                for (bool grew = true; grew;)
                {
                    grew = false;
                    for (const Box& b : Boxes)
                        if (b.x <= x1 && b.x + b.w >= x0 && b.y <= y1 && b.y + b.h >= y0 &&
                            (b.x < x0 || b.x + b.w > x1 || b.y < y0 || b.y + b.h > y1))
                        {
                            x0 = std::min(x0, b.x); x1 = std::max(x1, b.x + b.w);
                            y0 = std::min(y0, b.y); y1 = std::max(y1, b.y + b.h);
                            grew = true;
                        }
                }
            if (x1 > x0)
            {
                // Seen: its picture, and how fast it is moving.
                const float edgeX = side == 0 ? x0 : x1;
                g.vel = g.seen ? 0.5f * g.vel + 0.5f * (edgeX - g.edge) : 0;
                g.edge = edgeX;
                g.seen = true;
                g.moving = false;
                g.x = x0;
                g.y = std::max(0, y0);
                g.w = x1 - x0;
                g.h = std::min(192, y1) - g.y;
                g.px.assign((size_t)g.w * g.h, 0);
                g.prio.assign((size_t)g.w * g.h, 4);
                for (int y = 0; y < g.h; y++)
                    for (int x = std::max(x0, -extend); x < std::min(x1, 256 + extend); x++)
                    {
                        const int i = outIndex(x, g.y + y);
                        if (BackPrio[i] == 1 || BackPrio[i] == 2)
                        {
                            g.px[(size_t)y * g.w + x - x0] = Back[i];
                            g.prio[(size_t)y * g.w + x - x0] = BackPrio[i];
                        }
                    }
            }
            else if (g.seen && (side == 0 ? g.vel < -0.5f : g.vel > 0.5f))
            {
                g.moving = true;                                        // gone: carried on
                g.seen = false;
            }
            else if (!g.moving)
                g.seen = false;
            if (g.moving)
            {
                g.x += g.vel;
                if (side == 0 ? g.x + g.w <= -extend : g.x >= 256 + extend) g.moving = false;
            }
        }

        // The 2D layers at a point: the colour and the priority of the frontmost (4 for the
        // backdrop), and whether it is a sprite (in front of a background of the same priority).
        // The 8 pixels of the tile row last read on each background, as palette indices (0:
        // clear): the pixels beside the screen are asked for one after the other along a row,
        // so a tile's map entry and its row of pixels are read once instead of for each pixel.
        struct TileMemo { u32 mapAddr = ~0u; int sy = -1; u8 idx[8]; } tileMemo[4];
        auto layerPixel = [&](int x, int y, int& prio, bool& obj) -> uint32_t {
            uint32_t sprite = Back[outIndex(x, y)];
            const int spritePrio = BackPrio[outIndex(x, y)];
            if ((x < 0 || x >= 256) && spritePrio > EdgePrio[y * 2 + (x >= 256)]) sprite = 0;
            obj = false;
            if ((x < 0 || x >= 256) && RowFlat[y])
            {
                // A row the screen shows in one colour (see RowFlat) is that colour beside it,
                // without the sprites the game keeps off the screen there.
                prio = RowPrio[y];
                return RowColour[y];
            }
            for (int i = 0; i <= nLayers; i++)
            {
                if (sprite && (i == nLayers || layers[i].prio >= spritePrio))
                {
                    prio = spritePrio;
                    obj = true;
                    return sprite;
                }
                if (i == nLayers) break;
                const Layer& l = layers[i];
                const int sx = (C.lineScroll[y][l.num][0] + x) & 511;
                const int sy = (C.lineScroll[y][l.num][1] + y) & (l.mapH - 1);
                const int tx = sx >> 3, ty = sy >> 3;
                const u32 block = (tx >> 5) + ((ty >> 5) << 1);
                const u32 mapAddr = l.mapBase + block * 0x800 + (((ty & 31) << 5) + (tx & 31)) * 2;
                TileMemo& tm = tileMemo[i];
                if (tm.mapAddr != mapAddr || tm.sy != sy)
                {
                    tm.mapAddr = mapAddr;
                    tm.sy = sy;
                    const u16 e = vram16(mapAddr);
                    const int py = (e & 0x800) ? 7 - (sy & 7) : sy & 7;
                    for (int p = 0; p < 8; p++)
                    {
                        const int px = (e & 0x400) ? 7 - p : p;
                        int v;
                        if (l.c256)
                            v = vram8(l.charBase + (e & 0x3FF) * 64 + py * 8 + px);
                        else
                        {
                            const u8 two = vram8(l.charBase + (e & 0x3FF) * 32 + py * 4 + (px >> 1));
                            v = (px & 1) ? two >> 4 : two & 0xF;
                            if (v) v += (e >> 12) * 16;
                        }
                        tm.idx[p] = (u8)v;
                    }
                }
                const int idx = tm.idx[sx & 7];
                if (idx)
                {
                    prio = l.prio;
                    return colour(palette(idx * 2), l.num, y);
                }
            }
            prio = 4;
            return colour(backdropColour, 5, y);
        };

        // A row of the screen in one colour from edge to edge (the plain screen a battle
        // opens with) is continued in that colour, not with the rest of the map: the half of
        // the map the game does not show is not always more of the same (in the opening, it
        // and the wide 3D picture have things there that would flash beside the screen).
        RowFlat.assign(192, 0);
        RowColour.assign(192, 0);
        RowPrio.assign(192, 4);
        // Beside the screen a sprite is only drawn where it would be in front of the
        // background at that edge of the screen in its row: the message box hides the
        // trainer on the screen, and does beside it too (where its map is empty).
        EdgePrio.assign(192 * 2, 4);
        for (int y = 0; y < 192; y++)
            for (int side = 0; side < 2; side++)
            {
                const uint8_t c = LayerClass(top.flags[y * 256 + (side ? 255 : 0)]);
                if (c & 0x0F) EdgePrio[y * 2 + side] = C.bgcnt[__builtin_ctz(c)] & 3;
            }
        for (int y = 0; y < 192; y++)
        {
            // In one colour, whatever draws it (in the opening a sprite in the same blue
            // stands at the edge); the priority is that of the background showing.
            const uint32_t* px = &top.pixels[y * 256];
            bool same = true;
            uint8_t bg = 0;
            for (int x = 0; x < 256 && same; x++)
            {
                same = (px[x] & 0xFFFFFF) == (px[0] & 0xFFFFFF);
                const uint8_t c = LayerClass(top.flags[y * 256 + x]);
                if (!bg && (c & 0x0F)) bg = c;
            }
            if (!same) continue;
            RowFlat[y] = 1;
            RowColour[y] = 0xFF000000 | px[0];
            if (bg) RowPrio[y] = C.bgcnt[__builtin_ctz(bg)] & 3;
        }

        // With the 3D layer (BG0) over them where it is in front.
        const bool use3D = world && world->pixels && !world->placed && world->w > 0 && (dispcnt & 0x100) && (dispcnt & 8) && (shown & 1);
        const int prio3D = C.bgcnt[0] & 3;
        const float viewLeft = 128 - viewWidth / 2;
        auto bgPixel = [&](int x, int y) -> uint32_t {
            int prio;
            bool obj;
            const uint32_t c = layerPixel(x, y, prio, obj);
            if (!use3D || prio < prio3D || (obj && prio == prio3D)) return c;
            // Nor what the 3D picture has there (in the opening, white pieces the game keeps
            // just off the screen).
            if ((x < 0 || x >= 256) && RowFlat[y]) return c;
            int wx = (int)((x + 0.5f - viewLeft) / viewWidth * world->w), wy = (int)((y + 0.5f) / 192 * world->h);
            if (wx < 0 || wx >= world->w || wy < 0 || wy >= world->h) return c;
            const uint32_t w3 = world->pixels[(size_t)wy * world->w + wx];
            const uint32_t a = w3 >> 24;
            if (!a) return c;
            const uint32_t t = colour((u16)((((w3 >> 19) & 0x1F)) | (((w3 >> 11) & 0x1F) << 5) | (((w3 >> 3) & 0x1F) << 10)), 0, y);
            if (a == 255) return t;
            auto mix = [&](int sh) { return ((((t >> sh) & 0xFF) * a + ((c >> sh) & 0xFF) * (255 - a)) / 255) << sh; };
            return 0xFF000000 | mix(16) | mix(8) | mix(0);
        };

        // The HP boxes: groups of their sprites' pixels that touch the left or the right edge
        // of the screen (but not both, and not most of the screen, which would be an effect).
        const u8* objPrioAt = C.objPrio;
        auto hpPixel = [&](int i) { return objPrioAt[i] == 0 && LayerClass(top.flags[i]) == LAYER_OBJ; };
        Label.assign(256 * 192, 0);
        int moved[3] = {};
        for (int side = 1; side <= 2; side++)
        {
            Stack.clear();
            const int edgeX = side == 1 ? 0 : 255;
            for (int y = 0; y < 192; y++)
                if (hpPixel(y * 256 + edgeX) && !Label[y * 256 + edgeX])
                {
                    Label[y * 256 + edgeX] = side;
                    Stack.push_back(y * 256 + edgeX);
                }
            while (!Stack.empty())
            {
                const int p = Stack.back();
                Stack.pop_back();
                moved[side]++;
                const int x = p & 255, y = p >> 8;
                // Neighbours across corners too: the slanted edges of the boxes are lines of
                // single pixels.
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                    {
                        if (x + dx < 0 || x + dx > 255 || y + dy < 0 || y + dy > 191) continue;
                        const int q = p + dy * 256 + dx;
                        if (!Label[q] && hpPixel(q))
                        {
                            Label[q] = side;
                            Stack.push_back(q);
                        }
                    }
            }
        }
        bool shift[3] = { false, true, true };
        for (int side = 1; side <= 2; side++)
            if (moved[side] > 256 * 192 / 3) shift[side] = false;
        for (int y = 0; y < 192; y++)
            if (Label[y * 256] && Label[y * 256] == Label[y * 256 + 255]) shift[Label[y * 256]] = false;

        // The wild Pokémon stands at the right of the screen, and the player's at the left;
        // on a wide view the player's is at the left of the view, by its HP box, so the wild
        // one is moved out to the right as far, with the top of the picture it is in. Between
        // the two Pokémon the background (bands of sky and ground) is stretched to fill the
        // gap. As a battle opens it glides there once its platform has slid into place (not
        // before: the grass sweeping past would be torn).
        Calls++;
        // Not while something large in 3D is in front across its rows (the grass of the
        // opening, fading out): the Pokémon is part of it, and could not move with the
        // background.
        bool busy = false;
        if (Glide < 1)
        {
            Comp.assign(256 * 192, 0);
            auto isFg = [&](int i) { return LayerClass(top.flags[i]) == LAYER_3D; }; // the grass is 3D, as the Pokémon
            for (int start = 0; start < 256 * EnemyBottom && !busy; start++)
            {
                if (Comp[start] || !isFg(start)) continue;
                Stack.clear();
                Comp[start] = 1;
                Stack.push_back(start);
                int x0 = 256, x1 = -1;
                while (!Stack.empty())
                {
                    const int p = Stack.back();
                    Stack.pop_back();
                    const int x = p & 255, y = p >> 8;
                    x0 = std::min(x0, x); x1 = std::max(x1, x);
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++)
                        {
                            if (x + dx < 0 || x + dx > 255 || y + dy < 0 || y + dy > 191) continue;
                            const int q = p + dy * 256 + dx;
                            if (!Comp[q] && isFg(q))
                            {
                                Comp[q] = 1;
                                Stack.push_back(q);
                            }
                        }
                }
                busy = x1 - x0 >= 160;
            }
        }
        if (platRight > platLeft)
        {
            if ((platLeft + platRight) / 2 < 192) Glide = 0;
            else if (!busy) Glide = std::min(1.0f, Glide + 1 / 20.0f);
        }
        else if ((Glide > 0 || Calls > 300) && !busy)                   // after the opening, or joined late
            Glide = std::min(1.0f, Glide + 1 / 20.0f);
        const int enemyShift = (int)std::lround(extend * Glide);

        const int w = 256 + 2 * extend;
        // The picture as the screen shows it, continued beyond its edges; and where something
        // stands in front of the backgrounds (a Pokémon, a trainer, a move's effect), with the
        // backgrounds alone there.
        Picture.resize((size_t)w * 192);
        Fg.assign((size_t)w * 192, 0);
        Plain.resize((size_t)w * 192);
        auto plainPixel = [&](int x, int y) {
            // The background layers without the sprites.
            const size_t i = outIndex(x, y);
            const uint32_t keep = Back[i];
            Back[i] = 0;
            int prio;
            bool obj;
            const uint32_t c = layerPixel(x, y, prio, obj);
            Back[i] = keep;
            return c;
        };
        // The backgrounds reaching the screen's sides that are not continued beyond them (the
        // message box): never something in front, which a move's effect touching them would
        // otherwise carry along.
        uint8_t boxLayers = 0;
        for (int y = 0; y < 192; y++)
            for (int side = 0; side < 2; side++)
            {
                const int i = y * 256 + (side ? 255 : 0);
                const uint8_t c = LayerClass(top.flags[i]);
                if (!(Label[i] && shift[Label[i]]) && c < LAYER_OBJ && !(c & layerMask)) boxLayers |= c;
            }
        Ui.assign((size_t)w * 192, 0);
        for (int y = 0; y < 192; y++)
        {
            uint32_t* row = &Picture[(size_t)y * w];
            uint8_t* fg = &Fg[(size_t)y * w];
            uint32_t* plain = &Plain[(size_t)y * w];
            // Where another background reaches the edge of the screen in this row (the
            // message box along the bottom), it is continued instead, in its edge colour:
            // the map has nothing to show under it.
            uint32_t edge[2];
            bool flat[2];
            for (int side = 0; side < 2; side++)
            {
                const int i = y * 256 + (side ? 255 : 0);
                const uint8_t c = LayerClass(top.flags[i]);
                flat[side] = !(Label[i] && shift[Label[i]]) && !(c & layerMask) && c != LAYER_3D && c != LAYER_OBJ;
                edge[side] = 0xFF000000 | top.pixels[i];
            }
            for (int x = 0; x < w; x++)
            {
                const int sx = x - extend;
                bool computed = false;
                if (sx >= 0 && sx < 256)
                {
                    const int i = y * 256 + sx;
                    if (Label[i] && shift[Label[i]])
                    {
                        row[x] = bgPixel(sx, y);
                        computed = true;
                    }
                    else
                    {
                        row[x] = 0xFF000000 | top.pixels[i];
                        const uint8_t c = LayerClass(top.flags[i]);
                        Ui[(size_t)y * w + x] = (c & windowLayers) != 0;
                        fg[x] = c == LAYER_3D || c == LAYER_OBJ || (c != LAYER_BACKDROP && !(c & layerMask) && !(c & boxLayers));
                    }
                }
                else if (flat[sx >= 256])
                    row[x] = edge[sx >= 256];
                else
                {
                    row[x] = bgPixel(sx, y);
                    computed = true;
                }
                if (computed && enemyShift > 0)
                {
                    plain[x] = plainPixel(sx, y);
                    fg[x] = plain[x] != row[x];
                }
                else if (fg[x] && enemyShift > 0)
                    plain[x] = plainPixel(sx, y);
            }
        }

        // The wild Pokémon's side, moved out to the right (see above): the backgrounds of the
        // rows it is in, and each thing in front of them that is on its side, whole (a move's
        // effect reaching past those rows is not cut in two).
        Move.assign((size_t)w * 192, 0);
        Bridge.assign((size_t)w * 192, 0);
        int streamL = w, streamR = -1;
        if (enemyShift > 0)
        {
            Comp.assign((size_t)w * 192, 0);
            for (size_t start = 0; start < Fg.size(); start++)
            {
                if (!Fg[start] || Comp[start] || Ui[start]) continue;
                Stack.clear();
                Members.clear();
                Comp[start] = 1;
                Stack.push_back((int)start);
                int x0 = w, x1 = -1, y0 = 192, y1 = -1;
                bool atEnemy = false, atPlayer = false;
                while (!Stack.empty())
                {
                    const int p = Stack.back();
                    Stack.pop_back();
                    Members.push_back(p);
                    const int x = p % w, y = p / w;
                    atEnemy |= std::abs(x - extend - EnemyX) <= 10 && std::abs(y - EnemyY) <= 10;
                    atPlayer |= std::abs(x - extend - PlayerX) <= 10 && std::abs(y - PlayerY) <= 10;
                    x0 = std::min(x0, x); x1 = std::max(x1, x);
                    y0 = std::min(y0, y); y1 = std::max(y1, y);
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++)
                        {
                            if (x + dx < 0 || x + dx >= w || y + dy < 0 || y + dy > 191) continue;
                            const int q = p + dy * w + dx;
                            if (Fg[q] && !Comp[q] && !Ui[q])
                            {
                                Comp[q] = 1;
                                Stack.push_back(q);
                            }
                        }
                }
                // The wild Pokémon moves out as one piece, and so does a small effect on it.
                // A move running from one Pokémon to the other is kept and drawn below, long
                // enough to still reach: shifting only its far half left a hole, and filling
                // that hole smeared two colours across the stream.
                if (x0 == 0 && x1 == w - 1) continue;
                const int x0s = x0 - extend, x1s = x1 - extend;
                const int cx = (x0 + x1) / 2 - extend, cy = (y0 + y1) / 2;
                const int bw = x1 - x0, bh = y1 - y0;
                auto nearEnemy = [&](int x, int y, int r) {
                    const int dx = x - EnemyX, dy = y - EnemyY;
                    return dx * dx + dy * dy <= r * r;
                };
                const bool spans = x0s < EnemyGapLeft && x1s > EnemyGapRight;
                // The whole stream, including where it covers the wild Pokémon. Moving that
                // end across on its own cut Flamethrower in a straight line through the middle.
                if (atEnemy && spans)
                    for (int p : Members) Bridge[p] = 1;
                else if (atEnemy || (!atPlayer && bw <= 72 && bh <= 72 && nearEnemy(cx, cy, 36)))
                    for (int p : Members) Move[p] = 1;
                else if (!atPlayer)
                    for (int p : Members) Bridge[p] = 1;
            }
            // One span for the whole stream. Lengthening each row by its own width put a
            // horizontal cut through Flamethrower: the rows above the middle of the screen
            // and the rows below it no longer lined up.
            for (int y = 0; y < 192; y++)
            {
                const uint8_t* bridge = &Bridge[(size_t)y * w];
                for (int x = 0; x < w; x++)
                    if (bridge[x]) { streamL = std::min(streamL, x); streamR = std::max(streamR, x); }
            }
            if (streamR < 0 || streamR - streamL < 64 || streamR - extend < EnemyGapRight)
            {
                // Not a stream between the two Pokémon. A short effect on the wild Pokémon's
                // side (its shadow, a hit spark) moves with it.
                streamR = -1;
                for (int y = 0; y < 192; y++)
                {
                    uint8_t* bridge = &Bridge[(size_t)y * w];
                    uint8_t* move = &Move[(size_t)y * w];
                    for (int x = 0; x < w; x++)
                        if (bridge[x])
                        {
                            bridge[x] = 0;
                            if (x - extend >= EnemyGapRight) move[x] = 1;
                        }
                }
            }
        }

        Pixels.resize((size_t)w * 192);
        for (int y = 0; y < 192; y++)
        {
            uint32_t* row = &Pixels[(size_t)y * w];
            const uint32_t* pic = &Picture[(size_t)y * w];
            const uint8_t* fg = &Fg[(size_t)y * w];
            const uint8_t* move = &Move[(size_t)y * w];
            const uint8_t* bridge = &Bridge[(size_t)y * w];
            const uint8_t* ui = &Ui[(size_t)y * w];
            const uint32_t* plain = &Plain[(size_t)y * w];
            if (enemyShift > 0)
            {
                // The backgrounds, those of the wild Pokémon's rows moved, with the gap between
                // the two Pokémon stretched to fill.
                for (int x = 0; x < w; x++)
                    row[x] = (y < EnemyBottom ? fg[x] : move[x]) ? plain[x] : pic[x];
                if (y < EnemyBottom)
                {
                    Row.assign(row, row + w);
                    for (int sx = EnemyGapLeft; sx < 256 + extend; sx++)
                    {
                        int from;
                        if (sx >= EnemyGapRight + enemyShift) from = sx - enemyShift;
                        else from = EnemyGapLeft + (sx - EnemyGapLeft) * (EnemyGapRight - EnemyGapLeft) / (EnemyGapRight - EnemyGapLeft + enemyShift);
                        row[sx + extend] = Row[from + extend];
                    }
                    for (int x = 0; x < w; x++)
                        if (fg[x] && !move[x] && !bridge[x]) row[x] = pic[x];
                }
                for (int x = 0; x + enemyShift < w; x++)
                    if (move[x]) row[x + enemyShift] = pic[x];
                // The same mapping on every row, so the stream stays the shape the game drew
                // and still reaches the Pokémon that moved out.
                if (streamR >= streamL)
                {
                    bool rowHas = false;
                    for (int x = 0; x < w && !rowHas; x++) rowHas = bridge[x] != 0;
                    if (rowHas)
                    {
                        if (y >= EnemyBottom)
                            for (int x = 0; x < w; x++)
                                if (bridge[x]) row[x] = plain[x];
                        const int oldW = streamR - streamL;
                        const int newW = oldW + enemyShift;
                        if ((int)ScalePx.size() < newW + 1) { ScalePx.resize(newW + 1); ScaleOn.resize(newW + 1); }
                        for (int d = 0; d <= newW; d++) ScaleOn[d] = 0;
                        for (int x = streamL; x <= streamR; x++)
                        {
                            if (!bridge[x]) continue;
                            int d0 = (x - streamL) * newW / oldW;
                            int d1 = (x + 1 - streamL) * newW / oldW;
                            if (d1 <= d0) d1 = d0 + 1;
                            for (int d = d0; d < d1 && d <= newW; d++)
                            {
                                ScalePx[d] = pic[x];
                                ScaleOn[d] = 1;
                            }
                        }
                        for (int d = 0; d <= newW && streamL + d < w; d++)
                        {
                            if (!ScaleOn[d]) continue;
                            const int dst = streamL + d;
                            if (ui[dst]) continue;
                            if (fg[dst] && !move[dst] && !bridge[dst]) continue;
                            row[dst] = ScalePx[d];
                        }
                    }
                }
                for (int x = 0; x < w; x++)
                    if (ui[x]) row[x] = pic[x];
            }
            else
                std::copy(pic, pic + w, row);
            for (const Ghost& g : Ghosts)
                if (g.moving && y >= g.y && y < g.y + g.h)
                {
                    const int gx = (int)std::lround(g.x);
                    for (int i = 0; i < g.w; i++)
                    {
                        const int sx = gx + i;
                        const size_t gi = (size_t)(y - g.y) * g.w + i;
                        const uint32_t c = g.px[gi];
                        if (!c || sx < -extend || sx >= 256 + extend) continue;
                        // Behind the message box, as the game draws the trainer: on the
                        // screen, and beside it in the box's rows (see EdgePrio).
                        if (sx < 0 || sx >= 256 ? g.prio[gi] > EdgePrio[y * 2 + (sx >= 256)]
                                                : (LayerClass(top.flags[y * 256 + sx]) & (boxLayers | windowLayers)) != 0) continue;
                        row[sx + extend] = c;
                    }
                }
            for (int sx = 0; sx < 256; sx++)
            {
                const int l = Label[y * 256 + sx];
                if (l && shift[l]) row[l == 1 ? sx : sx + 2 * extend] = 0xFF000000 | top.pixels[y * 256 + sx];
            }
        }
        out = WideImage();
        out.pixels = Pixels.data();
        out.w = w;
        out.h = 192;
        out.placed = true;
        out.baseX = -extend;
    }

    // Between battles: the next one opens with the wild Pokémon in its place on the screen.
    void Reset()
    {
        Glide = 0;
        Calls = 0;
        Ghosts[0] = Ghosts[1] = Ghost();
    }

private:
    // Copies one 16 KB page of VRAM: straight from its bank when only one is mapped there,
    // else through the console's own reads (which combine the banks mapped together).
    template<typename F>
    static void CopyPage(uint8_t* dst, const uint8_t* direct, F read32, melonDS::u32 base)
    {
        if (direct)
        {
            memcpy(dst, direct, 0x4000);
            return;
        }
        for (melonDS::u32 a = 0; a < 0x4000; a += 4)
        {
            const melonDS::u32 v = read32(base + a);
            memcpy(dst + a, &v, 4);
        }
    }

    Capture Cap;
    // The rows of the wild Pokémon's side, and the stretch of plain background between the
    // two Pokémon, in screen pixels.
    static constexpr int EnemyBottom = 104, EnemyGapLeft = 100, EnemyGapRight = 126;
    // Where the two Pokémon stand on the screen.
    static constexpr int EnemyX = 192, EnemyY = 56, PlayerX = 60, PlayerY = 105;
    std::vector<uint32_t> Pixels, Picture, Plain;
    std::vector<uint8_t> Fg, Move, Bridge, Comp, Ui;
    std::vector<uint32_t> ScalePx;
    std::vector<uint8_t> ScaleOn;
    std::vector<int> Members;
    std::vector<uint8_t> Label;
    std::vector<uint32_t> Back, Row;
    std::vector<uint8_t> BackPrio;
    std::vector<uint8_t> RowFlat, RowPrio;
    std::vector<uint32_t> RowColour;
    std::vector<uint8_t> EdgePrio;   // per row, left and right: priority of the background at the screen's edge
    float Glide = 0;
    struct Box { int x, y, w, h; };
    std::vector<Box> Boxes;
    // A trainer's sprites last seen across the left or the right edge of the screen.
    struct Ghost
    {
        bool seen = false, moving = false;
        float x = 0, vel = 0, edge = 0;
        int y = 0, w = 0, h = 0;
        std::vector<uint32_t> px;
        std::vector<uint8_t> prio;
    };
    Ghost Ghosts[2];
    int Calls = 0;
    std::vector<int> Stack;
};
