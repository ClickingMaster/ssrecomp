// The smooth (filtered) compose honours keyRing and dropSpriteCorner like the nearest-pixel one.
#include "merged.h"
#include <cassert>
#include <cstdio>
static uint32_t Px(const std::vector<uint32_t>& o, int w, int h, float bx, float by)
{
    const MergedView v = MergedMakeView(w, h);
    return o[(size_t)(v.originY + by * v.unit) * w + (size_t)(v.originX + bx * v.unit)] & 0xFFFFFF;
}
int main() {
    const uint32_t K = 0x336699, F = 0xCC8844;
    std::vector<uint32_t> pix(256 * 192, F);
    std::vector<uint8_t> flags(256 * 192, 1);
    // A 32x32 piece: a key-coloured rim 2 pixels wide, and a key-coloured block inside it.
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            if (x < 2 || y < 2 || x >= 30 || y >= 30 || (x >= 12 && x < 20 && y >= 12 && y < 20)) pix[y * 256 + x] = K;
    // A sprite pixel in the piece's corner.
    pix[3 * 256 + 3] = 0x00FF00; flags[3 * 256 + 3] = 0x10;
    ScreenImage scr{pix.data(), flags.data()};
    ScreenImage screens[2] = { scr, scr };
    for (bool smooth : { false, true }) {
        Scene sc; sc.items = { Crop(0, 0, 0, 32, 32) };
        sc.items[0].keys[0] = K; sc.items[0].keyRing = 2; sc.items[0].dropSpriteCorner = 5;
        std::vector<uint32_t> out;
        MergedCompose(sc, screens, 1024, 768, out, smooth);
        const MergedView v = MergedMakeView(1024, 768);
        (void)v;
        // The block inside the ring stays; the rim goes; the corner sprite goes.
        printf("smooth=%d block=%06X rim=%06X sprite=%06X\n", smooth, Px(out, 1024, 768, 16, 16), Px(out, 1024, 768, 0.4f, 16), Px(out, 1024, 768, 3.5f, 3.5f));
        assert(Px(out, 1024, 768, 16, 16) == K);
        assert(Px(out, 1024, 768, 0.4f, 16) != K);
        assert(Px(out, 1024, 768, 3.5f, 3.5f) != 0x00FF00);
    }
    puts("Smooth compose: keyRing and dropSpriteCorner honoured");
}
