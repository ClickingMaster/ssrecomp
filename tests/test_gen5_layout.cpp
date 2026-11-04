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
            // An item chosen: both screens side by side, as on the DS, over the Bag's blue.
            assert(sc.items.size() == 3);
            const Element& top = sc.items[1];
            const Element& touch = sc.items[2];
            assert(top.screen == 0 && touch.screen == 1);
            assert(top.sw == 256 && top.sh == 192 && touch.sw == 256 && touch.sh == 192);
            assert(top.scale == touch.scale && top.scale <= 1);
            assert(top.dx + 256 * top.scale <= touch.dx + 0.01f); // No overlap.
            assert(top.dx >= 128 - view.width / 2 - 0.01f); // Inside the window.
            assert(touch.dx + 256 * touch.scale <= 128 + view.width / 2 + 0.01f);

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
                    // The touch screen's message is shown whole by both composition paths.
                    const int x = (int)(view.originX + (touch.dx + 30 * touch.scale) * view.unit);
                    const int y = (int)(view.originY + (touch.dy + 12 * touch.scale) * view.unit);
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

    // Battle questions: the question at full size, both answers in the message bar,
    // right of the text and inside the window.
    for (float width : { 256.0f, 341.33f, 455.11f, 512.0f })
    {
        const Scene question = BattleChoiceScene(width);
        assert(question.items.size() == 3);
        const Element& text = question.items[0];
        assert(text.screen == 0 && text.sy == 0 && text.sh == 192 && text.scale == 1);
        float last = 0;
        for (int i = 1; i <= 2; i++)
        {
            const Element& b = question.items[i];
            assert(b.screen == 1 && b.sw == 252 && b.sh == 47);
            assert(b.dy >= 147 && b.dy + b.sh * b.scale <= 190); // In the bar.
            assert(b.dy >= last);                                // Stacked, no overlap.
            assert(b.dx >= 168);                                 // Clear of the question.
            assert(b.dx + b.sw * b.scale <= 128 + width / 2);    // Inside the window.
            last = b.dy + b.sh * b.scale;
        }
    }
    SDL_Quit();
    puts("Gen 5 layouts: side-by-side Bag, battle questions and compositors passed.");
}
