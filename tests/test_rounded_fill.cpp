// ROM-free regression: build with C++17 and SDL2, then run with SDL_VIDEODRIVER=dummy.
// A plain rectangle with rounded corners (the summary view's backing) has to come out the same
// from the GPU compositor as from MergedCompose: the GPU one filled it square, over its outline.
#include <cassert>
#include <cstdio>
#include "../frontend/gpucompose.h"

int main()
{
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    std::vector<uint32_t> top(256 * 192, 0xFF000000), bottom(256 * 192, 0xFF000000);
    ScreenImage screens[2] = { { top.data(), nullptr }, { bottom.data(), nullptr } };
    for (float radius : { 3.0f, 8.0f })
    {
        Scene sc;
        Element back = FillRect(20, 20, 100, 60, 0x2040C0);
        back.border = true;
        back.borderColor = 0xF0E020;
        back.borderRadius = radius;
        sc.items.push_back(back);
        const int w = 1024, h = 768;
        std::vector<uint32_t> cpu;
        MergedCompose(sc, screens, w, h, cpu, false, nullptr);
        SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
        assert(surface);
        SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
        assert(renderer);
        long differing = 0;
        {
            GpuCompositor compositor(renderer);
            compositor.Draw(sc, screens, w, h, false, nullptr);
            SDL_RenderPresent(renderer);
            for (int y = 0; y < h; y++)
            {
                const auto* row = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(surface->pixels) + y * surface->pitch);
                for (int x = 0; x < w; x++)
                    if ((row[x] & 0xFFFFFF) != (cpu[y * w + x] & 0xFFFFFF)) differing++;
            }
        }
        printf("radius %g: %ld pixels differ\n", radius, differing);
        fflush(stdout); assert(differing == 0);
        SDL_DestroyRenderer(renderer);
        SDL_FreeSurface(surface);
    }
    SDL_Quit();
    puts("Rounded rectangles: both compositors agree.");
}
