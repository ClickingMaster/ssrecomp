// ssrecomp: the widescreen view's polygons drawn by the graphics chip. See GPU3D_WideGpu.h.
#include <chrono>
#include "GPU3D_WideGpu.h"

#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>
#include <cstdlib>
#include <cstring>
#include "GPU.h"
#include "GPU3D.h"
#include "SsrThreads.h"

namespace melonDS
{
std::unique_ptr<WideGpuBackend> WideGpuCurrent;
u64 WideGpuTexVersion = 0;
std::atomic<u64> WideGpuBusyNs { 0 };

namespace
{
std::unique_ptr<WideGpuBackend> MakeBackend(std::string& info, bool small)
{
    (void)small;
#ifdef SSR_DEKO3D
    return WideGpuMakeDeko(info, small);
#elif defined(OGLRENDERER_ENABLED)
    return WideGpuMakeGL(info);
#else
    info = "not in this build";
    return nullptr;
#endif
}
}

namespace
{
struct SendThread
{
    std::thread T;
    std::mutex M;
    std::condition_variable Wake, Done;
    std::deque<std::pair<int, std::function<void()>>> Queue;
    int Queued[4] = { 0, 0, 0, 0 }; // per slot: handed over and not yet finished
    bool Quit = false;

    void Loop()
    {
        SsrHelperThreadStart(-2);
        std::unique_lock<std::mutex> lock(M);
        for (;;)
        {
            Wake.wait(lock, [this] { return Quit || !Queue.empty(); });
            if (Queue.empty()) return; // (Quit, with nothing left)
            auto job = std::move(Queue.front());
            Queue.pop_front();
            lock.unlock();
            job.second();
            lock.lock();
            Queued[job.first]--;
            Done.notify_all();
        }
    }
    void Stop()
    {
        if (!T.joinable()) return;
        {
            std::lock_guard<std::mutex> lock(M);
            Quit = true;
        }
        Wake.notify_all();
        T.join();
        Quit = false;
    }
    // (Stopped by WideGpuSendStop as the program ends; joining it from a static object's
    // destructor, after exit, hung the Switch.)
    ~SendThread()
    {
        if (T.joinable()) T.detach();
    }
};
// Slots 0 and 1 (building and sending) on one, 2 and 3 (waiting for the pictures) on the other,
// so that waiting for the graphics chip holds up neither the next picture's sending nor what
// waits for that.
SendThread Sender, Fetcher;
SendThread& ThreadFor(int slot) { return slot >= 2 ? Fetcher : Sender; }
}

bool WideGpuSendOnThread()
{
#ifdef SSR_DEKO3D
    static const bool on = [] {
        const char* v = getenv("SSR_GPU_SEND_THREAD");
        return !(v && v[0] == '0');
    }();
    return on;
#else
    return false;
#endif
}

void WideGpuSend(int slot, std::function<void()> job)
{
    if (!WideGpuSendOnThread())
    {
        job();
        return;
    }
    SendThread& t = ThreadFor(slot);
    {
        std::lock_guard<std::mutex> lock(t.M);
        if (!t.T.joinable()) t.T = std::thread([&t] { t.Loop(); });
        t.Queue.emplace_back(slot, std::move(job));
        t.Queued[slot]++;
    }
    t.Wake.notify_all();
}

void WideGpuSendStop()
{
    Sender.Stop();
    Fetcher.Stop();
}

bool WideGpuSendBusy(int slot)
{
    if (!WideGpuSendOnThread()) return false;
    SendThread& t = ThreadFor(slot);
    std::lock_guard<std::mutex> lock(t.M);
    return t.Queued[slot] != 0;
}

void WideGpuSendJoin(int slot)
{
    if (!WideGpuSendOnThread()) return;
    if (slot < 0)
    {
        WideGpuSendJoin(0);
        WideGpuSendJoin(1);
        if (slot == -2)
        {
            WideGpuSendJoin(2);
            WideGpuSendJoin(3);
        }
        return;
    }
    SendThread& t = ThreadFor(slot);
    std::unique_lock<std::mutex> lock(t.M);
    t.Done.wait(lock, [&t, slot] { return t.Queued[slot] == 0; });
}

WideGpuDSRenderer::WideGpuDSRenderer(std::unique_ptr<WideGpuBackend> backend)
    : Renderer3D(false), Backend(std::move(backend))
{
}

WideGpuDSRenderer::~WideGpuDSRenderer()
{
    WideGpuSendJoin(-2);
    if (Pending) Backend->Fetch(0);
    WideGpuTexVersion = 0;
}

std::unique_ptr<WideGpuDSRenderer> WideGpuMakeDS(std::string& info)
{
    auto b = MakeBackend(info, true);
    if (!b) return nullptr;
    return std::make_unique<WideGpuDSRenderer>(std::move(b));
}

void WideGpuDSRenderer::RenderFrame(GPU& gpu)
{
    // (Both pictures' jobs: the widescreen one reads the texture memory's version and changes
    // that this brings up to date. And this picture's fetching, which writes Lines.)
    WideGpuSendJoin(-1);
    WideGpuSendJoin(3);
    // The flat copies of texture memory brought up to date, as the software renderer does it.
    auto textureDirty = gpu.VRAMDirty_Texture.DeriveState(gpu.VRAMMap_Texture, gpu);
    auto texPalDirty = gpu.VRAMDirty_TexPal.DeriveState(gpu.VRAMMap_TexPal, gpu);
    const bool textureChanged = gpu.MakeVRAMFlat_TextureCoherent(textureDirty);
    const bool texPalChanged = gpu.MakeVRAMFlat_TexPalCoherent(texPalDirty);
    if (textureChanged || texPalChanged || !Version)
    {
        Version++;
        WideGpuNoteChanges(Version, textureDirty.Data, texPalDirty.Data);
    }
    WideGpuTexVersion = Version;

    if (Pending) Backend->Fetch(0); // (an earlier picture nobody asked for)
    Pending = false;
    Pixels = Backend->Target(0, 256, 192);
    Fetched = false;
    // The list itself is copied (the in-between frame's is lent to RenderPolygonRAM for this
    // call only, see GPU3D::VCount215); the polygons, the registers it was drawn with and the
    // flat texture memory stay as they are until the next frame is flushed or rendered, by when
    // the job has been waited for (GetLine, and GPU3D::VBlank).
    List = gpu.GPU3D.RenderPolygonRAM;
    const u32 count = gpu.GPU3D.RenderNumPolygons;
    WideGpuSend(0, [this, &gpu, count] {
        WideGpuBuild(gpu, List.data(), count, 256, 192, Frame, Backend->TakesAtlas());
        Frame.TexVersion = Version;
        Pending = Pixels && Backend->Submit(Frame, 0);
        // (Waited for and converted on the other sending thread: GetLine then only waits for that.)
        if (Pending && WideGpuSendOnThread())
            WideGpuSend(3, [this] {
                Backend->Fetch(0);
                Pending = false;
                Convert();
            });
    });
}

void WideGpuDSRenderer::Convert()
{
    // XRGB (8 bits) with the alpha in the top byte, to the software renderer's format:
    // red, green, blue in 6 bits from bits 0, 8, 16, alpha in 5 from bit 24.
    for (int i = 0; i < 256 * 192; i++)
    {
        const u32 v = Pixels[i];
        Lines[i] = ((v >> 18) & 0x3F) | (((v >> 10) & 0x3F) << 8) | (((v >> 2) & 0x3F) << 16) | (((v >> 27) & 0x1F) << 24);
    }
}

u32* WideGpuDSRenderer::GetLine(int line)
{
    if (!Fetched)
    {
        Fetched = true;
        SsrWaitTimer t(SsrStats().Wait3DNs);
        WideGpuSendJoin(0);
        WideGpuSendJoin(3);
        if (Pending)
        {
            Backend->Fetch(0);
            Pending = false;
            Convert();
        }
    }
    return &Lines[256 * (line < 0 ? 0 : line > 191 ? 191 : line)];
}

bool WideGpuStart(std::string& info)
{
    WideGpuStop();
    WideGpuCurrent = MakeBackend(info, false);
    return WideGpuCurrent != nullptr;
}

void WideReleaseGpu();

void WideGpuStop()
{
    WideGpuSendStop();
    // (Pictures in the renderer's memory, the one on show among them, are copied out first.)
    if (WideGpuCurrent) WideReleaseGpu();
    WideGpuCurrent.reset();
}

namespace
{
// What changed with each of the last versions of the texture memory (WideGpuNoteChanges).
struct ChangeLog
{
    static constexpr int Size = 64;
    u64 Version[Size] {};
    u64 Tex[Size][16] {};
    u64 Pal[Size][4] {};
};
ChangeLog Changes;

// One texture decoded as wide_fsh.glsl's Lookup (and SoftRenderer::TextureLookup) reads it:
// colour (15 bits) | alpha (0-31) << 16 a texel, rows of `width`.
void DecodeTexture(const u8* texMem, const u8* palMem, u32 texParam, u32 texPal, u32* out)
{
    auto tex8 = [&](u32 a) -> u32 { return texMem[a & 0x7FFFF]; };
    auto tex16 = [&](u32 a) -> u32 { a &= 0x7FFFE; return texMem[a] | (texMem[a + 1] << 8); };
    auto pal16 = [&](u32 a) -> u32 { a &= 0x1FFFE; return palMem[a] | (palMem[a + 1] << 8); };
    const u32 vramaddr = (texParam & 0xFFFF) << 3;
    const int width = 8 << ((texParam >> 20) & 7), height = 8 << ((texParam >> 23) & 7);
    const u32 alpha0 = (texParam & (1u << 29)) ? 0 : 31;
    const u32 format = (texParam >> 26) & 7;
    for (int t = 0; t < height; t++)
        for (int s = 0; s < width; s++)
        {
            const u32 pos = (u32)(t * width + s);
            u32 color = 0, alpha = 31;
            switch (format)
            {
            case 1: // A3I5
            {
                const u32 pixel = tex8(vramaddr + pos);
                color = pal16((texPal << 4) + ((pixel & 0x1F) << 1));
                alpha = ((pixel >> 3) & 0x1C) + (pixel >> 6);
                break;
            }
            case 2: // 4 colours
            {
                const u32 pixel = (tex8(vramaddr + (pos >> 2)) >> ((s & 3) << 1)) & 3;
                color = pal16((texPal << 3) + (pixel << 1));
                alpha = pixel == 0 ? alpha0 : 31;
                break;
            }
            case 3: // 16 colours
            {
                u32 pixel = tex8(vramaddr + (pos >> 1));
                pixel = (s & 1) ? pixel >> 4 : pixel & 0xF;
                color = pal16((texPal << 4) + (pixel << 1));
                alpha = pixel == 0 ? alpha0 : 31;
                break;
            }
            case 4: // 256 colours
            {
                const u32 pixel = tex8(vramaddr + pos);
                color = pal16((texPal << 4) + (pixel << 1));
                alpha = pixel == 0 ? alpha0 : 31;
                break;
            }
            case 5: // compressed
            {
                u32 addr = vramaddr + (u32)(t & 0x3FC) * (u32)(width >> 2) + (u32)(s & 0x3FC) + (u32)(t & 3);
                addr &= 0x7FFFF;
                u32 slot1addr = 0x20000 + ((addr & 0x1FFFC) >> 1);
                if (addr >= 0x40000) slot1addr += 0x10000;
                const u32 val = (addr >= 0x20000 && addr < 0x40000) ? 0 : (tex8(addr) >> (2 * (s & 3)));
                const u32 palinfo = tex16(slot1addr);
                const u32 paloffset = (palinfo & 0x3FFF) << 2;
                const u32 base = texPal << 4;
                const u32 mode = palinfo >> 14;
                const u32 c0 = pal16(base + paloffset), c1 = pal16(base + paloffset + 2);
                switch (val & 3)
                {
                case 0: color = c0; break;
                case 1: color = c1; break;
                case 2:
                    if (mode == 1)
                        color = (((c0 & 0x1F) + (c1 & 0x1F)) >> 1) | ((((c0 & 0x3E0) + (c1 & 0x3E0)) >> 1) & 0x3E0) |
                            ((((c0 & 0x7C00) + (c1 & 0x7C00)) >> 1) & 0x7C00);
                    else if (mode == 3)
                        color = (((c0 & 0x1F) * 5 + (c1 & 0x1F) * 3) >> 3) | ((((c0 & 0x3E0) * 5 + (c1 & 0x3E0) * 3) >> 3) & 0x3E0) |
                            ((((c0 & 0x7C00) * 5 + (c1 & 0x7C00) * 3) >> 3) & 0x7C00);
                    else
                        color = pal16(base + paloffset + 4);
                    break;
                default:
                    if (mode == 2)
                        color = pal16(base + paloffset + 6);
                    else if (mode == 3)
                        color = (((c0 & 0x1F) * 3 + (c1 & 0x1F) * 5) >> 3) | ((((c0 & 0x3E0) * 3 + (c1 & 0x3E0) * 5) >> 3) & 0x3E0) |
                            ((((c0 & 0x7C00) * 3 + (c1 & 0x7C00) * 5) >> 3) & 0x7C00);
                    else
                    {
                        color = 0;
                        alpha = 0;
                    }
                    break;
                }
                break;
            }
            case 6: // A5I3
            {
                const u32 pixel = tex8(vramaddr + pos);
                color = pal16((texPal << 4) + ((pixel & 7) << 1));
                alpha = pixel >> 3;
                break;
            }
            case 7: // direct colour
                color = tex16(vramaddr + (pos << 1));
                alpha = (color & 0x8000) ? 31 : 0;
                break;
            default:
                break;
            }
            out[pos] = (color & 0x7FFF) | (alpha << 16);
        }
}

// The memory a texture is decoded from: its texels, and its palette entries (all of the palette
// memory for a compressed texture, whose palette offsets come from its texels; and its texels'
// second part, in texture slot 1, too).
void SourceRanges(u32 texParam, u32 texPal, u32& tex0, u32& tex1, u32& pal0, u32& pal1)
{
    const u32 vramaddr = (texParam & 0xFFFF) << 3;
    const u32 texels = (8u << ((texParam >> 20) & 7)) * (8u << ((texParam >> 23) & 7));
    switch ((texParam >> 26) & 7)
    {
    case 1: tex0 = vramaddr; tex1 = vramaddr + texels; pal0 = texPal << 4; pal1 = pal0 + 64; break;
    case 2: tex0 = vramaddr; tex1 = vramaddr + texels / 4; pal0 = texPal << 3; pal1 = pal0 + 8; break;
    case 3: tex0 = vramaddr; tex1 = vramaddr + texels / 2; pal0 = texPal << 4; pal1 = pal0 + 32; break;
    case 4: tex0 = vramaddr; tex1 = vramaddr + texels; pal0 = texPal << 4; pal1 = pal0 + 512; break;
    case 6: tex0 = vramaddr; tex1 = vramaddr + texels; pal0 = texPal << 4; pal1 = pal0 + 16; break;
    case 7: tex0 = vramaddr; tex1 = vramaddr + texels * 2; pal0 = pal1 = 0; break;
    default: tex0 = 0; tex1 = 512 * 1024; pal0 = 0; pal1 = 128 * 1024; break; // compressed: everything
    }
}

// Whether any 512-byte block of [from, to) (masked to the memory's size) is set in `blocks`.
bool AnyChanged(const u64* blocks, u32 words, u32 from, u32 to)
{
    if (to <= from) return false;
    const u32 mask = words * 64 * 512 - 1;
    if (to - from > mask) return true;
    for (u32 a = from & ~511u; a < to; a += 512)
    {
        const u32 b = (a & mask) / 512;
        if (blocks[b >> 6] & (1ull << (b & 63))) return true;
    }
    return false;
}

// Brings the atlas up to date with the texture memory's version: textures whose memory changed
// since it was last seen are decoded again when next used (everything, if that is too long ago).
void SeeVersion(WideGpuAtlas& a, u64 version)
{
    if (a.SeenVersion == version) return;
    bool all = a.SeenVersion == 0 || version < a.SeenVersion || version - a.SeenVersion >= (u64)ChangeLog::Size;
    u64 tex[16] {}, pal[4] {};
    for (u64 v = a.SeenVersion + 1; !all && v <= version; v++)
    {
        const int i = (int)(v % ChangeLog::Size);
        if (Changes.Version[i] != v) { all = true; break; }
        for (int w = 0; w < 16; w++) tex[w] |= Changes.Tex[i][w];
        for (int w = 0; w < 4; w++) pal[w] |= Changes.Pal[i][w];
    }
    for (auto& [key, e] : a.Entries)
        if (all || AnyChanged(tex, 16, e.Tex0, e.Tex1) || AnyChanged(pal, 4, e.Pal0, e.Pal1)) e.Valid = false;
    a.SeenVersion = version;
}
}

void WideGpuNoteChanges(u64 version, const u64* tex, const u64* pal)
{
    const int i = (int)(version % ChangeLog::Size);
    Changes.Version[i] = version;
    memcpy(Changes.Tex[i], tex, sizeof(Changes.Tex[i]));
    memcpy(Changes.Pal[i], pal, sizeof(Changes.Pal[i]));
}

void WideGpuBuild(const GPU& gpu, Polygon* const* list, u32 count, int W, int H, WideGpuFrame& f, bool atlas)
{
    const GPU3D& g = gpu.GPU3D;
    f.W = W;
    f.H = H;
    f.Vertices.clear();
    f.Polygons.clear();
    f.Draws.clear();

    WideGpuFrameConstants& k = f.Constants;
    const u32 dispCnt = g.RenderDispCnt;
    k.Size[0] = (u32)W;
    k.Size[1] = (u32)H;
    k.Size[2] = dispCnt;
    k.Size[3] = g.RenderAlphaRef;
    k.Scale[0] = 2.0f / W;
    k.Scale[1] = 2.0f / H;
    k.Scale[2] = 1.0f;
    k.Scale[3] = 0.0f;
    k.Fog[0] = g.RenderFogColor;
    k.Fog[1] = g.RenderFogOffset;
    k.Fog[2] = g.RenderFogShift;
    k.Fog[3] = ((dispCnt >> 7) & 1) | ((dispCnt & (1 << 6)) ? 0 : 2);
    for (int i = 0; i < 32; i++) k.Toon[i] = g.RenderToonTable[i];
    for (int i = 0; i < 36; i++) k.FogDensity[i] = i < 34 ? g.RenderFogDensityTable[i] : 0;

    // The rear plane, as GPU3D_Wide.cpp clears to it.
    const u32 a1 = g.RenderClearAttr1;
    const u32 clearAlpha = (a1 >> 16) & 0x1F;
    if (clearAlpha)
    {
        const u32 r = (a1 << 3) & 0xF8, gr = (a1 >> 2) & 0xF8, b = (a1 >> 7) & 0xF8;
        f.ClearColor[0] = (float)(r | (r >> 5)) / 255.0f;
        f.ClearColor[1] = (float)(gr | (gr >> 5)) / 255.0f;
        f.ClearColor[2] = (float)(b | (b >> 5)) / 255.0f;
        f.ClearColor[3] = 1.0f;
    }
    else
        f.ClearColor[0] = f.ClearColor[1] = f.ClearColor[2] = f.ClearColor[3] = 0.0f;
    f.ClearDepth = ((g.RenderClearAttr2 & 0x7FFF) * 0x200) + 0x1FF;
    // (W-buffering is chosen for the whole frame, when it is flushed.)
    f.WDepth = false;
    for (u32 i = 0; i < count; i++)
        if (list[i])
        {
            f.WDepth = list[i]->WBuffer;
            break;
        }
    f.ClearDepthValue = f.WDepth ? 1.0f / (float)std::max<u32>(f.ClearDepth, 1) : (float)f.ClearDepth / 16777216.0f;
    f.ClearFog = (a1 >> 15) & 1;
    f.ClearStencil = clearAlpha ? 0x80 : 0;
    f.TexMem = gpu.VRAMFlat_Texture;
    f.PalMem = gpu.VRAMFlat_TexPal;
    f.TexVersion = WideGpuTexVersion;

    const bool texturing = dispCnt & 1;
    const bool blendEnabled = dispCnt & (1 << 3);
    const float sx = W / (256.0f * 16), sy = H / (192.0f * 16);

    // Decoded textures (see WideGpuAtlas): only with the texture memory's versions to tell what
    // changed (the DS-resolution picture on the graphics chip keeps them).
    static const bool stats = getenv("SSR_WIDE_GPU_STATS") != nullptr;
    WideGpuAtlas& at = f.Atlas;
    f.AtlasOn = atlas && f.TexVersion != 0;
    u64 decoded = 0, decodeNs = 0;
    at.Uploads.clear();
    if (f.AtlasOn)
    {
        if (at.Texels.empty()) at.Texels.resize(WideGpuAtlas::Capacity);
        if (at.Full)
        {
            // (Filled up last frame: started again, so that what is used fits.)
            at.Entries.clear();
            at.Used = 0;
            at.Full = false;
        }
        SeeVersion(at, f.TexVersion);
    }
    // The atlas's place for a polygon's texture, decoded now if needed; ~0u if it has no room.
    auto atlasOffset = [&](u32 texParam, u32 texPal) -> u32 {
        const u32 format = (texParam >> 26) & 7;
        const u64 key = (u64)(texParam & 0x3FF0FFFF) | ((u64)(format == 7 ? 0 : texPal) << 32);
        auto it = at.Entries.find(key);
        if (it != at.Entries.end() && it->second.Valid) return it->second.Offset;
        const u32 size = (8u << ((texParam >> 20) & 7)) * (8u << ((texParam >> 23) & 7));
        WideGpuAtlas::Entry* e;
        if (it != at.Entries.end())
            e = &it->second; // decoded again in its own place
        else
        {
            if (at.Used + size > WideGpuAtlas::Capacity)
            {
                at.Full = true;
                return ~0u;
            }
            e = &at.Entries[key];
            e->Offset = at.Used;
            e->Size = size;
            at.Used += size;
        }
        SourceRanges(texParam, texPal, e->Tex0, e->Tex1, e->Pal0, e->Pal1);
        const auto t0 = stats ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point();
        DecodeTexture(gpu.VRAMFlat_Texture, gpu.VRAMFlat_TexPal, texParam, texPal, at.Texels.data() + e->Offset);
        if (stats) decodeNs += (u64)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
        decoded += e->Size;
        e->Valid = true;
        at.Uploads.push_back({ e->Offset, e->Size });
        return e->Offset;
    };

    // A polygon's triangles (a fan), appended to the vertices.
    auto add = [&](const Polygon* poly) {
        const u32 index = (u32)f.Polygons.size();
        WideGpuPolygon p;
        p.Attr = poly->Attr;
        p.TexParam = poly->TexParam;
        p.TexPalette = poly->TexPalette;
        p.Flags = (poly->WBuffer ? 1 : 0) | (texturing && ((poly->TexParam >> 26) & 0x7) ? 2 : 0);
        if (f.AtlasOn && (p.Flags & 2))
        {
            const u32 offset = atlasOffset(poly->TexParam, poly->TexPalette);
            if (offset != ~0u)
            {
                p.TexPalette = offset;
                p.Flags |= 4;
            }
        }
        f.Polygons.push_back(p);
        WideGpuVertex v[10];
        for (u32 i = 0; i < poly->NumVertices; i++)
        {
            const Vertex* vtx = poly->Vertices[i];
            WideGpuVertex& o = v[i];
            o.X = vtx->HiresPosition[0] * sx;
            o.Y = vtx->HiresPosition[1] * sy;
            o.W = poly->FinalW[i] > 0 ? (float)poly->FinalW[i] : 1.0f;
            o.Z = (float)poly->FinalZ[i];
            o.R = (float)(vtx->FinalColor[0] >> 3);
            o.G = (float)(vtx->FinalColor[1] >> 3);
            o.B = (float)(vtx->FinalColor[2] >> 3);
            o.S = (float)vtx->TexCoords[0];
            o.T = (float)vtx->TexCoords[1];
            o.Poly = index;
            o.Pad0 = o.Pad1 = 0;
        }
        for (u32 i = 1; i + 1 < poly->NumVertices; i++)
        {
            f.Vertices.push_back(v[0]);
            f.Vertices.push_back(v[i]);
            f.Vertices.push_back(v[i + 1]);
        }
    };
    auto drawable = [](const Polygon* poly) {
        return poly && !poly->Degenerate && !poly->IsShadowMask && !poly->IsShadow && poly->Type == 0 && poly->NumVertices >= 3;
    };
    auto frontFirst = [](const Polygon* poly) { return !poly->Translucent && !(poly->Attr & (1 << 14)); };

    // Whether the shader can discard any of a polygon's pixels (drawn with all its pixels): not
    // when it is opaque (alpha 31, or 0, wireframe, drawn as 31) and so is every texel: no
    // texture, or one of 4, 16 or 256 colours whose colour 0 is not transparent. (Alpha 31 then
    // passes any alpha test but one with reference 31.)
    auto neverDiscards = [&](const Polygon* poly) {
        const u32 polyAlpha = (poly->Attr >> 16) & 31;
        if ((polyAlpha != 31 && polyAlpha != 0) || g.RenderAlphaRef >= 31) return false;
        const u32 format = texturing ? (poly->TexParam >> 26) & 7 : 0;
        return format == 0 || ((format == 2 || format == 3 || format == 4) && !(poly->TexParam & (1u << 29)));
    };

    // The opaque polygons, in the game's order, in as few draws as runs of them alike: those
    // whose pixels are never discarded tested early (see WideGpuDraw::Early).
    {
        bool runEarly = false;
        u32 runFirst = 0;
        auto closeRun = [&]() {
            const u32 n = (u32)f.Vertices.size() - runFirst;
            if (!n) return;
            WideGpuDraw d {};
            d.First = runFirst;
            d.Count = n;
            d.Pixels = 0;
            d.DepthWrite = true;
            d.StencilTest = WideGpuStencil::Always;
            d.StencilRef = 0x80;
            d.StencilMask = 0xFF;
            d.WriteDepthAttr = d.WriteFogAttr = true;
            d.Early = runEarly;
            f.Draws.push_back(d);
            runFirst = (u32)f.Vertices.size();
        };
        for (u32 i = 0; i < count; i++)
            if (drawable(list[i]) && frontFirst(list[i]))
            {
                const bool early = neverDiscards(list[i]);
                if (early != runEarly)
                {
                    closeRun();
                    runEarly = early;
                }
                add(list[i]);
            }
        closeRun();
    }
    const size_t opaqueDraws = f.Draws.size();

    // The others, in order. Consecutive polygons that are drawn alike share their draws: all
    // opaque ones (decals), or all translucent ones of one polygon ID; a polygon with pixels
    // of both kinds is drawn on its own.
    struct Kind
    {
        bool Opaque, Translucent, Lequal, WriteDepth, Fog;
        s32 Bias;
        u8 Id;
        bool operator==(const Kind& o) const
        {
            return Opaque == o.Opaque && Translucent == o.Translucent && Lequal == o.Lequal && WriteDepth == o.WriteDepth &&
                Fog == o.Fog && Bias == o.Bias && Id == o.Id;
        }
    };
    Kind group {};
    u32 groupFirst = 0;
    bool open = false;
    auto close = [&]() {
        if (!open) return;
        open = false;
        const u32 n = (u32)f.Vertices.size() - groupFirst;
        if (!n) return;
        WideGpuDraw d {};
        d.First = groupFirst;
        d.Count = n;
        d.DepthLequal = group.Lequal;
        d.DepthBias = group.Bias;
        if (group.Opaque)
        {
            d.Pixels = group.Translucent ? 1 : 0;
            d.DepthWrite = true;
            d.StencilTest = WideGpuStencil::Always;
            d.StencilRef = 0x80;
            d.StencilMask = 0xFF;
            d.WriteDepthAttr = d.WriteFogAttr = true;
            f.Draws.push_back(d);
        }
        if (group.Translucent)
        {
            d.Pixels = 2;
            // (SSR_WIDEGPU_EARLYTRANS=1: translucent polygons that do not write depth tested early
            // too, before shading. In Castelia 1.2 million pixels fewer and 1.5 ms less a 720-line
            // picture, the same picture; but not exact in general: a pixel they discard, a
            // transparent texel, still marks the stencil with their polygon ID, which keeps a
            // later translucent polygon of the same ID from blending there. A battle's grass
            // tufts at the platform's edge came out wrong, so it is off.)
            static const bool earlyTrans = getenv("SSR_WIDEGPU_EARLYTRANS") && !strcmp(getenv("SSR_WIDEGPU_EARLYTRANS"), "1");
            d.Early = earlyTrans && !group.WriteDepth;
            d.DepthWrite = group.WriteDepth;
            d.WriteDepthAttr = group.WriteDepth;
            // A translucent pixel keeps the fog only if the polygon has it too (GPU3D_Wide.cpp's
            // pattr & fogBit): one without it writes its 0. SoulSilver's dark caves rely on this,
            // their darkness polygons taking the black fog off the lit circle around the player.
            d.WriteFogAttr = !group.Fog;
            d.StencilRef = (u8)(0x80 | group.Id);
            if (blendEnabled)
            {
                // Where the picture has no alpha yet: drawn as they are. (Nowhere, when the rear
                // plane has alpha: every pixel's stencil has 0x80 from the start. The pass is then
                // left out, rather than having the graphics chip shade all its pixels, which it
                // does before testing them, for nothing.)
                d.StencilTest = WideGpuStencil::NotEqual;
                d.StencilMask = 0x80;
                d.Blend = false;
                if (!(f.ClearStencil & 0x80)) f.Draws.push_back(d);
                // Elsewhere, blended, except over their own ID.
                d.StencilMask = 0xFF;
                d.Blend = true;
                f.Draws.push_back(d);
            }
            else
            {
                d.StencilTest = WideGpuStencil::NotEqual;
                d.StencilMask = 0xFF;
                d.Blend = false;
                f.Draws.push_back(d);
            }
        }
    };
    for (u32 i = 0; i < count; i++)
    {
        const Polygon* poly = list[i];
        if (!drawable(poly) || frontFirst(poly)) continue;
        const u32 attr = poly->Attr;
        const u32 polyAlpha = (attr >> 16) & 0x1F;
        Kind kind;
        kind.Translucent = poly->Translucent && polyAlpha != 0;
        // (Alpha 31 is reached by translucent polygons only through their texture's alpha.)
        kind.Opaque = !poly->Translucent || polyAlpha == 31 || polyAlpha == 0;
        kind.Lequal = attr & (1 << 14);
        kind.Bias = kind.Lequal ? (poly->WBuffer ? 0xFF : 0x200) : 0;
        kind.WriteDepth = attr & (1 << 11);
        kind.Fog = attr & (1 << 15);
        kind.Id = kind.Translucent ? (u8)(((attr >> 24) & 0x3F) + 1) : 0;
        if (!kind.Translucent) kind.WriteDepth = true;
        const bool mixed = kind.Opaque && kind.Translucent;
        if (!open || mixed || !(kind == group))
        {
            close();
            group = kind;
            groupFirst = (u32)f.Vertices.size();
            open = true;
        }
        add(poly);
        if (mixed) close();
    }
    close();
    // Development aid: how many polygons use W-buffering (SSR_WIDE_GPU_STATS=1).
    if (stats && f.AtlasOn)
    {
        // And what keeping decoded textures costs the processor.
        static u64 pictures = 0, texels = 0, ns = 0, worstNs = 0;
        pictures++;
        texels += decoded;
        ns += decodeNs;
        worstNs = std::max(worstNs, decodeNs);
        if (pictures % 240 == 0)
        {
            fprintf(stderr, "[widegpu] atlas: %zu textures, %u of %u texels used, %.0f texels decoded a picture, "
                "%.3f ms a picture (worst %.3f)\n", at.Entries.size(), at.Used, WideGpuAtlas::Capacity,
                (double)texels / 240, ns / 240 / 1e6, worstNs / 1e6);
            texels = ns = worstNs = 0;
        }
    }
    if (stats && W > 256)
    {
        // And how many pixels each kind of draw covers (as the graphics chip shades them all
        // when its shader may discard): triangles clipped to the picture.
        auto area = [&](const WideGpuVertex* t) {
            float px[16], py[16], qx[16], qy[16];
            int n = 3;
            for (int i = 0; i < 3; i++) { px[i] = t[i].X; py[i] = t[i].Y; }
            for (int edge = 0; edge < 4; edge++)
            {
                int m = 0;
                auto inside = [&](float x, float y) {
                    return edge == 0 ? x >= 0 : edge == 1 ? x <= W : edge == 2 ? y >= 0 : y <= H;
                };
                for (int i = 0; i < n; i++)
                {
                    const int j = (i + 1) % n;
                    const bool a = inside(px[i], py[i]), b = inside(px[j], py[j]);
                    if (a) { qx[m] = px[i]; qy[m] = py[i]; m++; }
                    if (a != b)
                    {
                        const float bound = edge == 0 ? 0 : edge == 1 ? W : edge == 2 ? 0 : H;
                        const float tt = edge < 2 ? (bound - px[i]) / (px[j] - px[i]) : (bound - py[i]) / (py[j] - py[i]);
                        qx[m] = px[i] + (px[j] - px[i]) * tt;
                        qy[m] = py[i] + (py[j] - py[i]) * tt;
                        m++;
                    }
                }
                n = m;
                std::copy(qx, qx + m, px);
                std::copy(qy, qy + m, py);
            }
            double sum = 0;
            for (int i = 0; i < n; i++) sum += (double)px[i] * py[(i + 1) % n] - (double)px[(i + 1) % n] * py[i];
            return std::abs(sum) / 2;
        };
        static double cover[5] = {}; // opaque that cannot discard, opaque that may, other opaque, translucent, frames
        for (size_t di = 0; di < f.Draws.size(); di++)
        {
            const WideGpuDraw& d = f.Draws[di];
            for (u32 v = d.First; v + 2 < d.First + d.Count + 0; v += 3)
            {
                const WideGpuPolygon& p = f.Polygons[f.Vertices[v].Poly];
                const double a = area(&f.Vertices[v]);
                int kind;
                (void)p;
                if (d.Early) kind = 0;
                else if (di < opaqueDraws) kind = 1;
                else kind = d.Pixels == 2 ? 3 : 2;
                cover[kind] += a;
            }
        }
        if (++cover[4] >= 120)
        {
            const double n = cover[4] * 1e6;
            fprintf(stderr, "[widegpu] million pixels a picture: opaque without discards %.2f, opaque that may discard %.2f, "
                "other opaque %.2f, translucent passes %.2f (rear plane alpha %u, blending %d)\n", cover[0] / n, cover[1] / n, cover[2] / n, cover[3] / n,
                clearAlpha, blendEnabled ? 1 : 0);
            std::fill(cover, cover + 5, 0.0);
        }
        static u64 frames = 0, polys = 0, wpolys = 0;
        frames++;
        for (const WideGpuPolygon& p : f.Polygons)
        {
            polys++;
            if (p.Flags & 1) wpolys++;
        }
        if (frames % 120 == 0) fprintf(stderr, "[widegpu] polygons %llu, W-buffered %llu\n", (unsigned long long)polys, (unsigned long long)wpolys);
    }
}
}
