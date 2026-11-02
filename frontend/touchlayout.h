// Shared touch layout storage and editor. Gameplay supplies its own default control geometry.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <SDL.h>

namespace TouchLayout
{
struct Control
{
    std::string key, label;
    float x, y, rx, ry;
    bool round = false;
};
struct Placement { float x = 0.5f, y = 0.5f, size = 1; };
struct Profile
{
    std::map<std::string, Placement> orientations[2];
    static int Orientation(int w, int h) { return h > w ? 1 : 0; }
    static std::string Path(const std::string& config, const char* system)
    {
        const auto slash = config.find_last_of("/\\");
        return (slash == std::string::npos ? "" : config.substr(0, slash + 1)) + "touch-layout-" + system + ".cfg";
    }
    void Load(const std::string& path)
    {
        std::ifstream in(path);
        std::string version;
        if (!(in >> version) || version != "fulldex-touch-v1") return;
        Profile parsed;
        std::string line;
        std::getline(in, line);
        while (std::getline(in, line))
        {
            std::istringstream row(line);
            int orientation;
            std::string key;
            Placement p;
            if (!(row >> orientation >> key >> p.x >> p.y >> p.size)) continue;
            if (orientation < 0 || orientation > 1 || key.size() > 16 ||
                !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.size) ||
                p.x < 0 || p.x > 1 || p.y < 0 || p.y > 1 || p.size < 0.5f || p.size > 1.75f) continue;
            parsed.orientations[orientation][key] = p;
        }
        *this = parsed;
    }
    bool Save(const std::string& path) const
    {
        const std::string temp = path + ".tmp";
        std::ofstream out(temp, std::ios::trunc);
        out << "fulldex-touch-v1\n";
        for (int o = 0; o < 2; ++o)
            for (const auto& entry : orientations[o])
                out << o << ' ' << entry.first << ' ' << entry.second.x << ' ' << entry.second.y << ' ' << entry.second.size << '\n';
        out.flush();
        if (!out) { out.close(); std::remove(temp.c_str()); return false; }
        out.close();
        if (!out || std::rename(temp.c_str(), path.c_str()) != 0) { std::remove(temp.c_str()); return false; }
        return true;
    }
    std::vector<Control> Apply(const std::vector<Control>& defaults, int w, int h) const
    {
        auto controls = defaults;
        for (auto& c : controls)
        {
            const auto it = orientations[Orientation(w, h)].find(c.key);
            if (it != orientations[Orientation(w, h)].end())
            {
                c.rx *= it->second.size;
                c.ry *= it->second.size;
                c.x = it->second.x * w;
                c.y = it->second.y * h;
            }
            c.rx = std::min(c.rx, w * 0.49f);
            c.ry = std::min(c.ry, h * 0.49f);
            c.x = std::clamp(c.x, c.rx, w - c.rx);
            c.y = std::clamp(c.y, c.ry, h - c.ry);
        }
        return controls;
    }
    void Move(const Control& c, int w, int h, float x, float y)
    {
        auto& p = orientations[Orientation(w, h)][c.key];
        p.x = std::clamp(x, c.rx, w - c.rx) / w;
        p.y = std::clamp(y, c.ry, h - c.ry) / h;
    }
    void Resize(const Control& current, int w, int h, float delta)
    {
        auto& entries = orientations[Orientation(w, h)];
        auto it = entries.find(current.key);
        if (it == entries.end())
            it = entries.emplace(current.key, Placement{current.x / w, current.y / h, 1}).first;
        it->second.size = std::clamp(it->second.size + delta, 0.5f, 1.75f);
    }
};

// A separate event loop keeps editor touches out of the game and pauses emulation.
// CanvasT is the existing DS or GBA pixel-font canvas; nothing about a game is shared here.
template<class CanvasT, class Defaults>
bool Edit(SDL_Renderer* renderer, Profile& profile, const std::string& path, Defaults defaults)
{
    Profile working = profile;
    CanvasT canvas;
    SDL_Texture* texture = nullptr;
    int tw = 0, th = 0, chosen = 0;
    bool dragging = false, mouse = false, done = false, saved = false;
    SDL_FingerID finger = 0;
    float offsetX = 0, offsetY = 0;
    std::string notice = "DRAG A CONTROL. USE - / + TO RESIZE.";
    while (!done)
    {
        int w = 0, h = 0;
        SDL_GetRendererOutputSize(renderer, &w, &h);
        if (w <= 0 || h <= 0) break;
        const int cw = w >= h ? 960 : 540, ch = std::max(220, (int)std::lround((double)h * cw / w));
        const float zoom = std::min((cw - 20.0f) / w, (ch - 130.0f) / h);
        const float px = (cw - w * zoom) / 2, py = 58 + (ch - 130 - h * zoom) / 2;
        auto controls = working.Apply(defaults(w, h), w, h);
        if (controls.empty()) break;
        chosen = std::clamp(chosen, 0, (int)controls.size() - 1);
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                SDL_PushEvent(&event);
                done = true;
                break;
            }
            if (event.type == SDL_APP_WILLENTERBACKGROUND) { done = true; break; }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            {
                dragging = false;
                break;
            }
            if (event.type == SDL_KEYDOWN && (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_AC_BACK))
                done = true;
            const bool touch = event.type == SDL_FINGERDOWN || event.type == SDL_FINGERMOTION || event.type == SDL_FINGERUP;
            const bool pointer = event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEBUTTONUP;
            if (!touch && !pointer) continue;
            if (pointer && ((event.type == SDL_MOUSEMOTION && event.motion.which == SDL_TOUCH_MOUSEID) ||
                (event.type != SDL_MOUSEMOTION && event.button.which == SDL_TOUCH_MOUSEID))) continue;
            const bool down = event.type == SDL_FINGERDOWN || event.type == SDL_MOUSEBUTTONDOWN;
            const bool up = event.type == SDL_FINGERUP || event.type == SDL_MOUSEBUTTONUP;
            const float x = touch ? event.tfinger.x * cw : (event.type == SDL_MOUSEMOTION ? event.motion.x : event.button.x) * (float)cw / w;
            const float y = touch ? event.tfinger.y * ch : (event.type == SDL_MOUSEMOTION ? event.motion.y : event.button.y) * (float)ch / h;
            if (down && !dragging)
            {
                if (y >= ch - 48)
                {
                    const int action = std::clamp((int)(x * 7 / cw), 0, 6);
                    if (action == 0) chosen = (chosen + (int)controls.size() - 1) % controls.size();
                    if (action == 1) chosen = (chosen + 1) % controls.size();
                    if (action == 2 || action == 3) working.Resize(controls[chosen], w, h, action == 2 ? -0.1f : 0.1f);
                    if (action == 4) working.orientations[Profile::Orientation(w, h)].clear();
                    if (action == 5) done = true;
                    if (action == 6)
                    {
                        if (working.Save(path)) { profile = working; saved = done = true; }
                        else notice = "COULD NOT SAVE. TRY AGAIN OR CANCEL.";
                    }
                    controls = working.Apply(defaults(w, h), w, h);
                    continue;
                }
                const float gx = (x - px) / zoom, gy = (y - py) / zoom;
                // The smallest hit wins when controls overlap; NEXT always reaches every control.
                int hit = -1;
                float area = 1e30f;
                for (int i = 0; i < (int)controls.size(); ++i)
                {
                    const auto& c = controls[i];
                    if (std::fabs(gx - c.x) <= c.rx && std::fabs(gy - c.y) <= c.ry && c.rx * c.ry < area)
                        hit = i, area = c.rx * c.ry;
                }
                if (hit >= 0)
                {
                    chosen = hit;
                    dragging = true;
                    mouse = pointer;
                    finger = touch ? event.tfinger.fingerId : 0;
                    offsetX = gx - controls[chosen].x;
                    offsetY = gy - controls[chosen].y;
                }
            }
            else if (dragging && ((mouse && pointer) || (!mouse && touch && finger == event.tfinger.fingerId)))
            {
                working.Move(controls[chosen], w, h, (x - px) / zoom - offsetX, (y - py) / zoom - offsetY);
                controls = working.Apply(defaults(w, h), w, h);
                if (up) dragging = false;
            }
        }
        if (done) break;
        controls = working.Apply(defaults(w, h), w, h);
        canvas.Resize(cw, ch);
        std::fill(canvas.px.begin(), canvas.px.end(), 0xFF111827);
        canvas.Fill((int)px, (int)py, (int)(w * zoom), (int)(h * zoom), 0xFF243247);
        const int font = cw >= 800 ? 2 : 1;
        auto text = [&](int x, int y, const std::string& value, uint32_t colour) { canvas.Text(x, y, value, font, colour); };
        text(12, 10, "TOUCH CONTROLS", 0xFFFFFFFF);
        const auto it = working.orientations[Profile::Orientation(w, h)].find(controls[chosen].key);
        const int size = it == working.orientations[Profile::Orientation(w, h)].end() ? 100 : (int)std::lround(it->second.size * 100);
        text(12, 32, controls[chosen].label + "  SIZE " + std::to_string(size) + "%", 0xFF7DD3FC);
        for (int i = 0; i < (int)controls.size(); ++i)
        {
            const auto& c = controls[i];
            const int cx = (int)(px + c.x * zoom), cy = (int)(py + c.y * zoom);
            const int rx = std::max(3, (int)(c.rx * zoom)), ry = std::max(3, (int)(c.ry * zoom));
            const uint32_t colour = i == chosen ? 0xFF0284C7 : 0xFF52627A;
            if (c.round)
                for (int dy = -ry; dy <= ry; ++dy)
                {
                    const int half = (int)(rx * std::sqrt(std::max(0.0f, 1.0f - (float)dy * dy / (ry * ry))));
                    canvas.Fill(cx - half, cy + dy, half * 2 + 1, 1, colour);
                }
            else canvas.Fill(cx - rx, cy - ry, rx * 2, ry * 2, colour);
            const int labelScale = std::min(font, rx * 2 >= (int)c.label.size() * 12 ? 2 : 1);
            canvas.Text(cx - ((int)c.label.size() * 6 - 1) * labelScale / 2, cy - 3 * labelScale, c.label, labelScale, 0xFFFFFFFF);
        }
        text(12, ch - 68, notice, 0xFFC5D3E5);
        const char* actions[] = { "PREV", "NEXT", "-", "+", "RESET", "CANCEL", "SAVE" };
        for (int i = 0; i < 7; ++i)
        {
            const int left = i * cw / 7, right = (i + 1) * cw / 7;
            canvas.Fill(left + 2, ch - 46, right - left - 4, 44, i == 6 ? 0xFF0369A1 : 0xFF34465F);
            text((left + right - ((int)std::string(actions[i]).size() * 6 - 1) * font) / 2, ch - 29, actions[i], 0xFFFFFFFF);
        }
        if (!texture || cw != tw || ch != th)
        {
            if (texture) SDL_DestroyTexture(texture);
            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, cw, ch);
            tw = cw; th = ch;
            if (!texture) break;
        }
        SDL_UpdateTexture(texture, nullptr, canvas.px.data(), cw * 4);
        SDL_SetRenderDrawColor(renderer, 17, 24, 39, 255);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }
    if (texture) SDL_DestroyTexture(texture);
    return saved;
}
} // namespace TouchLayout
