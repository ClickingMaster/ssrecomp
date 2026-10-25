// Draws a merged-view scene with the SDL renderer: every piece is uploaded at DS
// resolution and the graphics card does the scaling, so the cost no longer depends on
// the size of the window. MergedCompose in merged.h draws the same picture on the CPU
// and is what the headless tests use; the two have to be kept in step.
#pragma once

#include <cstdio>
#include <cstdlib>
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
        static const bool profile = std::getenv("SSR_COMPOSE_PROF") != nullptr;
        const Uint64 frameStart = profile ? SDL_GetPerformanceCounter() : 0;
        uint32_t bg = 0;
        if (scene.bgScreen >= 0) bg = screens[scene.bgScreen].pixels[scene.bgY * 256 + scene.bgX] & 0xFFFFFF;
        const bool useWide = scene.wide && wide && wide->pixels && wide->w > 0 && wide->h > 0;
        const MergedView v = MergedMakeView(outW, outH);

        SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(R, useWide ? 0 : (bg >> 16) & 0xFF, useWide ? 0 : (bg >> 8) & 0xFF, useWide ? 0 : bg & 0xFF, 255);
        SDL_RenderClear(R);

        if (useWide) DrawWide(scene, screens, outW, outH, v, smooth, *wide);
        else if (scene.edgeFill && !scene.items.empty() && scene.items[0].screen >= 0) DrawEdgeFill(screens[scene.items[0].screen], outW, v);

        int slot = 0;
        bool first = true;
        for (const Element& item : scene.items)
        {
            Element el = item;
            const bool isFirst = first;
            first = false;
            if (useWide && isFirst && wide->placed) continue;
            if (useWide && isFirst) el.drop |= LAYER_3D;
            const float s = v.unit * el.scale;
            SDL_Rect dst;
            dst.x = (int)(v.originX + el.dx * v.unit);
            dst.y = (int)(v.originY + el.dy * v.unit);
            dst.w = (int)(el.sw * s + 0.5f);
            dst.h = (int)(el.sh * s + 0.5f);
            if (dst.w <= 0 || dst.h <= 0 || el.sw <= 0 || el.sh <= 0) continue;
            if (el.screen < 0)
            {
                SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(R, (el.keys[0] >> 16) & 0xFF, (el.keys[0] >> 8) & 0xFF, el.keys[0] & 0xFF, 255);
                SDL_RenderFillRect(R, &dst);
                continue;
            }
            const ScreenImage& src = screens[el.screen];

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

            const bool nearest = el.crisp || !smooth;
            const bool keyed = el.keys[0] != 0xFFFFFFFF;
            const bool spriteKey = el.dropSpriteCorner > 0 && src.flags;
            const bool plain = !(el.drop && src.flags) && !keyed && !spriteKey && el.alpha == 255 && el.cutX >= 256;
            SDL_Texture* tex = Slot(slot++, el.sw, el.sh);
            if (!tex) continue;
            SDL_SetTextureScaleMode(tex, nearest ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
            if (plain)
            {
                SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
                const Uint64 start = profile ? SDL_GetPerformanceCounter() : 0;
                SDL_UpdateTexture(tex, nullptr, src.pixels + el.sy * 256 + el.sx, 256 * 4);
                if (profile) UploadTicks += SDL_GetPerformanceCounter() - start;
            }
            else
            {
                // Pixels that are left out get alpha 0.
                bool premul = !nearest && SDL_SetTextureBlendMode(tex, Premultiplied) == 0;
                if (!premul) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
                Buf.resize((size_t)el.sw * el.sh);
                const uint32_t al = el.alpha;
                const Uint64 packStart = profile ? SDL_GetPerformanceCounter() : 0;
                for (int y = 0; y < el.sh; y++)
                {
                    const int sy = el.sy + y;
                    const uint32_t* row = src.pixels + sy * 256 + el.sx;
                    const uint8_t* frow = src.flags ? src.flags + sy * 256 + el.sx : nullptr;
                    uint32_t* out = &Buf[(size_t)y * el.sw];
                    // A row outside the cut/corner/ring needs no corresponding per-pixel checks.
                    const bool cutRow = sy >= el.cutY;
                    const bool cornerRow = spriteKey && std::min(y, el.sh - 1 - y) < el.dropSpriteCorner;
                    const bool ringRow = !el.keyRing || std::min(y, el.sh - 1 - y) < el.keyRing;
                    for (int x = 0; x < el.sw; x++)
                    {
                        uint32_t c = row[x] & 0xFFFFFF;
                        bool skip = (cutRow && el.sx + x >= el.cutX) ||
                            (el.drop && frow && (LayerClass(frow[x]) & el.drop)) ||
                            (cornerRow && LayerClass(frow[x]) == LAYER_OBJ && std::min(x, el.sw - 1 - x) < el.dropSpriteCorner) ||
                            (keyed && MergedKeyed(el, c) &&
                                (ringRow || std::min(x, el.sw - 1 - x) < el.keyRing));
                        if (skip)
                            out[x] = premul ? 0 : c;
                        else if (premul && al != 255)
                            out[x] = (al << 24) | ((((c >> 16) & 0xFF) * al / 255) << 16) |
                                ((((c >> 8) & 0xFF) * al / 255) << 8) | ((c & 0xFF) * al / 255);
                        else
                            out[x] = (al << 24) | c;
                    }
                }
                if (profile) PackTicks += SDL_GetPerformanceCounter() - packStart;
                const Uint64 uploadStart = profile ? SDL_GetPerformanceCounter() : 0;
                SDL_UpdateTexture(tex, nullptr, Buf.data(), el.sw * 4);
                if (profile) UploadTicks += SDL_GetPerformanceCounter() - uploadStart;
            }
            SDL_RenderCopy(R, tex, nullptr, &dst);
        }
        if (profile)
        {
            TotalTicks += SDL_GetPerformanceCounter() - frameStart;
            if (++ProfileFrames == 120)
            {
                const double ms = 1000.0 / (SDL_GetPerformanceFrequency() * ProfileFrames);
                std::fprintf(stderr, "compose: total %.2f ms/frame, pixel preparation %.2f, texture uploads %.2f (120 frames)\n",
                    TotalTicks * ms, PackTicks * ms, UploadTicks * ms);
                TotalTicks = PackTicks = UploadTicks = 0;
                ProfileFrames = 0;
            }
        }
    }

private:
    struct Tex { SDL_Texture* t = nullptr; int w = 0, h = 0; };

    SDL_Texture* Make(Tex& x, int w, int h)
    {
        if (x.t && x.w == w && x.h == h) return x.t;
        if (x.t) SDL_DestroyTexture(x.t);
        x.t = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        x.w = w;
        x.h = h;
        return x.t;
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
        SDL_UpdateTexture(tex, nullptr, wide.pixels, wide.w * 4);
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
        for (int side = 0; side < 2; side++)
        {
            const int sx = side == 0 ? 0 : 255;
            bool any = false;
            for (int y = 0; y < 192; y++)
            {
                bool ext = LayerClass(top.flags[y * 256 + sx]) != LAYER_3D;
                Edge[y] = ext ? (0xFF000000 | top.pixels[y * 256 + sx]) : 0;
                any |= ext;
            }
            if (!any) continue;
            SDL_Rect dst = side == 0 ? SDL_Rect { 0, y0, left, h } : SDL_Rect { right, y0, outW - right, h };
            if (dst.w <= 0) continue;
            SDL_Texture* e = Make(EdgeTex[side], 1, 192);
            if (!e) continue;
            SDL_SetTextureBlendMode(e, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(e, SDL_ScaleModeNearest);
            SDL_UpdateTexture(e, nullptr, Edge, 4);
            SDL_RenderCopy(R, e, nullptr, &dst);
        }
        (void)outH;
    }

    // The bars beside a battle: each row's outermost pixel, darkening outwards (see
    // Scene::edgeFill). The darkening is baked into a 16-pixel-wide texture per side.
    void DrawEdgeFill(const ScreenImage& top, int outW, const MergedView& v)
    {
        const int left = (int)v.originX, right = (int)(v.originX + 256 * v.unit);
        if (left <= 0) return;
        const int y0 = (int)v.originY, h = (int)(192 * v.unit + 0.5f);
        for (int side = 0; side < 2; side++)
        {
            for (int y = 0; y < 192; y++)
            {
                const uint32_t c = top.pixels[y * 256 + (side ? 255 : 0)];
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

    SDL_Renderer* R;
    SDL_BlendMode Premultiplied;
    std::vector<Tex> Slots;
    Tex WideTex, EdgeTex[2], FillTex[2];
    uint32_t Fill[16 * 192];
    std::vector<SDL_Rect> Spans;
    std::vector<uint32_t> Buf;
    uint32_t Edge[192];
    Uint64 TotalTicks = 0, PackTicks = 0, UploadTicks = 0;
    int ProfileFrames = 0;
};
