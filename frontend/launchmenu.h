// The start menu of the Android TV launcher (tools/android/tv_launcher.py): the app has
// neither the game nor its code in it, so it finds the player's ROM on the device (the
// Download folder, a USB stick, ...) and lets them choose it with a remote or a controller.
// Drawn with the pause menu's Canvas and Menu (ui.h).
#pragma once
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>
#include <SDL.h>
#include "ui.h"

namespace LaunchMenu
{
// .nds files in dir and, depth levels down, in the folders in it.
inline void FindRoms(const std::filesystem::path& dir, int depth, std::vector<std::string>& out)
{
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        const std::filesystem::path& p = it->path();
        const std::string name = p.filename().u8string();
        if (name.empty() || name[0] == '.' || name == "Android") continue;
        std::error_code ec2;
        if (it->is_directory(ec2))
        {
            if (depth > 0) FindRoms(p, depth - 1, out);
        }
        else if (name.size() > 4)
        {
            std::string ext = name.substr(name.size() - 4);
            for (char& c : ext) c = (char)tolower((unsigned char)c);
            if (ext == ".nds") out.push_back(p.u8string());
        }
    }
}

// Where a player is likely to have put the ROM: the shared storage (Download, a ROMs
// folder, ...), the app's own folder there, and USB sticks and SD cards (/storage/XXXX-XXXX).
inline std::vector<std::string> ScanRoms(const std::string& appDir)
{
    std::vector<std::string> roms;
    FindRoms("/storage/emulated/0", 2, roms);
    if (!appDir.empty()) FindRoms(appDir, 1, roms);
    std::error_code ec;
    for (std::filesystem::directory_iterator it("/storage", ec), end; !ec && it != end; it.increment(ec))
    {
        const std::string name = it->path().filename().u8string();
        if (name != "emulated" && name != "self") FindRoms(it->path(), 2, roms);
    }
    std::sort(roms.begin(), roms.end());
    roms.erase(std::unique(roms.begin(), roms.end()), roms.end());
    return roms;
}

// Upper case (the menu's font), with what it cannot draw as spaces, shortened to fit.
inline std::string Label(const std::string& s, size_t max)
{
    std::string out;
    for (char c : s)
    {
        if (c >= 'a' && c <= 'z') c -= 32;
        out += ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr(":-+./<>!()", c)) ? c : ' ';
    }
    return out.size() > max ? out.substr(0, max - 3) + "..." : out;
}

// Shows the menu until a ROM is chosen; returns its path, or "" to quit. last is the ROM
// chosen the time before (selected first).
inline std::string Run(const std::string& appDir, const std::string& last)
{
    SDL_Window* window = SDL_CreateWindow("SoulSilver", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720,
        SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, 0) : nullptr;
    if (!renderer) return last;
    SDL_GameController* pads[8] = {};
    for (int i = 0; i < SDL_NumJoysticks() && i < 8; i++)
        if (SDL_IsGameController(i)) pads[i] = SDL_GameControllerOpen(i);

    std::vector<std::string> roms;
    Menu menu;
    menu.open = true;
    bool help = false;
    auto rebuild = [&]() {
        roms = ScanRoms(appDir);
        menu.items.clear();
        for (const auto& r : roms)
            menu.items.push_back({ "PLAY " + Label(std::filesystem::u8path(r).filename().u8string(), 26), "" });
        menu.items.push_back({ "LOOK FOR ROMS AGAIN", "" });
        menu.items.push_back({ "WHERE TO PUT THE ROM", "" });
        menu.items.push_back({ "QUIT", "" });
        menu.selected = 0;
        for (size_t i = 0; i < roms.size(); i++)
            if (roms[i] == last) menu.selected = (int)i;
    };
    rebuild();

    Canvas canvas;
    SDL_Texture* tex = nullptr;
    int texW = 0, texH = 0;
    std::string chosen;
    bool running = true;
    while (running)
    {
        MenuInput in;
        SDL_Event ev;
        while (SDL_WaitEventTimeout(&ev, 50))
        {
            if (ev.type == SDL_QUIT) running = false;
            else if (ev.type == SDL_KEYDOWN)
            {
                switch (ev.key.keysym.sym)
                {
                case SDLK_UP: in.up = true; break;
                case SDLK_DOWN: in.down = true; break;
                case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SELECT: case SDLK_SPACE: in.accept = true; break;
                case SDLK_AC_BACK: case SDLK_ESCAPE: in.back = true; break;
                default: break;
                }
            }
            else if (ev.type == SDL_CONTROLLERBUTTONDOWN)
            {
                switch (ev.cbutton.button)
                {
                case SDL_CONTROLLER_BUTTON_DPAD_UP: in.up = true; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: in.down = true; break;
                case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_START: in.accept = true; break;
                case SDL_CONTROLLER_BUTTON_B: in.back = true; break;
                default: break;
                }
            }
            else if (ev.type == SDL_CONTROLLERDEVICEADDED && ev.cdevice.which < 8 && !pads[ev.cdevice.which])
                pads[ev.cdevice.which] = SDL_GameControllerOpen(ev.cdevice.which);
            else if (ev.type == SDL_FINGERDOWN || ev.type == SDL_MOUSEBUTTONDOWN)
                in.accept = true; // a touch screen or mouse: plays the selected entry
        }
        if (help)
        {
            if (in.accept || in.back) help = false;
        }
        else
        {
            int dir;
            const int item = menu.Update(in, dir);
            const int n = (int)roms.size();
            if (in.back) running = false;
            else if (item >= 0 && item < n) { chosen = roms[item]; running = false; }
            else if (item == n) rebuild();
            else if (item == n + 1) help = true;
            else if (item == n + 2) running = false;
        }

        int w, h;
        SDL_GetRendererOutputSize(renderer, &w, &h);
        canvas.Resize(w, h);
        std::fill(canvas.px.begin(), canvas.px.end(), 0xFF101828);
        canvas.x0d = canvas.y0d = 0;
        canvas.x1d = w;
        canvas.y1d = h;
        const int sc = std::max(1, h / 240);
        if (help)
        {
            const char* lines[] = {
                "PUT YOUR POKEMON SOULSILVER (US) ROM,",
                "A FILE ENDING IN .NDS, IN ONE OF THESE:",
                "",
                "- THE DOWNLOAD FOLDER OF THIS DEVICE",
                "- A USB STICK OR SD CARD",
                "",
                "E.G. WITH A FILE MANAGER, A USB STICK, OR AN",
                "APP THAT SENDS FILES TO THE TV.",
                "THEN CHOOSE: LOOK FOR ROMS AGAIN.",
                "",
                "SAVES GO IN THE FOLDER SSRECOMP ON THE DEVICE.",
            };
            int y = h / 2 - 6 * 11 * sc;
            for (const char* l : lines)
            {
                canvas.Text((w - Canvas::TextWidth(l, sc)) / 2, y, l, sc, 0xFFE8E8E8);
                y += 11 * sc;
            }
            canvas.Text((w - Canvas::TextWidth("A: BACK", sc)) / 2, y + 11 * sc, "A: BACK", sc, 0xFFFFD75E);
        }
        else
        {
            menu.Draw(canvas, "POKEMON SOULSILVER",
                roms.empty() ? "NO ROM FOUND YET" : "A: PLAY   B: QUIT");
        }
        if (!tex || texW != w || texH != h)
        {
            if (tex) SDL_DestroyTexture(tex);
            tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
            texW = w;
            texH = h;
        }
        SDL_UpdateTexture(tex, nullptr, canvas.px.data(), w * 4);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, tex, nullptr, nullptr);
        SDL_RenderPresent(renderer);
    }
    if (tex) SDL_DestroyTexture(tex);
    for (auto* p : pads)
        if (p) SDL_GameControllerClose(p);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    return chosen;
}
}
