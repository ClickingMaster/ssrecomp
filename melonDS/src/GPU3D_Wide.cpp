// ssrecomp: widescreen, full-resolution rendering of the 3D scene. See GPU3D_Wide.h.
#include "GPU3D_Wide.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <type_traits>
#include <mutex>
#include <thread>

#include "NDS.h"
#include "GPU.h"
#include "GPU3D.h"
#include "GPU3D_Soft.h"

namespace melonDS
{
WideState Wide;

bool YSort(Polygon* a, Polygon* b);
extern u32 SubmitPolygonLimit, SubmitVertexLimit;

namespace
{
// Runs one job on several threads; the calling thread takes a share too.
class Workers
{
public:
    explicit Workers(int n)
    {
        for (int i = 0; i < n; i++) Threads.emplace_back([this, i] { Loop(i); });
    }
    ~Workers()
    {
        {
            std::lock_guard<std::mutex> lock(Mutex);
            Quit = true;
            Generation++;
        }
        Wake.notify_all();
        for (auto& t : Threads) t.join();
    }
    int Count() const { return (int)Threads.size(); }
    // Starts the job on all threads and returns; the job must stay valid until Wait().
    void Start(const std::function<void(int)>& job)
    {
        {
            std::lock_guard<std::mutex> lock(Mutex);
            Job = &job;
            Remaining = (int)Threads.size();
            Generation++;
        }
        Wake.notify_all();
    }
    void Wait()
    {
        std::unique_lock<std::mutex> lock(Mutex);
        Done.wait(lock, [this] { return Remaining == 0; });
    }

private:
    void Loop(int index)
    {
        u64 seen = 0;
        for (;;)
        {
            const std::function<void(int)>* job;
            {
                std::unique_lock<std::mutex> lock(Mutex);
                Wake.wait(lock, [&] { return Generation != seen; });
                seen = Generation;
                if (Quit) return;
                job = Job;
            }
            (*job)(index);
            {
                std::lock_guard<std::mutex> lock(Mutex);
                Remaining--;
            }
            Done.notify_all();
        }
    }

    std::vector<std::thread> Threads;
    std::mutex Mutex;
    std::condition_variable Wake, Done;
    const std::function<void(int)>* Job = nullptr;
    u64 Generation = 0;
    int Remaining = 0;
    bool Quit = false;
};

// The widescreen rasteriser works on groups of pixels with the compiler's vector types
// (SSE/AVX on x86, NEON on ARM): 8 at a time where AVX2 is on, otherwise 4.
#if defined(__AVX2__)
constexpr int Lanes = 8;
#else
constexpr int Lanes = 4;
#endif
typedef float VF __attribute__((vector_size(Lanes * 4)));
typedef s32 VI __attribute__((vector_size(Lanes * 4)));
static inline VF Select(VI mask, VF a, VF b) { return (VF)(((VI)a & mask) | ((VI)b & ~mask)); }
static inline VI Select(VI mask, VI a, VI b) { return (a & mask) | (b & ~mask); }
static inline bool Any(VI mask)
{
    s32 r = 0;
    for (int i = 0; i < Lanes; i++) r |= mask[i];
    return r != 0;
}
static inline VI LaneIndex()
{
    VI v;
    for (int i = 0; i < Lanes; i++) v[i] = i;
    return v;
}
static inline VI Clamp63(VI v) { return Select(v < 0, VI{}, Select(v > 63, VI{} + 63, v)); }
static inline VF LaneIndexF() { return __builtin_convertvector(LaneIndex(), VF); }
// A texture coordinate (whole texels) wrapped, mirrored or clamped to the texture's size.
static inline VI WrapCoord(VI t, int size, bool repeat, bool mirror)
{
    if (!repeat) return Select(t < 0, VI{}, Select(t >= size, VI{} + (size - 1), t));
    const VI m = t & (size - 1);
    if (!mirror) return m;
    return Select((t & size) != 0, (size - 1) - m, m);
}

struct WideVertex
{
    float X, Y;      // output pixels
    float InvW;      // 1 / normalised W
    float Z;         // depth as the DS stores it (Z or W, see Polygon::WBuffer)
    float R, G, B;   // 6-bit colour
    float S, T;      // texture coordinates, 1/16 texel
};
}

struct WideAccess
{
    static inline std::vector<Vertex> VtxReal, VtxInterp;
    static inline std::vector<Polygon> PolyReal, PolyInterp;
    // The wide view shows about a third more of the scene than the hardware has room for.
    static constexpr u32 MaxPolygons = 4096, MaxVertices = 12288;
    static inline std::vector<Polygon*> ListReal, ListInterp;
    static inline u32 NumReal = 0, NumInterp = 0;
    static inline bool Dirty = false, NeedReal = false, PendingPublish = false;
    static inline std::vector<u32> Back;
    static inline int BackW = 0, BackH = 0;
    // Per drawing thread, for the strip of rows it is drawing (see Draw): small enough to
    // stay in the processor's cache, unlike buffers the size of the picture.
    struct StripBuffers
    {
        std::vector<s32> Depth;
        std::vector<u8> Attr;    // bit 7 fog, bits 0-6 translucent polygon ID + 1
        std::vector<u16> Writer; // which opaque triangle (Tri::Order) drew the pixel
    };
    static inline std::unique_ptr<Workers> Pool;
    static inline bool Drawing = false;

    // Blocks until the picture being drawn in the background is finished.
    static void Wait()
    {
        if (Drawing) Pool->Wait();
        Drawing = false;
    }

    // Textures are decoded texel by texel as they are first used in a frame: an entry is
    // 0 until then, afterwards bit 31 is set and it holds red, green, blue (6 bits each,
    // from bit 0, 8 and 16) and alpha (5 bits from bit 24). Threads may decode the same
    // texel at once; they write the same value.
    struct Texture
    {
        u32 Param, Palette;   // Param without the wrapping bits
        int W, H;
        std::vector<u32> Texels;
    };
    static inline std::vector<std::unique_ptr<Texture>> Textures;

    // Submits the recorded frame again into the given buffers. With `interpolate`, every
    // vertex is first moved halfway back towards where the same polygon was in the
    // previous frame; `matched` then counts the polygons found in both frames. The
    // horizontal clip coordinate is multiplied by xnum/xden.
    static void Replay(GPU3D& g, bool interpolate, s64 xnum, s64 xden, Vertex* vram, Polygon* pram,
        Polygon** list, u32& numPolys, u32& matched, u32 polyLimit = 2048, u32 vertLimit = 6144)
    {
        SubmitPolygonLimit = polyLimit;
        SubmitVertexLimit = vertLimit;
        typedef GPU3D::RawPolygon RawPolygon;

        // Everything SubmitPolygon touches is saved and put back afterwards.
        Vertex savedTemp[4];
        memcpy(savedTemp, g.TempVertexBuffer, sizeof(savedTemp));
        u32 savedViewport[6];
        memcpy(savedViewport, g.Viewport, sizeof(savedViewport));
        const u32 savedPolygonMode = g.PolygonMode, savedAttr = g.CurPolygonAttr, savedTexParam = g.TexParam,
            savedTexPalette = g.TexPalette, savedConsecutive = g.NumConsecutivePolygons, savedDispCnt = g.DispCnt,
            savedNumVertices = g.NumVertices, savedNumPolygons = g.NumPolygons, savedNumOpaque = g.NumOpaquePolygons,
            savedSlotsFree = g.VertexSlotsFree;
        const s32 savedPipeline = g.PolygonPipeline, savedSlotCounter = g.VertexSlotCounter;
        Polygon* const savedLastStrip = g.LastStripPolygon;
        Vertex* const savedVertexRAM = g.CurVertexRAM;
        Polygon* const savedPolygonRAM = g.CurPolygonRAM;

        g.CurVertexRAM = vram;
        g.CurPolygonRAM = pram;
        g.NumVertices = g.NumPolygons = g.NumOpaquePolygons = 0;
        g.LastStripPolygon = nullptr;
        g.InReplay = true;

        // Polygons are matched in submission order. When the two frames do not line up (an
        // object entered or left), the search skips ahead a little; an unmatched polygon is
        // drawn at its new position.
        auto sameKind = [](const RawPolygon& a, const RawPolygon& b) {
            return a.PolygonMode == b.PolygonMode && a.PolygonAttr == b.PolygonAttr &&
                a.TexParam == b.TexParam && a.TexPalette == b.TexPalette;
        };
        // How a polygon moved between the two frames: the mean movement of its vertices in
        // clip space relative to depth (1024 = the full screen width). Returns false if any
        // vertex moved more than an eighth of the screen: that is a different polygon or a
        // camera cut, not motion to smooth out.
        auto movement = [](const RawPolygon& a, const RawPolygon& b, int nverts, s64& mx, s64& my) -> bool {
            s64 sum[2] = { 0, 0 };
            for (int i = 0; i < nverts; i++)
            {
                s64 wa = a.Vertices[i].Position[3], wb = b.Vertices[i].Position[3];
                if (wa < 0) wa = -wa;
                if (wb < 0) wb = -wb;
                s64 w = (wa > wb ? wa : wb);
                if (w < 4096) w = 4096;
                for (int c = 0; c < 2; c++)
                {
                    s64 d = ((s64)b.Vertices[i].Position[c] - a.Vertices[i].Position[c]) * 512 / w;
                    if (d > 128 || d < -128) return false;
                    sum[c] += d;
                }
            }
            mx = sum[0] / nverts;
            my = sum[1] / nverts;
            return true;
        };
        // How far a pairing is from what the rest of the picture does: the scene as a whole
        // moves with the camera, so the right partner of a polygon is the one that moved
        // like its neighbours, not the nearest one. (Taking the nearest paired a polygon
        // with a different one of the same model whenever the picture moved by more than
        // half the distance between them: running, and most at the bottom of the screen.)
        auto unlike = [](const RawPolygon& a, const RawPolygon& b, int nverts, s64 rx, s64 ry) -> s64 {
            s64 total = 0;
            for (int i = 0; i < nverts; i++)
            {
                s64 wa = a.Vertices[i].Position[3], wb = b.Vertices[i].Position[3];
                if (wa < 0) wa = -wa;
                if (wb < 0) wb = -wb;
                s64 w = (wa > wb ? wa : wb);
                if (w < 4096) w = 4096;
                s64 dx = ((s64)b.Vertices[i].Position[0] - a.Vertices[i].Position[0]) * 512 / w - rx;
                s64 dy = ((s64)b.Vertices[i].Position[1] - a.Vertices[i].Position[1]) * 512 / w - ry;
                total += (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            }
            return total;
        };

        // The movement most pairings agree on: every polygon votes with each candidate it
        // could be paired with; the true pairings all say the same, the false ones scatter.
        s64 sceneX = 0, sceneY = 0;
        if (interpolate && !g.RawPrevious.empty())
        {
            static std::vector<u16> votes;
            votes.assign(129 * 129, 0);
            const size_t np = g.RawPrevious.size(), nc = g.RawCurrent.size();
            const size_t step = nc > 600 ? nc / 600 : 1;
            int best = 0;
            for (size_t i = 0; i < nc; i += step)
            {
                const RawPolygon& cur = g.RawCurrent[i];
                const int nverts = (cur.PolygonMode & 0x1) ? 4 : 3;
                // The lists differ by whole objects that came or went, so the partner is
                // near the same place counted from the front of the list or from its end.
                for (int pass = 0; pass < 2; pass++)
                {
                    const s64 centre = pass == 0 ? (s64)i : (s64)i + (s64)np - (s64)nc;
                    if (pass == 1 && np == nc) break;
                    const s64 lo = std::max<s64>(0, centre - 48), hi = std::min<s64>((s64)np, centre + 48);
                    for (s64 k = lo; k < hi; k++)
                    {
                        s64 mx, my;
                        if (!sameKind(g.RawPrevious[k], cur) || !movement(g.RawPrevious[k], cur, nverts, mx, my)) continue;
                        const int bx = (int)(mx + 128) / 2, by = (int)(my + 128) / 2;
                        const int n = ++votes[by * 129 + bx];
                        if (n > best)
                        {
                            best = n;
                            sceneX = bx * 2 - 128;
                            sceneY = by * 2 - 128;
                        }
                    }
                }
            }
        }

        size_t ia = 0;
        s64 lastPair = -1;
        static const bool dbgPairOn = getenv("SSR_PAIR_DEBUG") != nullptr;
        const bool dbgPair = dbgPairOn && interpolate && xnum == xden;
        s64 dbgOff = 0;
        if (dbgPair) fprintf(stderr, "[pair] --- frame, scene motion %lld,%lld\n", (long long)sceneX, (long long)sceneY);
        matched = 0;
        for (const RawPolygon& cur : g.RawCurrent)
        {
            const int nverts = (cur.PolygonMode & 0x1) ? 4 : 3;
            memcpy(g.TempVertexBuffer, cur.Vertices, sizeof(cur.Vertices));

            // Polygons of a strip share two corners with the one before, and the DS takes
            // those from the polygon already built. That is only right here if both were
            // moved the same way: both paired, with neighbours in the previous frame as
            // well, or both left where they are. Otherwise this one is built on its own;
            // sharing would give it two corners from the in-between position and the rest
            // from the new one, which tore faces apart (seen where a model meets the edge
            // of the picture and some of its polygons no longer pair).
            bool keepStrip = true;
            if (interpolate)
            {
                // Scenes are full of identical tiles and models, so of the candidates the one
                // that moved the way the scene did is taken.
                const RawPolygon* prev = nullptr;
                s64 bestMove = -1;
                size_t bestIndex = 0;
                // A partner has to have moved roughly as the scene did (within a few DS
                // pixels per vertex, more when the scene moves fast; that leaves room for things
                // that move by themselves).
                // Without that limit, when a model leaves the picture the next model of the
                // same kind was paired with it and drawn halfway towards where it had been.
                static const bool loose = getenv("SSR_PAIR_LOOSE") != nullptr;
                const s64 speed = (sceneX < 0 ? -sceneX : sceneX) + (sceneY < 0 ? -sceneY : sceneY);
                const s64 limit = loose ? INT64_MAX : nverts * (12 + speed / 2);
                // First the polygon that follows the last partner: within an object the two
                // lists run in step, and that also pairs what moves by itself (a windmill's
                // blades, a walking person) and what is so close to the camera that it moves
                // unlike the rest. A neighbouring tile or model is further off than this allows.
                if (ia < g.RawPrevious.size() && !loose)
                {
                    s64 mx, my;
                    if (sameKind(g.RawPrevious[ia], cur) && movement(g.RawPrevious[ia], cur, nverts, mx, my) &&
                        unlike(g.RawPrevious[ia], cur, nverts, sceneX, sceneY) <= nverts * 40)
                    {
                        bestMove = 0;
                        bestIndex = ia;
                    }
                }
                for (size_t k = ia; bestMove < 0 && k < g.RawPrevious.size() && k < ia + 512; k++)
                {
                    s64 mx, my;
                    if (!sameKind(g.RawPrevious[k], cur) || !movement(g.RawPrevious[k], cur, nverts, mx, my)) continue;
                    s64 m = unlike(g.RawPrevious[k], cur, nverts, sceneX, sceneY);
                    if (m > limit) continue;
                    if (bestMove < 0 || m < bestMove)
                    {
                        bestMove = m;
                        bestIndex = k;
                        if (m <= nverts * 6) break; // within a pixel or so per vertex of the scene's movement
                    }
                }
                if (bestMove >= 0)
                {
                    prev = &g.RawPrevious[bestIndex];
                    ia = bestIndex + 1;
                }
                const s64 pairIndex = prev ? (s64)bestIndex : -1;
                keepStrip = (pairIndex < 0 && lastPair < 0) || (pairIndex >= 0 && lastPair >= 0 && pairIndex == lastPair + 1);
                lastPair = pairIndex;
                if (dbgPair)
                {
                    const size_t ci = &cur - g.RawCurrent.data();
                    const s64 off = pairIndex < 0 ? INT64_MIN : pairIndex - (s64)ci;
                    if (off != dbgOff)
                        fprintf(stderr, "[pair] cur %zu/%zu -> prev %lld/%zu (mode %u, unlike %lld)\n", ci, g.RawCurrent.size(),
                            (long long)pairIndex, g.RawPrevious.size(), cur.PolygonMode, (long long)bestMove);
                    dbgOff = off;
                }
                if (prev)
                {
                    matched++;
                    for (int i = 0; i < nverts; i++)
                    {
                        Vertex& v = g.TempVertexBuffer[i];
                        const Vertex& p = prev->Vertices[i];
                        for (int c = 0; c < 4; c++) v.Position[c] = (s32)(((s64)v.Position[c] + p.Position[c]) / 2);
                        for (int c = 0; c < 3; c++) v.Color[c] = (v.Color[c] + p.Color[c]) / 2;
                        for (int c = 0; c < 2; c++)
                        {
                            // Texture coordinates that jump (wrapping animations) are left at their new value.
                            int d = v.TexCoords[c] - p.TexCoords[c];
                            if (d > -512 && d < 512) v.TexCoords[c] = (s16)((v.TexCoords[c] + p.TexCoords[c]) / 2);
                        }
                    }
                }
            }

            if (xnum != xden)
                for (int i = 0; i < nverts; i++)
                    g.TempVertexBuffer[i].Position[0] = (s32)((s64)g.TempVertexBuffer[i].Position[0] * xnum / xden);

            g.PolygonMode = cur.PolygonMode;
            g.CurPolygonAttr = cur.PolygonAttr;
            g.TexParam = cur.TexParam;
            g.TexPalette = cur.TexPalette;
            memcpy(g.Viewport, cur.Viewport, sizeof(g.Viewport));
            g.NumConsecutivePolygons = cur.NumConsecutivePolygons;
            if (cur.StripStart || !keepStrip) g.LastStripPolygon = nullptr;
            g.SubmitPolygon();
        }

        numPolys = g.NumPolygons;
        if (g.NumPolygons)
        {
            u32 io = 0, it = g.NumOpaquePolygons;
            for (u32 i = 0; i < g.NumPolygons; i++)
            {
                Polygon* poly = &g.CurPolygonRAM[i];
                if (poly->Translucent)
                    list[it++] = poly;
                else
                    list[io++] = poly;
            }
            std::stable_sort(list, list + ((g.FlushAttributes & 0x1) ? g.NumOpaquePolygons : g.NumPolygons), YSort);
        }
        SubmitPolygonLimit = 2048;
        SubmitVertexLimit = 6144;

        g.InReplay = false;
        memcpy(g.TempVertexBuffer, savedTemp, sizeof(savedTemp));
        memcpy(g.Viewport, savedViewport, sizeof(savedViewport));
        g.PolygonMode = savedPolygonMode;
        g.CurPolygonAttr = savedAttr;
        g.TexParam = savedTexParam;
        g.TexPalette = savedTexPalette;
        g.NumConsecutivePolygons = savedConsecutive;
        g.DispCnt = savedDispCnt;
        g.NumVertices = savedNumVertices;
        g.NumPolygons = savedNumPolygons;
        g.NumOpaquePolygons = savedNumOpaque;
        g.VertexSlotsFree = savedSlotsFree;
        g.PolygonPipeline = savedPipeline;
        g.VertexSlotCounter = savedSlotCounter;
        g.LastStripPolygon = savedLastStrip;
        g.CurVertexRAM = savedVertexRAM;
        g.CurPolygonRAM = savedPolygonRAM;
    }

    // A frame has been flushed: builds the wide polygon lists for it.
    static void OnFlush(GPU3D& g)
    {
        if (VtxReal.empty())
        {
            VtxReal.resize(MaxVertices);
            VtxInterp.resize(MaxVertices);
            PolyReal.resize(MaxPolygons);
            PolyInterp.resize(MaxPolygons);
            ListReal.resize(MaxPolygons);
            ListInterp.resize(MaxPolygons);
        }
        if (getenv("SSR_WIDE_COUNT"))
        {
            // Development aid: how close the frame came to the DS's polygon and vertex memory.
            static u32 maxP = 0, maxV = 0;
            if (g.NumPolygons > maxP + 50 || g.NumVertices > maxV + 150)
            {
                maxP = std::max(maxP, g.NumPolygons);
                maxV = std::max(maxV, g.NumVertices);
                fprintf(stderr, "[wide] DS lists: %u of 2048 polygons, %u of 6144 vertices (raw %zu)\n", g.NumPolygons, g.NumVertices, g.RawCurrent.size());
            }
        }
        float view = Wide.ViewWidth < 256 ? 256 : Wide.ViewWidth;
        const s64 xden = 4096, xnum = (s64)(4096.0f * 256.0f / view + 0.5f);
        u32 matched;
        Replay(g, false, xnum, xden, VtxReal.data(), PolyReal.data(), ListReal.data(), NumReal, matched, MaxPolygons, MaxVertices);
        NumInterp = 0;
        if (g.InterpPending)
            Replay(g, true, xnum, xden, VtxInterp.data(), PolyInterp.data(), ListInterp.data(), NumInterp, matched, MaxPolygons, MaxVertices);
        Dirty = true;
    }

    // Called where the 3D renderer draws a frame. The image drawn here belongs to the
    // display frame after this one, like the 3D renderer's own, so it is published one
    // call later.
    static void OnRender(GPU& gpu, bool showingInterp)
    {
        Wait();
        if (PendingPublish)
        {
            Wide.Front.swap(Back);
            std::swap(Wide.FrontW, BackW);
            std::swap(Wide.FrontH, BackH);
            Wide.FrontSerial++;
            PendingPublish = false;
        }
        if (Wide.Width <= 0 || Wide.Height <= 0) return;
        if (showingInterp && NumInterp)
        {
            Draw(gpu, ListInterp.data(), NumInterp);
            NeedReal = true;
            Dirty = false;
            PendingPublish = true;
        }
        else if (Dirty || NeedReal || BackW != Wide.Width || BackH != Wide.Height)
        {
            Draw(gpu, ListReal.data(), NumReal);
            NeedReal = false;
            Dirty = false;
            PendingPublish = true;
        }
    }

    static void Draw(GPU& gpu, Polygon** list, u32 npolys)
    {
        const int W = Wide.Width, H = Wide.Height;
        const GPU3D& g = gpu.GPU3D;
        const SoftRenderer& soft = static_cast<const SoftRenderer&>(g.GetCurrentRenderer());

        Back.resize((size_t)W * H);
        BackW = W;
        BackH = H;

        // Rear plane.
        const s32 clearZ = (s32)(((g.RenderClearAttr2 & 0x7FFF) * 0x200) + 0x1FF);
        const u32 clearAlpha = (g.RenderClearAttr1 >> 16) & 0x1F;
        u32 clearColor = 0;
        if (clearAlpha)
        {
            u32 r = (g.RenderClearAttr1 << 3) & 0xF8, gr = (g.RenderClearAttr1 >> 2) & 0xF8, b = (g.RenderClearAttr1 >> 7) & 0xF8;
            clearColor = 0xFF000000 | ((r | (r >> 5)) << 16) | ((gr | (gr >> 5)) << 8) | (b | (b >> 5));
        }
        const u8 clearAttr = (g.RenderClearAttr1 & (1 << 15)) ? 0x80 : 0;

        // Screen-space vertices of every polygon, as fans of triangles.
        struct Tri
        {
            u32 Attr, TexParam;
            bool WBuffer;
            WideVertex V[3];
            int Y0, Y1; // rows touched
            Texture* Tex;
            bool FrontFirst; // drawn out of order: on equal depth it wins if it came first
            u16 Order;       // its place among the opaque triangles, in the game's order
            float NearZ;
        };
        // Opaque polygons arrive in no particular order of depth (the hardware sorts them by
        // screen position). Drawn like that, a forest shades every pixel many times over, so
        // they are drawn near to far instead and the depth test rejects what is hidden
        // before any shading. To keep the outcome the same, on equal depth the one earlier in
        // the original order wins (Writer records who drew each pixel). Polygons that test
        // for equal depth (decals) need what is under them drawn first, and translucent ones
        // are blended in order, so those follow in their original order.
        static std::vector<Tri> tris, opaqueTris, laterTris;
        tris.clear();
        opaqueTris.clear();
        laterTris.clear();
        const float sx = W / (256.0f * 16), sy = H / (192.0f * 16);
        const bool texturing = g.RenderDispCnt & 1;
        size_t texturesUsed = 0;
        auto findTexture = [&](u32 param, u32 palette) -> Texture* {
            param &= ~0x000F0000u; // wrapping is applied when sampling
            for (size_t i = 0; i < texturesUsed; i++)
                if (Textures[i]->Param == param && Textures[i]->Palette == palette) return Textures[i].get();
            if (texturesUsed == Textures.size()) Textures.push_back(std::make_unique<Texture>());
            Texture* t = Textures[texturesUsed++].get();
            t->Param = param;
            t->Palette = palette;
            t->W = 8 << ((param >> 20) & 0x7);
            t->H = 8 << ((param >> 23) & 0x7);
            t->Texels.assign((size_t)t->W * t->H, 0);
            return t;
        };
        for (u32 p = 0; p < npolys; p++)
        {
            const Polygon* poly = list[p];
            if (!poly || poly->Degenerate || poly->IsShadowMask || poly->IsShadow || poly->Type != 0) continue;
            if (poly->NumVertices < 3) continue;
            Texture* tex = texturing && ((poly->TexParam >> 26) & 0x7) ? findTexture(poly->TexParam, poly->TexPalette) : nullptr;
            WideVertex v[10];
            for (u32 i = 0; i < poly->NumVertices; i++)
            {
                const Vertex* vtx = poly->Vertices[i];
                v[i].X = vtx->HiresPosition[0] * sx;
                v[i].Y = vtx->HiresPosition[1] * sy;
                v[i].InvW = poly->FinalW[i] > 0 ? 1.0f / poly->FinalW[i] : 1.0f;
                v[i].Z = (float)poly->FinalZ[i];
                v[i].R = (float)(vtx->FinalColor[0] >> 3);
                v[i].G = (float)(vtx->FinalColor[1] >> 3);
                v[i].B = (float)(vtx->FinalColor[2] >> 3);
                v[i].S = (float)vtx->TexCoords[0];
                v[i].T = (float)vtx->TexCoords[1];
            }
            for (u32 i = 1; i + 1 < poly->NumVertices; i++)
            {
                Tri t;
                t.Attr = poly->Attr;
                t.TexParam = poly->TexParam;
                t.WBuffer = poly->WBuffer;
                t.Tex = tex;
                t.V[0] = v[0];
                t.V[1] = v[i];
                t.V[2] = v[i + 1];
                float ymin = std::min({ t.V[0].Y, t.V[1].Y, t.V[2].Y }), ymax = std::max({ t.V[0].Y, t.V[1].Y, t.V[2].Y });
                // Spans are widened by a hair so that neighbouring polygons leave no gaps.
                t.Y0 = std::max(0, (int)std::ceil(ymin - 0.5f - 0.4f));
                t.Y1 = std::min(H - 1, (int)std::floor(ymax - 0.5f + 0.4f));
                if (t.Y1 < t.Y0) continue;
                t.FrontFirst = !poly->Translucent && !(poly->Attr & (1 << 14));
                t.NearZ = std::min({ t.V[0].Z, t.V[1].Z, t.V[2].Z });
                if (t.FrontFirst) t.Order = (u16)std::min<size_t>(opaqueTris.size(), 0xFFFE);
                (t.FrontFirst ? opaqueTris : laterTris).push_back(t);
            }
        }
        // Nearest first (by its nearest corner), so that the depth test rejects as much as
        // possible; on equal depth the one first in the game's order wins (Writer).
        std::stable_sort(opaqueTris.begin(), opaqueTris.end(), [](const Tri& a, const Tri& b) { return a.NearZ < b.NearZ; });
        tris.assign(opaqueTris.begin(), opaqueTris.end());
        tris.insert(tris.end(), laterTris.begin(), laterTris.end());

        // The drawing itself runs on other threads while emulation goes on, so everything it
        // reads is copied here first. (Texture memory stays as it is until the next frame is
        // rendered, and WideWait() is called before that.)
        static u16 toonCopy[32];
        static u8 fogDensity[34];
        memcpy(toonCopy, g.RenderToonTable, sizeof(toonCopy));
        memcpy(fogDensity, g.RenderFogDensityTable, sizeof(fogDensity));
        const u32 fogOffset = g.RenderFogOffset, fogShift = g.RenderFogShift;
        const GPU* const gpuP = &gpu;
        const SoftRenderer* const softP = &soft;
        const u32 dispCnt = g.RenderDispCnt;
        const bool blendEnabled = dispCnt & (1 << 3);
        const u32 alphaRef = g.RenderAlphaRef;
        const bool fogEnabled = dispCnt & (1 << 7);
        const bool fogColorOn = !(dispCnt & (1 << 6));
        const u32 fogR = (g.RenderFogColor << 3) & 0xF8, fogG = (g.RenderFogColor >> 2) & 0xF8, fogB = (g.RenderFogColor >> 7) & 0xF8;

        if (!Pool)
        {
            int n = (int)std::thread::hardware_concurrency() - 2;
#if defined(__x86_64__) || defined(__i386__)
            // x86 processors mostly run two threads per core. Drawing threads on the second
            // thread of a core slow down the emulation running on the first, and measured
            // (6 cores, 12 threads) 3-5 threads were quicker overall than 10.
            n = (int)std::thread::hardware_concurrency() / 2 - 1;
#else
            // On ARM64 SoCs with 4 cores (such as Cortex-A53), now that native 3D software rasterization
            // is skipped in widescreen mode, 2 worker threads provide the optimal throughput without contention.
            if (std::thread::hardware_concurrency() == 4) n = 2;
            else if (std::thread::hardware_concurrency() < 4) n = 1;
#endif
            if (const char* forced = getenv("SSR_WIDE_THREADS")) n = atoi(forced);
            Pool = std::make_unique<Workers>(std::clamp(n, 1, 16));
        }

        // The picture is drawn in strips of rows that the threads take one after another, so
        // that a thread whose strip is quick (sky, a plain floor) goes on to help elsewhere.
        const int stripRows = 16, strips = (H + stripRows - 1) / stripRows;
        static std::atomic<int> nextStrip;
        nextStrip = 0;
        auto strip = [=](int index) {
            const int rowA = index * stripRows, rowB = std::min(H, rowA + stripRows);
            const size_t stripPixels = (size_t)(rowB - rowA) * W;
            static thread_local StripBuffers buffers;
            const size_t neededDepth = (size_t)stripRows * W + Lanes;
            if (buffers.Depth.size() < neededDepth)
            {
                buffers.Depth.resize(neededDepth);
                buffers.Attr.resize((size_t)stripRows * W);
                buffers.Writer.resize(neededDepth);
            }
            std::fill(Back.begin() + (size_t)rowA * W, Back.begin() + (size_t)rowB * W, clearColor);
            std::fill(buffers.Depth.begin(), buffers.Depth.begin() + stripPixels, clearZ);
            memset(buffers.Attr.data(), clearAttr, stripPixels);
            memset(buffers.Writer.data(), 0xFF, stripPixels * sizeof(u16));

            // One copy of the loop per kind of triangle (textured or not, Z- or W-buffered,
            // one colour or shaded), so that each pixel tests and keeps live only what its
            // kind needs.
            auto raster = [&](const Tri& t, auto texC, auto wbC, auto flatC) {
                    const WideVertex& a = t.V[0];
                    const WideVertex& b = t.V[1];
                    const WideVertex& c = t.V[2];
                    double area = (double)(b.X - a.X) * (c.Y - a.Y) - (double)(c.X - a.X) * (b.Y - a.Y);
                    if (area == 0) return;
                    const double inv = 1.0 / area;
                    // Barycentric weight i at (x, y) = E[i][0] * x + E[i][1] * y + E[i][2].
                    const double E[3][3] = {
                        { (b.Y - c.Y) * inv, (c.X - b.X) * inv, ((double)b.X * c.Y - (double)c.X * b.Y) * inv },
                        { (c.Y - a.Y) * inv, (a.X - c.X) * inv, ((double)c.X * a.Y - (double)a.X * c.Y) * inv },
                        { (a.Y - b.Y) * inv, (b.X - a.X) * inv, ((double)a.X * b.Y - (double)b.X * a.Y) * inv },
                    };

                    const u32 attr = t.Attr;
                    const u32 polyAlpha = (attr >> 16) & 0x1F;
                    const bool depthEqual = attr & (1 << 14);
                    constexpr bool wbuffer = decltype(wbC)::value;
                    const s32 equalTolerance = wbuffer ? 0xFF : 0x200;
                    const u8 polyId = (u8)(((attr >> 24) & 0x3F) + 1);
                    const u8 fogBit = (attr & (1 << 15)) ? 0x80 : 0;
                    // In W-buffer mode the stored depth is W; the vertices carry it scaled.
                    const float wScale = wbuffer ? a.Z * a.InvW : 0;
                    constexpr bool textured = decltype(texC)::value;
                    Texture* const tex = t.Tex;
                    const u32 texParam = t.TexParam;
                    const int texW = tex ? tex->W : 0, texH = tex ? tex->H : 0;
                    const u32 blendMode = (attr >> 4) & 0x3;
                    const bool highlight = blendMode == 2 && (dispCnt & (1 << 1));
                    const bool toon = blendMode == 2 && !highlight;
                    // Most polygons have one colour at all three corners.
                    constexpr bool flat = decltype(flatC)::value;
                    const int flatR = (int)a.R, flatG = (int)a.G, flatB = (int)a.B;

                    // Local copies: the loop below writes through byte pointers, which would
                    // otherwise make the compiler read all of this from memory again per pixel.
                    const float aIW = a.InvW, bIW = b.InvW, cIW = c.InvW, aZ = a.Z, bZ = b.Z, cZ = c.Z;
                    const float aR = a.R, bR = b.R, cR = c.R, aG = a.G, bG = b.G, cG = c.G, aB = a.B, bB = b.B, cB = c.B;
                    const float aS = a.S, bS = b.S, cS = c.S, aT = a.T, bT = b.T, cT = c.T;
                    u32* const texels = tex ? tex->Texels.data() : nullptr;
                    // A texel, decoded the first time it is used in this frame.
                    auto fetchTexel = [&](int ts, int tv) -> u32 {
                        u32 texel = texels[(size_t)tv * texW + ts];
                        if (!texel)
                        {
                            u16 tcolor;
                            u8 talpha;
                            softP->TextureLookup(*gpuP, tex->Param, tex->Palette, (s16)(ts << 4), (s16)(tv << 4), &tcolor, &talpha);
                            u32 tr = (tcolor << 1) & 0x3E; if (tr) tr++;
                            u32 tg = (tcolor >> 4) & 0x3E; if (tg) tg++;
                            u32 tb = (tcolor >> 9) & 0x3E; if (tb) tb++;
                            texel = 0x80000000 | tr | (tg << 8) | (tb << 16) | ((u32)talpha << 24);
                            texels[(size_t)tv * texW + ts] = texel;
                        }
                        return texel;
                    };
                    const bool frontFirst = t.FrontFirst;
                    const u16 order = t.Order;
                    const int Wl = W;
                    u32* const backData = Back.data();
                    // (the strip's buffers start at its first row)
                    s32* const depthData = buffers.Depth.data() - (ptrdiff_t)rowA * W;
                    u8* const attrData = buffers.Attr.data() - (ptrdiff_t)rowA * W;
                    u16* const writerData = buffers.Writer.data() - (ptrdiff_t)rowA * W;
                    const u16* const toonTable = toonCopy;
                    // Vertices are placed to a sixteenth of a DS pixel, which is a visible fraction
                    // of an output pixel, so where three polygons meet along an edge a sliver can
                    // stay open. Each polygon is grown by a third of an output pixel to close them.
                    const double margin[3] = {
                        0.34 * std::sqrt(E[0][0] * E[0][0] + E[0][1] * E[0][1]),
                        0.34 * std::sqrt(E[1][0] * E[1][0] + E[1][1] * E[1][1]),
                        0.34 * std::sqrt(E[2][0] * E[2][0] + E[2][1] * E[2][1]),
                    };

                    const double xmin = std::min({ (double)a.X, (double)b.X, (double)c.X });
                    const double xmax = std::max({ (double)a.X, (double)b.X, (double)c.X });
                    const int y0 = std::max(t.Y0, rowA), y1 = std::min(t.Y1, rowB - 1);
                    for (int y = y0; y <= y1; y++)
                    {
                        const double py = y + 0.5;
                        // The span of this row: where all three weights are non-negative.
                        double lo = 0, hi = Wl;
                        bool empty = false;
                        for (int i = 0; i < 3; i++)
                        {
                            const double k = E[i][1] * py + E[i][2] + margin[i];
                            if (E[i][0] > 1e-12)
                                lo = std::max(lo, -k / E[i][0]);
                            else if (E[i][0] < -1e-12)
                                hi = std::min(hi, -k / E[i][0]);
                            else if (k < -1e-6)
                                empty = true;
                        }
                        if (empty) continue;
                        // At a sharp corner the widened edges meet well beyond the vertex, which
                        // would draw a streak along that row; the span ends where the triangle does.
                        lo = std::max(lo, xmin - 0.4);
                        hi = std::min(hi, xmax + 0.4);
                        int x0 = (int)std::ceil(lo - 0.5 - 0.02), x1 = (int)std::floor(hi - 0.5 + 0.02);
                        if (x0 < 0) x0 = 0;
                        if (x1 >= Wl) x1 = Wl - 1;
                        if (x1 < x0) continue;

                        u32* color = backData + (size_t)y * Wl;
                        s32* depth = depthData + (size_t)y * Wl;
                        u8* pattr = attrData + (size_t)y * Wl;
                        u16* writer = writerData + (size_t)y * Wl;

                        const float l0 = (float)(E[0][0] * (x0 + 0.5) + E[0][1] * py + E[0][2]);
                        const float l1 = (float)(E[1][0] * (x0 + 0.5) + E[1][1] * py + E[1][2]);
                        const float s0 = (float)E[0][0], s1 = (float)E[1][0];
                        // Up to the depth test and the texel's place, a group of pixels at a time (SIMD);
                        // what passes is then textured, shaded and written one pixel at a time.
                        for (int xb = x0; xb <= x1; xb += Lanes)
                        {
                            const int n = std::min(Lanes, x1 - xb + 1);
                            const VF k = (float)(xb - x0) + LaneIndexF();
                            const VF f0 = l0 + k * s0, f1 = l1 + k * s1;
                            // Samples a hair outside the triangle (the widened spans) are pulled
                            // back onto it, and all of them slightly towards its middle, so that
                            // texture coordinates never reach past the polygon's own texels.
                            VF w0 = Select(f0 < 0, VF{}, f0), w1 = Select(f1 < 0, VF{}, f1), w2 = 1.0f - w0 - w1;
                            const VI outside = w2 < 0;
                            if (Any(outside))
                            {
                                const VF kk = 1.0f / (w0 + w1);
                                w0 = Select(outside, w0 * kk, w0);
                                w1 = Select(outside, w1 * kk, w1);
                                w2 = Select(outside, VF{}, w2);
                            }
                            w0 = w0 * 0.9994f + 0.0002f;
                            w1 = w1 * 0.9994f + 0.0002f;
                            w2 = w2 * 0.9994f + 0.0002f;
                            // Perspective-correct weights.
                            VF q0 = w0 * aIW, q1 = w1 * bIW, q2 = w2 * cIW;
                            const VF qs = q0 + q1 + q2;
                            VI ok = (qs > 0) & (LaneIndex() < n);
                            const VF iq = 1.0f / qs;
                            // In W-buffer mode the stored depth is W; the vertices carry it scaled.
                            const VI z = wbuffer ? __builtin_convertvector(wScale * iq, VI) : __builtin_convertvector(w0 * aZ + w1 * bZ + w2 * cZ, VI);
                            VI dz;
                            memcpy(&dz, depth + xb, sizeof(dz));
                            if (depthEqual)
                            {
                                const VI diff = dz - z;
                                ok &= (diff >= -equalTolerance) & (diff <= equalTolerance);
                            }
                            else if (frontFirst)
                            {
                                // On equal depth the one first in the game's order wins.
                                VI wr;
                                for (int i = 0; i < Lanes; i++) wr[i] = writer[xb + i];
                                ok &= (z < dz) | ((z == dz) & ((s32)order < wr));
                            }
                            else
                                ok &= z < dz;
                            if (!Any(ok)) continue;
                            q0 *= iq;
                            q1 *= iq;
                            q2 *= iq;
                            VI vts{}, vtv{};
                            if (textured)
                            {
                                // (+32768 keeps the value positive so the conversion rounds down)
                                vts = (__builtin_convertvector(q0 * aS + q1 * bS + q2 * cS + 32768.0f, VI) - 32768) >> 4;
                                vtv = (__builtin_convertvector(q0 * aT + q1 * bT + q2 * cT + 32768.0f, VI) - 32768) >> 4;
                                vts = WrapCoord(vts, texW, texParam & (1 << 16), texParam & (1 << 18));
                                vtv = WrapCoord(vtv, texH, texParam & (1 << 17), texParam & (1 << 19));
                            }
                            if (!highlight && !toon)
                            {
                                // The usual case, on the whole group: the texel and alpha test, the colour, and
                                // what goes into the picture; only the writes are made one pixel at a time.
                                VI alphaV = VI{} + (s32)polyAlpha, texel{};
                                if (textured)
                                {
                                    for (int i = 0; i < n; i++)
                                        if (ok[i]) texel[i] = (s32)fetchTexel(vts[i], vtv[i]);
                                    if (!(blendMode & 1))
                                        alphaV = ((((texel >> 24) & 0x1F) + 1) * (s32)(polyAlpha + 1) - 1) >> 5;
                                }
                                if (polyAlpha == 0) alphaV = VI{} + 31; // wireframe polygons are drawn filled here
                                ok &= alphaV > (s32)alphaRef;
                                if (!Any(ok)) continue;
                                VI vr = VI{} + flatR, vg = VI{} + flatG, vb = VI{} + flatB;
                                if (!flat)
                                {
                                    vr = Clamp63(__builtin_convertvector(q0 * aR + q1 * bR + q2 * cR + 0.5f, VI));
                                    vg = Clamp63(__builtin_convertvector(q0 * aG + q1 * bG + q2 * cG + 0.5f, VI));
                                    vb = Clamp63(__builtin_convertvector(q0 * aB + q1 * bB + q2 * cB + 0.5f, VI));
                                }
                                VI vR = vr, vG = vg, vB = vb;
                                if (textured)
                                {
                                    const VI tr = texel & 0x3F, tg = (texel >> 8) & 0x3F, tb = (texel >> 16) & 0x3F, ta = (texel >> 24) & 0x1F;
                                    if (blendMode & 1)
                                    {
                                        // decal
                                        const VI none = ta == 0, full = ta == 31;
                                        vR = Select(none, vr, Select(full, tr, (tr * ta + vr * (31 - ta)) >> 5));
                                        vG = Select(none, vg, Select(full, tg, (tg * ta + vg * (31 - ta)) >> 5));
                                        vB = Select(none, vb, Select(full, tb, (tb * ta + vb * (31 - ta)) >> 5));
                                    }
                                    else
                                    {
                                        // modulate
                                        vR = ((tr + 1) * (vr + 1) - 1) >> 6;
                                        vG = ((tg + 1) * (vg + 1) - 1) >> 6;
                                        vB = ((tb + 1) * (vb + 1) - 1) >> 6;
                                    }
                                }
                                vR = (vR << 2) | (vR >> 4);
                                vG = (vG << 2) | (vG >> 4);
                                vB = (vB << 2) | (vB >> 4);
                                for (int i = 0; i < n; i++)
                                {
                                    if (!ok[i]) continue;
                                    const int x = xb + i;
                                    if (alphaV[i] == 31)
                                    {
                                        color[x] = 0xFF000000 | ((u32)vR[i] << 16) | ((u32)vG[i] << 8) | (u32)vB[i];
                                        depth[x] = z[i];
                                        pattr[x] = fogBit;
                                        if (frontFirst) writer[x] = order;
                                        continue;
                                    }
                                    const s32 zl = z[i];
                                    const u32 alpha = (u32)alphaV[i];
                                    u32 r = vR[i], gr = vG[i], bl = vB[i];
                                    // Translucent pixels of one polygon ID are not drawn over each other.
                                    if ((pattr[x] & 0x7F) == polyId) continue;
                                    const u32 dst = color[x];
                                    u32 da = dst >> 24;
                                    if (da && blendEnabled)
                                    {
                                        const u32 f = alpha + 1;
                                        r = (r * f + ((dst >> 16) & 0xFF) * (32 - f)) >> 5;
                                        gr = (gr * f + ((dst >> 8) & 0xFF) * (32 - f)) >> 5;
                                        bl = (bl * f + (dst & 0xFF) * (32 - f)) >> 5;
                                    }
                                    da = std::max(da, (alpha << 3) | (alpha >> 2));
                                    color[x] = (da << 24) | (r << 16) | (gr << 8) | bl;
                                    if (attr & (1 << 11))
                                    {
                                        depth[x] = zl;
                                        if (frontFirst) writer[x] = order;
                                    }
                                    pattr[x] = (u8)(polyId | (pattr[x] & fogBit));
                                }
                            }
                            else
                                for (int i = 0; i < n; i++)
                                {
                                    if (!ok[i]) continue;
                                    const int x = xb + i;
                                    const s32 zl = z[i];
                                    const float pq0 = q0[i], pq1 = q1[i], pq2 = q2[i];
                                    // The alpha test first: it needs only the texel, and a quarter of the
                                    // pixels (the clear parts of billboards) stop there.
                                    u32 r, gr, bl, alpha, texel = 0;
                                    if (textured)
                                    {
                                        const int ts = vts[i], tv = vtv[i];
                                        texel = fetchTexel(ts, tv);
                                        const u32 ta = (texel >> 24) & 0x1F;
                                        alpha = (blendMode & 1) ? polyAlpha : ((ta + 1) * (polyAlpha + 1) - 1) >> 5;
                                    }
                                    else
                                        alpha = polyAlpha;
                                    if (polyAlpha == 0) alpha = 31; // wireframe polygons are drawn filled here
                                    if (alpha <= alphaRef) continue;

                                    int vr = flatR, vg = flatG, vb = flatB;
                                    if (!flat)
                                    {
                                        vr = std::clamp((int)(pq0 * aR + pq1 * bR + pq2 * cR + 0.5f), 0, 63);
                                        vg = std::clamp((int)(pq0 * aG + pq1 * bG + pq2 * cG + 0.5f), 0, 63);
                                        vb = std::clamp((int)(pq0 * aB + pq1 * bB + pq2 * cB + 0.5f), 0, 63);
                                    }
                                    const int rawR = vr;
                                    if (highlight)
                                        vg = vb = vr;
                                    else if (toon)
                                    {
                                        u16 tc = toonTable[vr >> 1];
                                        vr = (tc << 1) & 0x3E; if (vr) vr++;
                                        vg = (tc >> 4) & 0x3E; if (vg) vg++;
                                        vb = (tc >> 9) & 0x3E; if (vb) vb++;
                                    }

                                    if (textured)
                                    {
                                        const u32 tr = texel & 0x3F, tg = (texel >> 8) & 0x3F, tb = (texel >> 16) & 0x3F, ta = (texel >> 24) & 0x1F;
                                        if (blendMode & 1)
                                        {
                                            // decal
                                            if (ta == 0) { r = vr; gr = vg; bl = vb; }
                                            else if (ta == 31) { r = tr; gr = tg; bl = tb; }
                                            else
                                            {
                                                r = ((tr * ta) + (vr * (31 - ta))) >> 5;
                                                gr = ((tg * ta) + (vg * (31 - ta))) >> 5;
                                                bl = ((tb * ta) + (vb * (31 - ta))) >> 5;
                                            }
                                        }
                                        else
                                        {
                                            // modulate
                                            r = ((tr + 1) * (vr + 1) - 1) >> 6;
                                            gr = ((tg + 1) * (vg + 1) - 1) >> 6;
                                            bl = ((tb + 1) * (vb + 1) - 1) >> 6;
                                        }
                                    }
                                    else
                                    {
                                        r = vr;
                                        gr = vg;
                                        bl = vb;
                                    }
                                    if (highlight)
                                    {
                                        u16 tc = toonTable[rawR >> 1];
                                        u32 hr = (tc << 1) & 0x3E; if (hr) hr++;
                                        u32 hg = (tc >> 4) & 0x3E; if (hg) hg++;
                                        u32 hb = (tc >> 9) & 0x3E; if (hb) hb++;
                                        r = std::min<u32>(63, r + hr);
                                        gr = std::min<u32>(63, gr + hg);
                                        bl = std::min<u32>(63, bl + hb);
                                    }

                                    r = (r << 2) | (r >> 4);
                                    gr = (gr << 2) | (gr >> 4);
                                    bl = (bl << 2) | (bl >> 4);

                                    if (alpha == 31)
                                    {
                                        color[x] = 0xFF000000 | (r << 16) | (gr << 8) | bl;
                                        depth[x] = zl;
                                        pattr[x] = fogBit;
                                        if (frontFirst) writer[x] = order;
                                    }
                                    else
                                    {
                                        // Translucent pixels of one polygon ID are not drawn over each other.
                                        if ((pattr[x] & 0x7F) == polyId) continue;
                                        const u32 dst = color[x];
                                        u32 da = dst >> 24;
                                        if (da && blendEnabled)
                                        {
                                            const u32 f = alpha + 1;
                                            r = (r * f + ((dst >> 16) & 0xFF) * (32 - f)) >> 5;
                                            gr = (gr * f + ((dst >> 8) & 0xFF) * (32 - f)) >> 5;
                                            bl = (bl * f + (dst & 0xFF) * (32 - f)) >> 5;
                                        }
                                        da = std::max(da, (alpha << 3) | (alpha >> 2));
                                        color[x] = (da << 24) | (r << 16) | (gr << 8) | bl;
                                        if (attr & (1 << 11))
                                        {
                                            depth[x] = zl;
                                            if (frontFirst) writer[x] = order;
                                        }
                                        pattr[x] = (u8)(polyId | (pattr[x] & fogBit));
                                    }
                                }
                        }
                    }

            };
            using T = std::true_type;
            using F = std::false_type;
            for (const Tri& t : tris)
            {
                if (t.Y1 < rowA || t.Y0 >= rowB) continue;
                const WideVertex& a = t.V[0];
                const WideVertex& b = t.V[1];
                const WideVertex& c = t.V[2];
                const bool flat = a.R == b.R && a.R == c.R && a.G == b.G && a.G == c.G && a.B == b.B && a.B == c.B;
                switch ((t.Tex ? 4 : 0) | (t.WBuffer ? 2 : 0) | (flat ? 1 : 0))
                {
                case 0: raster(t, F(), F(), F()); break;
                case 1: raster(t, F(), F(), T()); break;
                case 2: raster(t, F(), T(), F()); break;
                case 3: raster(t, F(), T(), T()); break;
                case 4: raster(t, T(), F(), F()); break;
                case 5: raster(t, T(), F(), T()); break;
                case 6: raster(t, T(), T(), F()); break;
                case 7: raster(t, T(), T(), T()); break;
                }
            }

            if (fogEnabled)
            {
                for (int y = rowA; y < rowB; y++)
                {
                    u32* color = &Back[(size_t)y * W];
                    const s32* depth = &buffers.Depth[(size_t)(y - rowA) * W];
                    const u8* pattr = &buffers.Attr[(size_t)(y - rowA) * W];
                    for (int x = 0; x < W; x++)
                    {
                        if (!(pattr[x] & 0x80) || !(color[x] >> 24)) continue;
                        u32 z = (u32)depth[x], id, frac;
                        if (z < fogOffset)
                        {
                            id = 0;
                            frac = 0;
                        }
                        else
                        {
                            z -= fogOffset;
                            z = (z >> 2) << fogShift;
                            id = z >> 17;
                            if (id >= 32)
                            {
                                id = 32;
                                frac = 0;
                            }
                            else
                                frac = z & 0x1FFFF;
                        }
                        u32 density = ((fogDensity[id] * (0x20000 - frac)) +
                            (fogDensity[id + 1] * frac)) >> 17;
                        if (density >= 127) density = 128;
                        if (!density || !fogColorOn) continue;
                        const u32 c = color[x];
                        u32 r = (fogR * density + ((c >> 16) & 0xFF) * (128 - density)) >> 7;
                        u32 gr = (fogG * density + ((c >> 8) & 0xFF) * (128 - density)) >> 7;
                        u32 b = (fogB * density + (c & 0xFF) * (128 - density)) >> 7;
                        color[x] = (c & 0xFF000000) | (r << 16) | (gr << 8) | b;
                    }
                }
            }
        };
        static std::function<void(int)> job;
        job = [=](int) {
            for (int i; (i = nextStrip.fetch_add(1)) < strips;) strip(i);
        };
        static const bool prof = getenv("SSR_WIDE_PROF") != nullptr;
        auto t0 = std::chrono::steady_clock::now();
        Pool->Start(job);
        Drawing = true;
        if (prof)
        {
            Wait();
            static double total = 0;
            static u64 frames = 0, triCount = 0;
            total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            triCount += tris.size();
            if (++frames % 200 == 0)
                fprintf(stderr, "[wide] %dx%d: %.2f ms per draw, %llu triangles, %d threads\n", W, H, total / frames,
                    (unsigned long long)(triCount / frames), Pool->Count());
        }
    }
};

void WideWait() { WideAccess::Wait(); }
void WideOnFlush(GPU3D& g) { WideAccess::OnFlush(g); }
void WideOnRender(GPU& gpu, bool showingInterp) { WideAccess::OnRender(gpu, showingInterp); }
void WideReplay(GPU3D& g, bool interpolate, Vertex* vram, Polygon* pram, Polygon** list,
    u32& numPolys, u32& matched)
{
    WideAccess::Replay(g, interpolate, 1, 1, vram, pram, list, numPolys, matched);
}
}
