// Draws a merged-view scene with the SDL renderer: every piece is uploaded at DS
// resolution and the graphics card does the scaling, so the cost no longer depends on
// the size of the window. MergedCompose in merged.h draws the same picture on the CPU
// and is what the headless tests use; the two have to be kept in step.
#pragma once

#include <chrono>
#include <vector>
#include <SDL.h>
#include "merged.h"

class GpuCompositor
{
public:
    explicit GpuCompositor(SDL_Renderer* renderer) : R(renderer)
    {
        // Filtered pieces with holes in them are drawn with premultiplied alpha, so that
        // the colour of a pixel that is left out does not bleed into its neighbours.
        Premultiplied = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    }

    void Draw(const Scene& scene, const ScreenImage screens[2], int outW, int outH, bool smooth, const WideImage* wide)
    {
        uint32_t bg = 0;
        if (scene.bgScreen >= 0) bg = screens[scene.bgScreen].pixels[scene.bgY * 256 + scene.bgX] & 0xFFFFFF;
        const bool useWide = scene.wide && !scene.narrow && wide && wide->pixels && wide->w > 0 && wide->h > 0;
        const MergedView v = MergedMakeView(outW, outH);

        SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(R, useWide ? 0 : (bg >> 16) & 0xFF, useWide ? 0 : (bg >> 8) & 0xFF, useWide ? 0 : bg & 0xFF, 255);
        SDL_RenderClear(R);

        if (useWide) DrawWide(scene, screens, outW, outH, v, smooth, *wide);
        else if (scene.edgeFill && !scene.items.empty() && scene.items[0].screen >= 0) DrawEdgeFill(screens[scene.items[0].screen], outW, v);
        else if ((scene.softBars || (scene.wide && scene.narrow)) && !scene.items.empty() && scene.items[0].screen >= 0 && v.originX > 0)
            DrawNarrowBars(screens[scene.items[0].screen], outW, outH, v, scene.wide && scene.narrow);

        int slot = 0;
        bool first = true;
        for (const Element& item : scene.items)
        {
            Element el = item;
            const bool isFirst = first;
            first = false;
            if (useWide && isFirst && wide->placed) continue;
            if (useWide && isFirst) el.drop |= LAYER_3D;
            // (Over the world, its backdrop where the world has something is left out: see
            // MergedWideCovers.)
            const bool overWorld = useWide && isFirst && !wide->placed;
            if (el.wideOnly && !useWide) continue;
            if (!useWide) el.holeCount = 0;
            const float s = v.unit * el.scale;
            SDL_Rect dst;
            dst.x = (int)(v.originX + el.dx * v.unit);
            dst.y = (int)(v.originY + el.dy * v.unit);
            dst.w = (int)(el.sw * s * el.stretchX + 0.5f);
            dst.h = (int)(el.sh * s + 0.5f);
            if (dst.w <= 0 || dst.h <= 0 || el.sw <= 0 || el.sh <= 0) continue;
            // The outline first, for a plain rectangle as for a piece (the chosen battle button's
            // outline is drawn around its backing rectangle: here it was left out, so the Switch
            // showed no outline on the chosen button).
            if (el.border)
            {
                int t = (int)(v.unit * (el.borderWidth ? el.borderWidth : 6) / 8.0f + 0.5f);
                if (t < 1) t = 1;
                SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(R, (el.borderColor >> 16) & 0xFF, (el.borderColor >> 8) & 0xFF, el.borderColor & 0xFF, 255);
                if (el.borderRadius <= 0)
                {
                    const SDL_Rect frame[4] = {
                        { dst.x - t, dst.y - t, dst.w + 2 * t, t }, { dst.x - t, dst.y + dst.h, dst.w + 2 * t, t },
                        { dst.x - t, dst.y, t, dst.h }, { dst.x + dst.w, dst.y, t, dst.h },
                    };
                    SDL_RenderFillRects(R, frame, 4);
                }
                else
                {
                    // A rounded outline, one row of spans at a time.
                    Spans.clear();
                    const float r = el.borderRadius * v.unit;
                    for (int y = dst.y - t; y < dst.y + dst.h + t; y++)
                        OutlineRow(dst.x, dst.y, dst.w, dst.h, t, r, y, [&](int xa, int xb) { Spans.push_back({ xa, y, xb - xa, 1 }); });
                    SDL_RenderFillRects(R, Spans.data(), (int)Spans.size());
                }
            }

            if (el.screen < 0)
            {
                SDL_SetRenderDrawBlendMode(R, el.alpha == 255 ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(R, (el.keys[0] >> 16) & 0xFF, (el.keys[0] >> 8) & 0xFF, el.keys[0] & 0xFF, el.alpha);
                SDL_RenderFillRect(R, &dst);
                continue;
            }
            const ScreenImage& src = screens[el.screen];

            const bool nearest = el.crisp || !smooth;
            const bool keyed = el.keys[0] != 0xFFFFFFFF;
            const bool spriteKey = el.dropSpriteCorner > 0 && src.flags;
            const bool plain = !(el.drop && src.flags) && !keyed && !spriteKey && el.alpha == 255 && el.cutX >= 256 && !el.holeCount;
            const int slotIndex = slot++;
            SDL_Texture* tex = Slot(slotIndex, el.sw, el.sh);
            if (!tex) continue;
            SDL_SetTextureScaleMode(tex, nearest ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
            // Uploading every piece every frame cost the Switch 13 ms a frame in a Gen 4
            // battle waiting for a command (the strip's thirty-odd pieces, none of them
            // changing): a piece the same as last frame keeps its texture. (Not the piece under
            // the widescreen world, whose holes follow the world's picture.)
            Tex& cached = Slots[slotIndex];
            const auto tp = std::chrono::steady_clock::now();
            const uint64_t sig = overWorld ? 0 : Signature(el, src, plain, nearest);
            SignatureMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tp).count();
            if (!overWorld && cached.hasSig && cached.sig == sig)
            {
                SDL_SetTextureBlendMode(tex, plain ? SDL_BLENDMODE_NONE : (!nearest && slotIndex < (int)CachedPremul.size() && CachedPremul[slotIndex]) ? Premultiplied : SDL_BLENDMODE_BLEND);
                DrawPiece(tex, dst, el);
                continue;
            }
            cached.hasSig = !overWorld;
            cached.sig = sig;
            const auto tu = std::chrono::steady_clock::now();
            if (plain)
            {
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
                SDL_UpdateTexture(tex, nullptr, src.pixels + el.sy * 256 + el.sx, 256 * 4);
            }
            else
            {
                // Pixels that are left out get alpha 0.
                bool premul = !nearest && SDL_SetTextureBlendMode(tex, Premultiplied) == 0;
                if (!premul) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                if ((int)CachedPremul.size() <= slotIndex) CachedPremul.resize(slotIndex + 1);
                CachedPremul[slotIndex] = premul;
                Buf.resize((size_t)el.sw * el.sh);
                const uint32_t al = el.alpha;
                for (int y = 0; y < el.sh; y++)
                {
                    const int sy = el.sy + y;
                    const uint32_t* row = src.pixels + sy * 256 + el.sx;
                    const uint8_t* frow = src.flags ? src.flags + sy * 256 + el.sx : nullptr;
                    uint32_t* out = &Buf[(size_t)y * el.sw];
                    for (int x = 0; x < el.sw; x++)
                    {
                        uint32_t c = row[x] & 0xFFFFFF;
                        bool skip = el.Skips(el.sx + x, sy) ||
                            (el.drop && frow && (LayerClass(frow[x]) & el.drop)) ||
                            (overWorld && frow && LayerClass(frow[x]) == LAYER_BACKDROP &&
                                MergedWideCovers(*wide, v, outW, outH, el.sx + x, sy)) ||
                            (spriteKey && LayerClass(frow[x]) == LAYER_OBJ && std::min(x, el.sw - 1 - x) < el.dropSpriteCorner &&
                                std::min(y, el.sh - 1 - y) < el.dropSpriteCorner) ||
                            (keyed && MergedKeyed(el, c) &&
                                (!el.keyRing || std::min({ x, el.sw - 1 - x, y, el.sh - 1 - y }) < el.keyRing));
                        if (skip)
                            out[x] = premul ? 0 : c;
                        else if (premul && al != 255)
                            out[x] = (al << 24) | ((((c >> 16) & 0xFF) * al / 255) << 16) |
                                ((((c >> 8) & 0xFF) * al / 255) << 8) | ((c & 0xFF) * al / 255);
                        else
                            out[x] = (al << 24) | c;
                    }
                }
                SDL_UpdateTexture(tex, nullptr, Buf.data(), el.sw * 4);
            }
            (overWorld ? UnderWorldMs : UploadMs) += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tu).count();
            Uploads++;
            DrawPiece(tex, dst, el);
        }
    }

private:
    // A texture, and for the pieces' slots what was last uploaded to it (Signature): a piece
    // whose pixels and settings are as last frame's is not uploaded again.
    struct Tex { SDL_Texture* t = nullptr; int w = 0, h = 0; uint64_t sig = 0; bool hasSig = false; };

    SDL_Texture* Make(Tex& x, int w, int h)
    {
        if (x.t && x.w == w && x.h == h) return x.t;
        if (x.t) SDL_DestroyTexture(x.t);
        x.t = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        x.hasSig = false;
        x.w = w;
        x.h = h;
        return x.t;
    }

    void DrawPiece(SDL_Texture* tex, const SDL_Rect& dst, const Element& el)
    {
        // (A mirrored piece, as MergedCompose draws it: the message box's closing edge in
        // the battle strip was drawn the wrong way round here, a dark line beside the text.)
        if (el.flipX || el.flipY)
            SDL_RenderCopyEx(R, tex, nullptr, &dst, 0.0, nullptr,
                (SDL_RendererFlip)((el.flipX ? SDL_FLIP_HORIZONTAL : 0) | (el.flipY ? SDL_FLIP_VERTICAL : 0)));
        else
            SDL_RenderCopy(R, tex, nullptr, &dst);
    }

    // What a piece's texture is made from: its source pixels (and their layers, when they
    // decide what is left out) and everything else that goes into the upload.
    static uint64_t Signature(const Element& el, const ScreenImage& src, bool plain, bool nearest)
    {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
        mix((uint64_t)(el.screen + 1) << 48 | (uint64_t)el.sx << 32 | (uint64_t)el.sy << 16 | (uint64_t)el.sw);
        mix((uint64_t)el.sh << 32 | (uint64_t)el.alpha << 24 | (uint64_t)el.drop << 16 | (plain ? 1u : 0u) | (nearest ? 2u : 0u));
        for (uint32_t k : el.keys) mix(k);
        mix((uint64_t)el.keyTolerance << 32 | (uint32_t)el.keyRing);
        mix((uint64_t)el.cutX << 32 | (uint32_t)el.cutY);
        mix((uint64_t)el.dropSpriteCorner << 32 | (uint32_t)el.holeCount);
        for (int i = 0; i < el.holeCount; i++)
            for (int j = 0; j < 4; j++) mix((uint32_t)el.holes[i][j]);
        const bool flags = !plain && src.flags;
        for (int y = 0; y < el.sh; y++)
        {
            const uint32_t* row = src.pixels + (el.sy + y) * 256 + el.sx;
            // (Two pixels a step: the words of a row are mixed as 64-bit values.)
            int x = 0;
            for (; x + 1 < el.sw; x += 2) mix((uint64_t)row[x] << 32 | row[x + 1]);
            if (x < el.sw) mix(row[x]);
            if (flags)
            {
                const uint8_t* f = src.flags + (el.sy + y) * 256 + el.sx;
                uint64_t acc = 0;
                for (int i = 0; i < el.sw; i++)
                {
                    acc = acc << 8 | f[i];
                    if ((i & 7) == 7) { mix(acc); acc = 0; }
                }
                mix(acc);
            }
        }
        return h;
    }

    SDL_Texture* Slot(int i, int w, int h)
    {
        if ((int)Slots.size() <= i) Slots.resize(i + 1);
        return Make(Slots[i], w, h);
    }

    // The widescreen 3D picture with the game's fade, and the continuation of whatever 2D
    // reaches the left or right edge of the top screen (a wipe closing in, a flash).
    void DrawWide(const Scene& scene, const ScreenImage screens[2], int outW, int outH, const MergedView& v, bool smooth,
        const WideImage& wide)
    {
        SDL_Texture* tex = Make(WideTex, wide.w, wide.h);
        if (!tex) return;
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
        SDL_SetTextureScaleMode(tex, smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
        const auto t0 = std::chrono::steady_clock::now();
        const uint32_t* px = wide.placed ? wide.pixels
            : MergedFillWideHoles(wide, (int)std::floor(v.originX * wide.w / outW),
                  (int)std::ceil((v.originX + 256 * v.unit) * wide.w / outW), WideFilled);
        SDL_UpdateTexture(tex, nullptr, px, wide.w * 4);
        WideUploadMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        WideUploads++;
        if (wide.placed)
        {
            // The widened top screen, at its place in the view.
            SDL_SetTextureColorMod(tex, 255, 255, 255);
            const SDL_FRect dst = { v.originX + wide.baseX * v.unit, v.originY, wide.w * v.unit, wide.h * v.unit };
            SDL_RenderCopyF(R, tex, nullptr, &dst);
            return;
        }
        int k = 255;
        if (wide.fadeMode == 2) k = 255 * (16 - wide.fade) / 16;
        SDL_SetTextureColorMod(tex, k, k, k);
        SDL_RenderCopy(R, tex, nullptr, nullptr);
        if (wide.fadeMode == 1 && wide.fade)
        {
            SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(R, 255, 255, 255, 255 * wide.fade / 16);
            SDL_RenderFillRect(R, nullptr);
        }

        const ScreenImage& top = screens[scene.items[0].screen];
        if (!top.flags) return;
        const int left = (int)v.originX, right = (int)(v.originX + 256 * v.unit);
        const int y0 = (int)v.originY, h = (int)(192 * v.unit + 0.5f);
        // Rows of the top screen that are only black backdrop (a door's wipe): black across the view.
        {
            SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(R, 0, 0, 0, 255);
            for (int y = 0; y < 192;)
            {
                if (!MergedBlankRow(top, y)) { y++; continue; }
                int e = y;
                while (e < 192 && MergedBlankRow(top, e)) e++;
                const SDL_Rect r = { 0, y0 + (int)(y * v.unit), outW, (int)(e * v.unit + 0.5f) - (int)(y * v.unit) };
                SDL_RenderFillRect(R, &r);
                y = e;
            }
        }
        // Each row's continuation is rgb * eva / 16 + world * evb / 16 (see EdgeContinuation):
        // drawn premultiplied, with evb / 16 as the alpha that the world below is multiplied
        // by. Where the renderer can't blend that way, the usual blending comes close.
        if (EdgeMode == SDL_BLENDMODE_INVALID)
            EdgeMode = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
        for (int side = 0; side < 2; side++)
        {
            // A 16-pixel-wide texture per side, repeated outwards from the screen's edge: a
            // row that goes on as a pattern has its 16 edge pixels in it, any other row its
            // one colour 16 times.
            bool any = false, anyInner = false;
            for (int y = 0; y < 192; y++)
            {
                const EdgeContinuation c = MergedEdgeContinuation(scene, top, y, side);
                any |= !c.none();
                // (Continued from the next column in: the outermost column, the 3D world alone,
                // is covered by the same, or a slit shows at the screen's edge. Elsewhere: left as it is.)
                const bool inner = !c.none() && MergedEdgeColumn(top, y, side) != (side ? 255 : 0);
                anyInner |= inner;
                EdgeIn[y] = EdgeCustomFailed ? 0u : 0xFF000000u;
                uint32_t* row = &Edge[y * 16];
                if (c.pattern)
                {
                    if (inner) EdgeIn[y] = (EdgeCustomFailed ? 0xFF000000u : 0u) | (top.pixels[y * 256 + (side ? 239 : 16)] & 0xFFFFFF);
                    for (int k = 0; k < 16; k++)
                        row[k] = (EdgeCustomFailed ? 0xFF000000u : 0u) | (top.pixels[y * 256 + (side ? 240 : 0) + k] & 0xFFFFFF);
                    continue;
                }
                uint32_t r = (c.rgb >> 16) & 0xFF, g = (c.rgb >> 8) & 0xFF, b = c.rgb & 0xFF;
                uint32_t px;
                if (!EdgeCustomFailed)
                {
                    r = std::min(255u, r * c.eva / 16);
                    g = std::min(255u, g * c.eva / 16);
                    b = std::min(255u, b * c.eva / 16);
                    px = (uint32_t)std::min(255, c.evb * 255 / 16) << 24 | r << 16 | g << 8 | b;
                }
                else
                {
                    // src * a + world * (1 - a), with a = 1 - evb / 16 and src scaled to match.
                    const int a = 16 - std::min(c.evb, 16);
                    if (a > 0)
                    {
                        r = std::min(255u, r * c.eva / a);
                        g = std::min(255u, g * c.eva / a);
                        b = std::min(255u, b * c.eva / a);
                    }
                    px = (uint32_t)(a * 255 / 16) << 24 | r << 16 | g << 8 | b;
                }
                std::fill(row, row + 16, px);
                if (inner) EdgeIn[y] = px;
            }
            if (!any) continue;
            const float barW = side == 0 ? (float)left : (float)(outW - right);
            if (barW <= 0) continue;
            SDL_Texture* e = Make(EdgeTex[side], 16, 192);
            if (!e) continue;
            if (EdgeCustomFailed || SDL_SetTextureBlendMode(e, EdgeMode) != 0)
            {
                if (!EdgeCustomFailed)
                {
                    EdgeCustomFailed = true;
                    side--; // the same side again, with the colours for the usual blending
                    continue;
                }
                SDL_SetTextureBlendMode(e, SDL_BLENDMODE_BLEND);
            }
            SDL_SetTextureScaleMode(e, SDL_ScaleModeNearest);
            SDL_UpdateTexture(e, nullptr, Edge, 16 * 4);
            // Copies 16 base pixels wide, outwards from the screen's edge (the last one cut at
            // the edge of the view).
            const float tw = 16 * v.unit;
            for (int k = 0; k * tw < barW; k++)
            {
                const float x = side == 0 ? v.originX - (k + 1) * tw : v.originX + 256 * v.unit + k * tw;
                const float x0 = std::max(x, 0.0f), x1 = std::min(x + tw, (float)outW);
                if (x1 <= x0) continue;
                const SDL_Rect src = { (int)std::lround((x0 - x) / v.unit), 0,
                    std::max(1, (int)std::lround((x1 - x0) / v.unit)), 192 };
                const SDL_FRect dst = { x0, (float)y0, x1 - x0, (float)h };
                SDL_RenderCopyF(R, e, &src, &dst);
            }
            if (anyInner)
                if (SDL_Texture* in = Make(EdgeInTex[side], 1, 192))
                {
                    SDL_SetTextureBlendMode(in, EdgeCustomFailed ? SDL_BLENDMODE_BLEND : EdgeMode);
                    SDL_SetTextureScaleMode(in, SDL_ScaleModeNearest);
                    SDL_UpdateTexture(in, nullptr, EdgeIn, 4);
                    const SDL_FRect dst = { side ? v.originX + 255 * v.unit : v.originX, (float)y0, v.unit, (float)h };
                    SDL_RenderCopyF(R, in, nullptr, &dst);
                }
        }
        // The weather's particles beside the screen (Scene::weatherObj): the top screen's
        // sprites, drawn again one screen width to the left and to the right.
        if (scene.weatherObj && (left > 0 || right < outW))
        {
            // (Rows where 2D is continued beside the screen are left out.)
            for (int side = 0; side < 2; side++)
            {
                SDL_Texture* w = Make(WeatherTex[side], 256, 192);
                if (!w) continue;
                for (int y = 0; y < 192; y++)
                {
                    uint32_t* row = &Weather[y * 256];
                    if (!MergedWeatherRow(scene, top, y))
                    {
                        std::fill(row, row + 256, 0u);
                        continue;
                    }
                    for (int x = 0; x < 256; x++)
                    {
                        const int i = y * 256 + x;
                        row[x] = LayerClass(top.flags[i]) == LAYER_OBJ ? 0xFF000000u | (top.pixels[i] & 0xFFFFFF) : 0u;
                    }
                }
                SDL_SetTextureBlendMode(w, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(w, SDL_ScaleModeNearest);
                SDL_UpdateTexture(w, nullptr, Weather, 256 * 4);
                const SDL_FRect dst = { v.originX + (side ? 256 : -256) * v.unit, (float)y0, 256 * v.unit, (float)h };
                SDL_RenderCopyF(R, w, nullptr, &dst);
            }
        }
        (void)outH;
    }

    // The bars beside a 4:3 picture on a wide view (MergedNarrowBars): a colour per row.
    void DrawNarrowBars(const ScreenImage& top, int outW, int outH, const MergedView& v, bool calm)
    {
        uint32_t bars[2][192];
        MergedNarrowBars(top, bars[0], bars[1], calm);
        const int left = (int)v.originX, right = (int)(v.originX + 256 * v.unit);
        const float y0 = v.originY, h = 192 * v.unit;
        for (int side = 0; side < 2; side++)
        {
            SDL_Texture* t = Make(NarrowTex[side], 1, 192);
            if (!t) continue;
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
            SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
            SDL_UpdateTexture(t, nullptr, bars[side], 4);
            const SDL_FRect dst = side == 0 ? SDL_FRect { 0, y0, (float)left, h } : SDL_FRect { (float)right, y0, (float)(outW - right), h };
            if (dst.w > 0) SDL_RenderCopyF(R, t, nullptr, &dst);
        }
        (void)outH;
    }

    // The bars beside a battle: each row's outermost pixel that isn't black (EdgeFillColour),
    // darkening outwards (see Scene::edgeFill). The darkening is baked into a 16-pixel-wide texture per side.
    void DrawEdgeFill(const ScreenImage& top, int outW, const MergedView& v)
    {
        const int left = (int)v.originX, right = (int)(v.originX + 256 * v.unit);
        if (left <= 0) return;
        const int y0 = (int)v.originY, h = (int)(192 * v.unit + 0.5f);
        for (int side = 0; side < 2; side++)
        {
            for (int y = 0; y < 192; y++)
            {
                const uint32_t c = EdgeFillColour(top, y, side);
                for (int x = 0; x < 16; x++)
                {
                    // Column 0 is the outer edge on the left side, the inner one on the right.
                    const int d = side ? x : 15 - x;
                    const uint32_t k = 256 - (256 - EdgeFillOuter) * d / 15;
                    Fill[y * 16 + x] = 0xFF000000 | ((((c >> 16) & 0xFF) * k >> 8) << 16) | ((((c >> 8) & 0xFF) * k >> 8) << 8) | ((c & 0xFF) * k >> 8);
                }
            }
            SDL_Texture* t = Make(FillTex[side], 16, 192);
            if (!t) continue;
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
            SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
            SDL_UpdateTexture(t, nullptr, Fill, 16 * 4);
            const SDL_Rect dst = side == 0 ? SDL_Rect { 0, y0, left, h } : SDL_Rect { right, y0, outW - right, h };
            SDL_RenderCopy(R, t, nullptr, &dst);
        }
    }

public:
    // Time spent handing the widescreen picture to the renderer (for the frontend's timing line).
    double WideUploadMs = 0;
    unsigned WideUploads = 0;
    // (For SSR_EMU_PROF: the pieces' signatures, the piece under the world, the other pieces
    // that were uploaded, and how many were.)
    double SignatureMs = 0, UnderWorldMs = 0, UploadMs = 0;
    unsigned Uploads = 0;

private:
    SDL_Renderer* R;
    SDL_BlendMode Premultiplied;
    std::vector<Tex> Slots;
    std::vector<bool> CachedPremul; // (each slot's last blend mode, for a texture not uploaded again)
    Tex WideTex, EdgeTex[2], EdgeInTex[2], FillTex[2], NarrowTex[2], WeatherTex[2];
    uint32_t Fill[16 * 192];
    std::vector<SDL_Rect> Spans;
    std::vector<uint32_t> Buf, WideFilled;
    uint32_t Edge[16 * 192];
    uint32_t EdgeIn[192]; // the screen's own outermost column, where a row is continued from the next one in
    uint32_t Weather[256 * 192];
    SDL_BlendMode EdgeMode = SDL_BLENDMODE_INVALID;
    bool EdgeCustomFailed = false;
};
