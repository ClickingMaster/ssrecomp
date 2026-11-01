#pragma once
// RetroAchievements while a GBA game from the list is running (Pokemon GBA Widescreen's embed.h).
// The same account as the DS games: ra=, ra_user= and ra_token= in the settings file. The GBA
// game's pause menu is the one that shows them; this starts the client for that window and
// closes it when the game does, so a DS game started afterwards begins its own.
#include "achievements.h"
#include "embed.h"
#include "launcher.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#ifndef SSR_RA_VERSION
#define SSR_RA_VERSION "0.0.0"
#endif

namespace GbaRa
{

inline void WriteKeys(const std::string& cfg, const std::vector<std::pair<std::string, std::string>>& set)
{
    std::vector<std::string> lines;
    if (FILE* f = fopen(cfg.c_str(), "r"))
    {
        char buf[2048];
        while (fgets(buf, sizeof buf, f))
        {
            std::string l = buf;
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            bool ours = false;
            for (const auto& kv : set)
                ours |= l.size() > kv.first.size() && !l.compare(0, kv.first.size(), kv.first) && l[kv.first.size()] == '=';
            if (!ours && !l.empty()) lines.push_back(std::move(l));
        }
        fclose(f);
    }
    for (const auto& kv : set)
        if (!kv.second.empty()) lines.push_back(kv.first + "=" + kv.second);
    if (FILE* f = fopen(cfg.c_str(), "w"))
    {
        for (const std::string& l : lines) fprintf(f, "%s\n", l.c_str());
        fclose(f);
    }
}

struct Host
{
    Achievements::Account account;
    std::string cfg;
    std::vector<Achievements::Row> rows;
    bool began = false;
    GbaWide::AchievementsHost api{};

    explicit Host(const std::string& cfgPath) : cfg(cfgPath)
    {
        account.on = Launcher::ConfigValue(cfg, "ra") == "1";
        account.user = Launcher::ConfigValue(cfg, "ra_user");
        account.token = Launcher::ConfigValue(cfg, "ra_token");
        api.ud = this;
        api.begin = Begin;
        api.end = End;
        api.frame = Frame;
        api.pump = PumpFn;
        api.reset = Reset;
        api.setOn = SetOn;
        api.login = Login;
        api.logout = Logout;
        api.account = AccountLine;
        api.game = GameLine;
        api.detail = Detail;
        api.pop = Pop;
        api.loggedIn = LoggedIn;
        api.active = Active;
        api.busy = Busy;
        api.on = On;
        api.count = Count;
        api.at = At;
    }

    void Save() const
    {
        WriteKeys(cfg, { { "ra", account.on ? "1" : "0" }, { "ra_user", account.user }, { "ra_token", account.token } });
    }

    static bool Copy(char* buf, size_t cap, const std::string& s)
    {
        if (!buf || !cap) return false;
        snprintf(buf, cap, "%s", s.c_str());
        return buf[0] != 0;
    }

    static bool Begin(void* ud, const uint8_t* rom, size_t n, void* core, GbaWide::AchievementsHost::Read read)
    {
        auto* h = (Host*)ud;
        if (!h || !Achievements::Available() || !rom || !n || !read) return false;
        Achievements::BeginSession(&h->account, std::string("PokemonDSWidescreen/") + SSR_RA_VERSION, [h]() { h->Save(); });
        h->began = true;
        std::vector<uint8_t> bytes(rom, rom + n);
        Achievements::SetGameMemory(read, core, Achievements::FingerprintGba(bytes));
        return true;
    }
    static void End(void* ud)
    {
        auto* h = (Host*)ud;
        if (!h || !h->began) return;
        h->began = false;
        Achievements::EndSession();
    }
    static void Frame(void*) { Achievements::DoFrame(); }
    static void PumpFn(void*, int paused) { Achievements::Pump(paused != 0); }
    static void Reset(void*) { Achievements::Reset(); }
    static void SetOn(void*, int on) { Achievements::SetOn(on != 0); }
    static void Login(void*, const char* user, const char* password)
    {
        Achievements::Login(user ? user : "", password ? password : "");
    }
    static void Logout(void*) { Achievements::Logout(); }
    static bool AccountLine(void*, char* buf, size_t cap) { return Copy(buf, cap, Achievements::AccountLine()); }
    static bool GameLine(void*, char* buf, size_t cap) { return Copy(buf, cap, Achievements::GameLine()); }
    static bool Detail(void*, char* buf, size_t cap) { return Copy(buf, cap, Achievements::Detail()); }
    static bool Pop(void*, char* buf, size_t cap)
    {
        std::string m;
        if (!Achievements::PopMessage(m)) return false;
        return Copy(buf, cap, m);
    }
    static bool LoggedIn(void*) { return Achievements::LoggedIn(); }
    static bool Active(void*) { return Achievements::Active(); }
    static bool Busy(void*) { return Achievements::Busy(); }
    static bool On(void* ud)
    {
        auto* h = (Host*)ud;
        return h && h->account.on;
    }
    static int Count(void* ud)
    {
        auto* h = (Host*)ud;
        if (!h) return 0;
        h->rows = Achievements::List();
        return (int)h->rows.size();
    }
    static bool At(void* ud, int index, char* title, size_t titleCap, char* description, size_t descriptionCap,
                   char* progress, size_t progressCap, unsigned* points, int* unlocked)
    {
        auto* h = (Host*)ud;
        if (!h || index < 0 || index >= (int)h->rows.size()) return false;
        const Achievements::Row& r = h->rows[index];
        Copy(title, titleCap, r.title);
        Copy(description, descriptionCap, r.description);
        Copy(progress, progressCap, r.progress);
        if (points) *points = r.points;
        if (unlocked) *unlocked = r.unlocked ? 1 : 0;
        return true;
    }
};

// Plays the GBA game, with RetroAchievements when this build can reach the server.
inline bool Play(const std::string& rom, GbaWide::Options& o, const std::string& cfg)
{
    if (!Achievements::Available()) return GbaWide::Play(rom, o);
    Host host(cfg);
    o.achievements = &host.api;
    const bool ok = GbaWide::Play(rom, o);
    if (host.began) Host::End(&host);
    return ok;
}

}
