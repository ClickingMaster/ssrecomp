#include "merged.h"
#include <cassert>
#include <cstdio>
int main() {
    std::vector<uint32_t> pixels(256 * 192);
    std::vector<uint8_t> flags(256 * 192, 2);
    uint32_t edge[384] = {};
    uint16_t blend[384]; std::fill(blend, blend + 384, 16);
    uint8_t flat[384] = {};
    ScreenImage top{pixels.data(), flags.data(), edge, blend, flat};
    Scene scene; scene.smartEdges = true; scene.items = {FullScreen(0)};
    for (int x = 0; x < 256; ++x) pixels[x] = (x % 16 < 4 ? 0x445566 : 0xCCDDEE);
    assert(MergedEdgeContinuation(scene, top, 0, 0).pattern);
    assert(MergedEdgeContinuation(scene, top, 0, 1).pattern);
    // A non-repeating window border and its advance arrow are not tiles.
    std::fill(pixels.begin(), pixels.end(), 0xEEEEEE);
    for (int x : {0, 3, 252, 255}) pixels[x] = 0x334455;
    pixels[246] = pixels[247] = 0x222222;
    assert(!MergedEdgeContinuation(scene, top, 0, 0).pattern);
    assert(!MergedEdgeContinuation(scene, top, 0, 1).pattern);
    // Flat fades and fills still extend, and a 3D edge remains transparent.
    flat[0] = 1;
    auto fill = MergedEdgeContinuation(scene, top, 0, 0);
    assert(!fill.none() && !fill.pattern && fill.evb == 0);
    flags[0] = 0x40;
    assert(MergedEdgeContinuation(scene, top, 0, 0).none());
    // The flat strokes of the same closed window must not stick out either.
    std::fill(flags.begin(), flags.end(), 2);
    std::fill(flat, flat + 384, 1);
    flat[5 * 2] = 0;
    pixels[5 * 256] = 0x123456;
    MergedPrepareEdgeWindows(scene, &top);
    assert(MergedEdgeContinuation(scene, top, 0, 0).none());
    assert(MergedEdgeContinuation(scene, top, 5, 0).none());
    // Recomputing for the next frame must clear the mask: a full fade still fills.
    std::fill(flat, flat + 384, 1);
    MergedPrepareEdgeWindows(scene, &top);
    assert(!MergedEdgeContinuation(scene, top, 0, 0).none());
    puts("Wide edges: tiled banners preserved; frame borders/arrows not repeated");
}
