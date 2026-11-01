// ROM-free regression: build with C++17 and SDL2, then run with SDL_VIDEODRIVER=dummy.
#include <cassert>
#include <cstdio>
#include "../frontend/gpucompose.h"

int main()
{
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    constexpr uint32_t blue = 0xFF103870, white = 0xFFF8F8F8, ink = 0xFF303030;
    std::vector<uint32_t> top(256 * 192, blue), bottom(256 * 192, blue);
    // An item's description and a distinct colour in the last row of its move stats.
    for (int y = 40; y < 50; y++)
        for (int x = 16; x < 96; x++) top[y * 256 + x] = white;
    for (int x = 8; x < 248; x++) top[191 * 256 + x] = 0xFF00E000;
    ScreenImage screens[2] = { { top.data(), nullptr }, { bottom.data(), nullptr } };
    for (int width : { 1024, 1280, 1920, 2560 })
        for (bool message : { false, true })
        {
            std::fill(bottom.begin(), bottom.end(), blue);
            if (message)
            {
                for (int y = 2; y < 44; y++)
                    for (int x = 2; x < 254; x++) bottom[y * 256 + x] = white;
                for (int y = 8; y < 18; y++)
                    for (int x = 8; x < 90; x++) bottom[y * 256 + x] = ink;
            }
            const MergedView view = MergedMakeView(width, 768);
            const Scene sc = BagScene(view.width, screens);
            assert(sc.items.size() == 2);
            const Element& list = sc.items[0];
            const Element& card = sc.items[1];
            assert(card.sy + card.sh == 192); // No move stats cropped out.
            assert(card.dy + card.sh * card.scale <= 164.01f); // Pocket controls remain clear.
            assert(card.dx + card.sw * card.scale <= list.dx + 116);
            if (message) assert(card.dy >= 44);

            std::vector<uint32_t> cpu;
            MergedCompose(sc, screens, width, 768, cpu, false, nullptr);
            SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, 768, 32, SDL_PIXELFORMAT_ARGB8888);
            assert(surface);
            SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
            assert(renderer);
            {
                GpuCompositor compositor(renderer);
                compositor.Draw(sc, screens, width, 768, false, nullptr);
                SDL_RenderPresent(renderer);
                if (message)
                {
                    // The first word is in the area the old item card covered. Both composition
                    // paths must preserve it rather than drawing the description over it.
                    const int x = (int)(view.originX + (list.dx + 16) * view.unit);
                    const int y = (int)(view.originY + 12 * view.unit);
                    assert((cpu[y * width + x] & 0xFFFFFF) == (ink & 0xFFFFFF));
                    const auto* row = reinterpret_cast<const uint32_t*>(
                        static_cast<const uint8_t*>(surface->pixels) + y * surface->pitch);
                    assert((row[x] & 0xFFFFFF) == (ink & 0xFFFFFF));
                }
            }
            SDL_DestroyRenderer(renderer);
            SDL_FreeSurface(surface);
        }
    // Empty pockets and missing screen images must not create a description card.
    std::fill(top.begin(), top.end(), blue);
    assert(BagScene(341.33f, screens).items.size() == 1);
    assert(BagScene(341.33f, nullptr).items.size() == 1);
    SDL_Quit();
    puts("Bag layout: dialogue, move stats, controls and both compositors passed.");
}
