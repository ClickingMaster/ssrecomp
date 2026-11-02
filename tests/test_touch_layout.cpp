#include <cassert>
#include <filesystem>
#include <fstream>
#include <thread>
#include <SDL.h>
enum { PAD_A, PAD_B, PAD_X, PAD_Y, PAD_BACK, PAD_GUIDE, PAD_START, PAD_LS, PAD_RS, PAD_LB, PAD_RB, PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_COUNT };
#include "touchpad.h"

int main()
{
    const std::string path = "/tmp/fulldex-touch-test-" + std::to_string(SDL_GetPerformanceCounter()) + ".cfg";
    TouchLayout::Profile profile;
    auto defaults = TouchPad::Defaults(1280, 720);
    assert(defaults.size() == 11);
    auto first = profile.Apply(defaults, 1280, 720);
    auto a = first[1];
    profile.Move(a, 1280, 720, 600, 300);
    profile.Resize(profile.Apply(defaults, 1280, 720)[1], 1280, 720, 0.5f);
    auto adjusted = profile.Apply(defaults, 1280, 720);
    assert(std::fabs(adjusted[1].x - 600) < 0.01f);
    assert(std::fabs(adjusted[1].rx / a.rx - 1.5f) < 0.01f);
    assert(profile.Save(path));
    TouchLayout::Profile loaded; loaded.Load(path);
    auto roundtrip = loaded.Apply(defaults, 1280, 720);
    assert(std::fabs(roundtrip[1].x - 600) < 0.01f);
    assert(std::fabs(roundtrip[1].rx - adjusted[1].rx) < 0.01f);
    auto portrait = loaded.Apply(TouchPad::Defaults(720, 1280), 720, 1280);
    assert(loaded.orientations[1].empty());
    loaded.Move(roundtrip[1], 1280, 720, -100, 9000);
    const auto clamped = loaded.Apply(defaults, 1280, 720)[1];
    assert(clamped.x >= clamped.rx && clamped.y <= 720 - clamped.ry);
    assert(!loaded.Save(path + "/missing/file"));
    { std::ofstream bad(path); bad << "fulldex-touch-v1\n0 A -2 5 99\n0 B 0.5 0.5 1.2\n3 X .5 .5 1\n"; }
    loaded.Load(path); assert(loaded.orientations[0].size() == 1);
    assert(loaded.orientations[0].count("B"));
    TouchPad pad; pad.profile = profile; pad.Layout(1280, 720);
    assert(pad.HitButton(600, 300) == PAD_A);
    pad.Down(1, 600, 300); bool held[PAD_COUNT] = {}; bool menu = false;
    pad.Apply(held, menu); assert(held[PAD_A] && !menu);
    std::remove(path.c_str());
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0);
    SDL_Window* window = SDL_CreateWindow("Touch layout test", 0, 0, 1280, 720, SDL_WINDOW_HIDDEN);
    assert(window);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE); assert(renderer);
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    auto tap = [](float x, float y) {
        SDL_Event e{}; e.type = SDL_FINGERDOWN; e.tfinger.fingerId = 123; e.tfinger.x = x; e.tfinger.y = y; SDL_PushEvent(&e);
        e.type = SDL_FINGERUP; SDL_PushEvent(&e);
    };
    // Resize the selected D-pad, save, then check the persisted result.
    std::thread save([&] { SDL_Delay(80); tap(3.5f / 7, .97f); SDL_Delay(40); tap(6.5f / 7, .97f); });
    TouchLayout::Profile edited;
    assert(TouchLayout::Edit<Canvas>(renderer, edited, path, TouchPad::Defaults)); save.join();
    assert(std::fabs(edited.orientations[0].at("DPAD").size - 1.1f) < .01f);
    // RESET followed by CANCEL must keep both memory and the saved layout.
    std::thread cancel([&] { SDL_Delay(80); tap(4.5f / 7, .97f); SDL_Delay(40); tap(5.5f / 7, .97f); });
    assert(!TouchLayout::Edit<Canvas>(renderer, edited, path, TouchPad::Defaults)); cancel.join();
    assert(std::fabs(edited.orientations[0].at("DPAD").size - 1.1f) < .01f);
    TouchLayout::Profile persisted; persisted.Load(path);
    assert(std::fabs(persisted.orientations[0].at("DPAD").size - 1.1f) < .01f);
    // SAVE after RESET removes the customisation and still creates a readable profile.
    std::thread reset([&] { SDL_Delay(80); tap(4.5f / 7, .97f); SDL_Delay(40); tap(6.5f / 7, .97f); });
    assert(TouchLayout::Edit<Canvas>(renderer, edited, path, TouchPad::Defaults)); reset.join();
    assert(edited.orientations[0].empty());
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); std::remove(path.c_str());
    puts("Touch layout persistence, bounds, DS hit detection, editor save/cancel/reset passed");
}
