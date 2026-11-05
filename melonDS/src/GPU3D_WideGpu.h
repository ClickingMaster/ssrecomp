// ssrecomp: the widescreen view's polygons drawn by the graphics chip instead of
// GPU3D_Wide.cpp's own rasteriser, into the same picture (XRGB, the top byte the alpha, 0
// where nothing was drawn), so that everything that uses it stays as it is.
//
// What is drawn, and how, is worked out here the same way for every graphics API (the
// shaders in wide_gpu/ are the same for both too); a backend only carries it out:
// GPU3D_WideGpu_Deko.cpp on the Switch (deko3D, its own API: about 3 ms a 1280x720 picture,
// where Mesa's OpenGL took 47), GPU3D_WideGpu_GL.cpp on a PC, for testing it against the
// software rasteriser (SSR_WIDE_GPU=1 with --headless).
//
// The picture follows GPU3D_Wide.cpp's rules, with these differences:
// - Pixels are covered where the graphics chip covers them (no widening of the edges by a
//   third of a pixel), and colours are blended with rounding instead of truncation (at most
//   one step of 255 apart).
// - Polygons that pass the depth test on equal depth (decals) pass on any depth up to the
//   tolerance in front of what is there, not only within it (as melonDS's own OpenGL
//   renderer does); the depth they store is the lower end of their tolerance.
//
// How it is drawn: the rear plane cleared; the opaque polygons (not translucent, not tested
// on equal depth) in one draw in the game's order with a "less" depth test, which gives what
// the software's near-to-far order with its tie rule gives; then the others in the game's
// order. Their opaque pixels (alpha 31) are drawn as opaque ones; their translucent pixels
// first where the picture has no alpha yet (stencil 0: drawn as they are, as the DS does)
// and then elsewhere, blended, except over pixels of their own translucent polygon ID (the
// stencil holds 0x80 where the picture has alpha, plus the ID + 1 of the translucent
// polygon drawn there last). A final pass applies the fog and writes the picture.
#pragma once
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include "types.h"
#include "GPU3D.h"

namespace melonDS
{
class GPU;
struct Polygon;

struct WideGpuVertex
{
    float X, Y, W, Z;   // output pixels; W for perspective; depth as the DS stores it
    float R, G, B, S;   // colour (6 bits a channel); texture coordinates in 1/16 texel
    float T;
    u32 Poly;           // index into the polygon table
    u32 Pad0, Pad1;
};
static_assert(sizeof(WideGpuVertex) == 48, "matches wide_vsh.glsl");

struct WideGpuPolygon
{
    // Flags: bit 0 W-buffer, bit 1 textured, bit 2 its texture is decoded in the atlas
    // (WideGpuAtlas), at the texel TexPalette holds then.
    u32 Attr, TexParam, TexPalette, Flags;
};

// Textures decoded once (colour, 15 bits, and alpha, 5 bits from bit 16, a texel), for the
// graphics chip to read with one fetch a pixel instead of decoding the DS's formats every time
// (that was over half its time). Kept from frame to frame; a texture is decoded again when the
// texture or palette memory it comes from has changed (the DS-resolution renderer's record of
// what changed, WideGpuNoteChanges). Each renderer has its own, as each has its own memory on
// the graphics chip.
struct WideGpuAtlas
{
    static constexpr u32 Capacity = 2 * 1024 * 1024; // texels (8 MB)
    struct Entry
    {
        u32 Offset, Size;          // texels
        u32 Tex0, Tex1, Pal0, Pal1; // the bytes it was decoded from (texture, palette memory)
        bool Valid;
    };
    std::vector<u32> Texels;
    std::unordered_map<u64, Entry> Entries; // by texture parameters and palette
    u32 Used = 0;
    u64 SeenVersion = 0;
    bool Full = false;
    std::vector<std::pair<u32, u32>> Uploads; // (first texel, count) decoded this frame
};

// The shaders' uniform buffer 0 (std140).
struct WideGpuFrameConstants
{
    u32 Size[4];          // width, height, DISP3DCNT, alpha test reference
    float Scale[4];       // 2 / width, 2 / height, direction of y, depth range (both the backend's)
    u32 Fog[4];           // fog colour, offset, shift, flags (bit 0 fog on, bit 1 fog colours,
                          // bit 2 no textures: for measuring, see GPU3D_WideGpu_Deko.cpp)
    u32 Toon[32];
    u32 FogDensity[36];
};

enum class WideGpuStencil : u8 { Always, Equal, NotEqual };

struct WideGpuDraw
{
    u32 First, Count;     // vertices (triangles of three)
    u8 Pixels;            // 0 all, 1 opaque (alpha 31) only, 2 translucent only
    bool DepthLequal;     // "less or equal" (equal-depth polygons, with DepthBias), else "less"
    bool DepthWrite;
    WideGpuStencil StencilTest;
    u8 StencilRef;        // compared under StencilMask; written whole where drawn
    u8 StencilMask;
    bool Blend;
    bool WriteDepthAttr, WriteFogAttr; // the second target's two channels
    s32 DepthBias;        // subtracted from the depth tested and stored
    // Depth and stencil tested (and written) before the pixel is shaded, not after: the graphics
    // chip then shades only what is in front. For polygons whose pixels the shader never
    // discards (opaque ones with opaque textures), where that changes nothing; the shader
    // with a discard in it would otherwise have its pixels tested after shading.
    bool Early = false;
};

struct WideGpuFrame
{
    int W = 0, H = 0;
    WideGpuFrameConstants Constants {};
    std::vector<WideGpuVertex> Vertices;
    std::vector<WideGpuPolygon> Polygons;
    std::vector<WideGpuDraw> Draws;
    float ClearColor[4] {}; // red, green, blue (0-1), alpha (0 or 1)
    u32 ClearDepth = 0;     // as the DS stores it (for the fog)
    // Depth as the graphics chip tests it (see wide_vsh.glsl): Z / 2^24, tested "less"; with
    // W-buffering (the DS stores W) 1 / W, tested "greater". The rear plane's:
    bool WDepth = false;
    float ClearDepthValue = 1.0f;
    u32 ClearFog = 0;
    u8 ClearStencil = 0;
    const u8* TexMem = nullptr; // 512 KB, read during Submit
    const u8* PalMem = nullptr; // 128 KB
    u64 TexVersion = 0;         // the same (not 0) as last time: the texture memory has not changed
    bool AtlasOn = false;       // textures from the atlas (when the backend takes it, see Build)
    WideGpuAtlas Atlas;
};

class WideGpuBackend
{
public:
    virtual ~WideGpuBackend() = default;
    virtual const char* Name() const = 0;
    // Memory for the pictures (W x H, XRGB), one for each of three slots that take turns
    // (GPU3D_Wide.cpp's buffers), which the graphics chip writes the picture into, so that it
    // need not be copied; the processor reads it after Fetch. nullptr if there is none.
    virtual u32* Target(int slot, int w, int h) = 0;
    // Starts drawing the frame into the slot (the backend's direction of y is put in
    // Constants.Scale[2]); everything in it is copied before this returns.
    virtual bool Submit(WideGpuFrame& frame, int slot) = 0;
    // Waits for the picture of the last Submit; Target(slot)'s memory then holds it.
    virtual void Fetch(int slot) = 0;
    // Whether it reads textures from WideGpuFrame::Atlas (its shader can).
    virtual bool TakesAtlas() const { return false; }
    // Whether the picture of the last Submit is finished (Fetch would not wait).
    virtual bool Done(int /*slot*/) { return true; }
};

// Works out what to draw for the polygons of a wide frame (as GPU3D_Wide.cpp's Draw sees
// them), at w x h.
void WideGpuBuild(const GPU& gpu, Polygon* const* list, u32 count, int w, int h, WideGpuFrame& out, bool atlas = false);

// The DS-resolution renderer's record of the texture and palette memory that changed with each
// new version of it (WideGpuTexVersion): 512-byte blocks.
void WideGpuNoteChanges(u64 version, const u64* tex /* 16 words */, const u64* pal /* 4 words */);

// The DS-resolution 3D picture (the one melonDS's 2D renderer builds the screens from) drawn by
// the graphics chip too, in place of the software renderer, with the same rules as the
// widescreen view (no shadows, edge marking or antialiasing, as in that view): with the
// widescreen view on, the world shown is that view's, and this picture is seen only through
// translucent 2D layers over it and in scenes without the widescreen view. It is drawn when
// the frame is rendered (scanline 215) and fetched for the first line the 2D renderer needs,
// a few milliseconds later: the graphics chip takes well under one.
// Where the pictures' lists are worked out and sent to the graphics chip (WideGpuBuild and the
// backend's Submit): with deko3D (the Switch), on a thread of their own, so that the emulation
// goes on meanwhile (2 ms of every frame the 3D was drawn in White 2's Aspertia City); elsewhere,
// and with SSR_GPU_SEND_THREAD=0, at once. Slot 0 is the DS-resolution picture, 1 the widescreen
// one; on a second thread, 2 waits for and fetches the widescreen one, 3 the DS-resolution one.
// Jobs of a thread run in the order they were handed over. WideGpuSendJoin(slot) waits for the
// jobs of that slot (-1: slots 0 and 1, -2: all) to have finished; WideGpuSendBusy(slot) tells
// whether any has not.
bool WideGpuSendOnThread();
void WideGpuSend(int slot, std::function<void()> job);
void WideGpuSendJoin(int slot);
bool WideGpuSendBusy(int slot);
void WideGpuSendStop(); // (waits for the jobs, then ends the thread; started again when needed)

class WideGpuDSRenderer : public Renderer3D
{
public:
    explicit WideGpuDSRenderer(std::unique_ptr<WideGpuBackend> backend);
    ~WideGpuDSRenderer() override;
    void Reset(GPU& gpu) override {}
    void RenderFrame(GPU& gpu) override;
    u32* GetLine(int line) override;

private:
    void Convert(); // Pixels to Lines
    std::unique_ptr<WideGpuBackend> Backend;
    WideGpuFrame Frame;
    u32* Pixels = nullptr;
    bool Pending = false, Fetched = true;
    u64 Version = 0;
    std::array<Polygon*, 2048> List {}; // (the frame's list as it was handed over)
    u32 Lines[256 * 192] {}; // in the format GetLine returns
};
std::unique_ptr<WideGpuDSRenderer> WideGpuMakeDS(std::string& info);

// The texture memory's version as the DS-resolution renderer above last saw it: changes
// whenever the memory may have changed, 0 when that renderer is not in use (the widescreen
// view's renderer then copies the texture memory every frame).
extern u64 WideGpuTexVersion;

// The graphics chip's time on the widescreen view's pictures (from its own clock: starting on
// one to having copied it out), added up as they are fetched, in nanoseconds. The frontend
// lowers the widescreen view's detail when this takes more of each frame than there is room for
// (see LOWER DETAIL WHEN BUSY in main.cpp).
extern std::atomic<u64> WideGpuBusyNs;

// The backend in use, if any (the frontend sets it up; see WideGpuStart).
extern std::unique_ptr<WideGpuBackend> WideGpuCurrent;
// Sets up the best backend this build has; false (and why, in info) if none could be.
bool WideGpuStart(std::string& info);
void WideGpuStop();

#ifdef SSR_DEKO3D
// small: for the DS-resolution picture, wanted within the frame (its queue gets the graphics
// chip's attention first).
std::unique_ptr<WideGpuBackend> WideGpuMakeDeko(std::string& info, bool small);
#endif
#ifdef OGLRENDERER_ENABLED
std::unique_ptr<WideGpuBackend> WideGpuMakeGL(std::string& info);
// Set by the frontend: an OpenGL function by name (for glClipControl, which the bundled
// loader lacks: depth from 0 to 1 keeps the precision that 1/W needs).
extern void* (*WideGpuGLGetProc)(const char* name);
// Set by the frontend when the game is played (not in the headless tests, where the context
// simply stays current on their one thread): the drawing done on a thread of its own, with the
// context current there (see GPU3D_WideGpu_GL.cpp).
extern bool WideGpuGLThreaded;
// Set by the frontend: lets go of the context on the calling thread (that thread's end).
extern void (*WideGpuGLRelease)();
#endif
}
