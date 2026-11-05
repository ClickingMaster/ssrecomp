// The games: finding the player's ROMs, telling which game each one is, and putting the
// widescreen patch on it as it starts. Nothing of the games comes with the program: it holds
// only this project's patches (our own code, tools/hookpatch.py) and the checks that make sure
// a ROM has what they need.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#include <SDL.h>
#include <mgba-util/crc32.h>
#include <mgba-util/sha1.h>

#include "patches.h" // made at build time (tools/embed_patches.py): kWidePatches

namespace gbaw
{

namespace Games
{

inline std::string Lower(std::string s)
{
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}
inline bool EndsWith(const std::string& s, const char* end)
{
    const size_t n = strlen(end);
    return s.size() >= n && Lower(s.substr(s.size() - n)) == end;
}
inline bool Exists(const std::string& path)
{
    if (SDL_RWops* f = SDL_RWFromFile(path.c_str(), "rb"))
    {
        SDL_RWclose(f);
        return true;
    }
    return false;
}
inline void MakeDir(const std::string& path)
{
#ifdef _WIN32
    _mkdir(path.c_str());
#else
    mkdir(path.c_str(), 0755);
#endif
}
// (Through SDL, which takes UTF-8 names on every platform.)
inline bool ReadFile(const std::string& path, std::vector<uint8_t>& out, size_t limit = 64 << 20)
{
    SDL_RWops* f = SDL_RWFromFile(path.c_str(), "rb");
    if (!f) return false;
    const Sint64 size = SDL_RWsize(f);
    bool ok = size >= 0 && (size_t)size <= limit;
    if (ok)
    {
        out.resize((size_t)size);
        ok = size == 0 || SDL_RWread(f, out.data(), 1, (size_t)size) == (size_t)size;
    }
    SDL_RWclose(f);
    return ok;
}
inline bool WriteFile(const std::string& path, const std::vector<uint8_t>& data)
{
    SDL_RWops* f = SDL_RWFromFile(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = data.empty() || SDL_RWwrite(f, data.data(), 1, data.size()) == data.size();
    return SDL_RWclose(f) == 0 && ok;
}
inline std::string Sha1(const std::vector<uint8_t>& data)
{
    uint8_t d[20];
    sha1Buffer(data.data(), data.size(), d);
    char hex[41];
    for (int i = 0; i < 20; i++) snprintf(hex + 2 * i, 3, "%02x", d[i]);
    return hex;
}

// An IPS patch onto data (strictly: false if it is cut short or not an IPS patch).
inline bool ApplyIps(std::vector<uint8_t>& rom, const uint8_t* p, size_t n)
{
    if (n < 8 || memcmp(p, "PATCH", 5)) return false;
    size_t i = 5;
    while (i + 3 <= n)
    {
        if (!memcmp(p + i, "EOF", 3) && (i + 3 == n || i + 6 == n))
        {
            if (i + 6 == n) rom.resize((size_t)(p[i + 3] << 16 | p[i + 4] << 8 | p[i + 5])); // (Lunar IPS: cut to this size)
            return true;
        }
        const size_t off = (size_t)(p[i] << 16 | p[i + 1] << 8 | p[i + 2]);
        i += 3;
        if (i + 2 > n) return false;
        size_t len = (size_t)(p[i] << 8 | p[i + 1]);
        i += 2;
        if (len)
        {
            if (i + len > n) return false;
            if (off + len > rom.size()) rom.resize(off + len, 0);
            memcpy(rom.data() + off, p + i, len);
            i += len;
        }
        else
        {
            if (i + 3 > n) return false;
            len = (size_t)(p[i] << 8 | p[i + 1]);
            if (off + len > rom.size()) rom.resize(off + len, 0);
            memset(rom.data() + off, p[i + 2], len);
            i += 3;
        }
    }
    return false;
}

// Whether a ROM has everything a widescreen patch needs (its .safety list): the functions it
// replaces and the tables it reads exactly as in the original game, the functions it calls
// still there, and its space free. If not, `why` says what is in the way.
inline bool PatchFits(const std::vector<uint8_t>& rom, const WidePatch& patch, std::string& why)
{
    const char* s = patch.safety;
    while (*s)
    {
        const char* end = strchr(s, '\n');
        const std::string line(s, end ? end - s : strlen(s));
        s = end ? end + 1 : s + line.size();
        char kind[8] = {}, name[96] = {};
        unsigned off = 0, size = 0, crc = 0;
        const int got = sscanf(line.c_str(), "%7s %x %x %x %95s", kind, &off, &size, &crc, name);
        if (got < 3) continue;
        if (!strcmp(kind, "free"))
        {
            // (Past the end of a smaller ROM (Japanese Ruby and Sapphire are 8 MB) is free too:
            // the patch makes the ROM longer.)
            for (unsigned i = 0; i < size && off + i < rom.size(); i++)
                if (rom[off + i] != 0xFF)
                {
                    why = "ITS FREE SPACE IS IN USE";
                    return false;
                }
        }
        else if ((size_t)off + size > rom.size())
        {
            why = "TOO SMALL";
            return false;
        }
        else if (got >= 4 && doCrc32(rom.data() + off, size) != crc)
        {
            why = std::string("IT CHANGES ") + name;
            return false;
        }
    }
    return true;
}

// The games this program knows by their exact ROM.
struct Known
{
    const char* sha1;
    const char* key;  // the save's name, and what --game takes
    const char* name; // as the list shows it
    const char* patch;
    uint32_t colour;
    const char* art; // its picture, art/<art>.jpg, "" for none yet
};
inline const std::vector<Known>& KnownGames()
{
    static const std::vector<Known> k = {
        { "f3ae088181bf583e55daf962a92bb46f4f1d07b7", "emerald", "POKEMON EMERALD", "emerald", 0xFF1E8C55, "emerald" },
        { "3b6c51d8fb6b513bdd01b7bda3ed8965bc985d58", "ultraviolet", "POKEMON ULTRA VIOLET", "firered", 0xFF6A3FC0, "ultraviolet" },
        // (FireRed 1.0 and 1.1 share a save, as do LeafGreen's: the same game either way.)
        { "41cb23d8dccc8ebd7c649cd8fbb58eeace6e2fdc", "firered", "POKEMON FIRERED", "firered", 0xFFC8382A, "firered" },
        { "dd5945db9b930750cb39d00c84da8571feebf417", "firered", "POKEMON FIRERED", "firered11", 0xFFC8382A, "firered" },
        { "574fa542ffebb14be69902d1d36f1ec0a4afd71e", "leafgreen", "POKEMON LEAFGREEN", "leafgreen", 0xFF4FA83A, "leafgreen" },
        { "7862c67bdecbe21d1d69ce082ce34327e1c6ed5e", "leafgreen", "POKEMON LEAFGREEN", "leafgreen11", 0xFF4FA83A, "leafgreen" },
        // (English Ruby and Sapphire, US and Europe: versions 1.2, 1.1 and 1.0 share a save.)
        { "5b64eacf892920518db4ec664e62a086dd5f5bc8", "ruby", "POKEMON RUBY", "ruby", 0xFFB8202E, "ruby" },
        { "89b45fb172e6b55d51fc0e61989775187f6fe63c", "sapphire", "POKEMON SAPPHIRE", "sapphire", 0xFF2452B0, "sapphire" },
        { "610b96a9c9a7d03d2bafb655e7560ccff1a6d894", "ruby", "POKEMON RUBY", "ruby11", 0xFFB8202E, "ruby" },
        { "4722efb8cd45772ca32555b98fd3b9719f8e60a9", "sapphire", "POKEMON SAPPHIRE", "sapphire11", 0xFF2452B0, "sapphire" },
        { "f28b6ffc97847e94a6c21a63cacf633ee5c8df1e", "ruby", "POKEMON RUBY", "ruby10", 0xFFB8202E, "ruby" },
        { "3ccbbd45f8553c36463f13b938e833f652b793e4", "sapphire", "POKEMON SAPPHIRE", "sapphire10", 0xFF2452B0, "sapphire" },
        // (German Ruby and Sapphire, version 1.1: their own saves, as a German save isn't an English one.)
        { "424740be1fc67a5ddb954794443646e6aeee2c1b", "rubyde", "POKEMON RUBIN", "rubyde", 0xFFB8202E, "ruby" },
        { "7e6e034f9cdca6d2c4a270fdb50a94def5883d17", "sapphirede", "POKEMON SAPHIR", "sapphirede", 0xFF2452B0, "sapphire" },
        // (French, Spanish, Italian and Japanese Ruby and Sapphire 1.1: their own saves too)
        { "ba888dfba231a231cbd60fe228e894b54fb1ed79", "rubyfr", "POKEMON RUBIS", "rubyfr", 0xFFB8202E, "ruby" },
        { "9ac73481d7f5d150a018309bba91d185ce99fb7c", "rubyes", "POKEMON RUBI", "rubyes", 0xFFB8202E, "ruby" },
        { "015a5d380afe316a2a6fcc561798ebff9dfb3009", "rubyit", "POKEMON RUBINO", "rubyit", 0xFFB8202E, "ruby" },
        { "971e0d670a95e5b32240b2deed20405b8daddf47", "rubyjp", "POCKET MONSTERS RUBY", "rubyjp", 0xFFB8202E, "ruby" },
        { "860e93f5ea44f4278132f6c1ee5650d07b852fd8", "sapphirefr", "POKEMON SAPHIR", "sapphirefr", 0xFF2452B0, "sapphire" },
        { "0fe9ad1e602e2fafa090aee25e43d6980625173c", "sapphirees", "POKEMON ZAFIRO", "sapphirees", 0xFF2452B0, "sapphire" },
        { "73edf67b9b82ff12795622dca412733755d2c0fe", "sapphireit", "POKEMON ZAFFIRO", "sapphireit", 0xFF2452B0, "sapphire" },
        { "01f509671445965236ac4c6b5a354fe2f1e69f13", "sapphirejp", "POCKET MONSTERS SAPPHIRE", "sapphirejp", 0xFF2452B0, "sapphire" },
    };
    return k;
}
// The art for a game the list knows (by its SHA-1), "" for none.
inline std::string ArtCode(const std::string& sha1)
{
    for (const Known& k : KnownGames())
        if (sha1 == k.sha1) return k.art;
    return "";
}
inline const WidePatch* FindPatch(const std::string& name)
{
    for (const WidePatch& p : kWidePatches)
        if (name == p.name) return &p;
    return nullptr;
}
// Which patch a ROM of this game code would take (a hack keeps its base game's code, or not:
// Ultra Violet is PUVV).
inline const char* PatchForCode(const std::string& code)
{
    if (code == "BPEE") return "emerald";
    if (code == "BPRE" || code == "PUVV") return "firered";
    if (code == "AXVE") return "ruby";
    if (code == "AXPE") return "sapphire";
    if (code == "AXVD") return "rubyde";
    if (code == "AXPD") return "sapphirede";
    if (code == "AXVF") return "rubyfr";
    if (code == "AXVS") return "rubyes";
    if (code == "AXVI") return "rubyit";
    if (code == "AXVJ") return "rubyjp";
    if (code == "AXPF") return "sapphirefr";
    if (code == "AXPS") return "sapphirees";
    if (code == "AXPI") return "sapphireit";
    if (code == "AXPJ") return "sapphirejp";
    return nullptr;
}

struct Game
{
    std::string rom;       // the ROM's path
    std::string ips;       // an IPS patch put on it first (Ultra Violet from FireRed), or ""
    std::string fileName;  // as shown under the name
    std::string key;       // its save is saves/<key>.sav
    std::string name;
    std::string sha1;      // of the ROM as played (after `ips`)
    std::string patch;     // the widescreen patch it takes, "" for none
    std::string why;       // why it has none
    uint32_t colour = 0xFF505A70;
    bool known = false;
    bool hasSave = false;
};

// A save the player put beside the ROM (or beside Ultra Violet's .ips), with the ROM's name.
inline std::string SaveBeside(const Game& g)
{
    const std::string& f = g.ips.empty() ? g.rom : g.ips;
    return f.substr(0, f.rfind('.')) + ".sav";
}

inline std::string Header(const std::vector<uint8_t>& rom, size_t off, size_t len)
{
    std::string s;
    for (size_t i = off; i < off + len && i < rom.size() && rom[i]; i++) s += (char)rom[i];
    return s;
}
inline bool LooksLikeGba(const std::vector<uint8_t>& rom)
{
    return rom.size() >= 0x200 && rom.size() <= (32u << 20) && rom[0xB2] == 0x96;
}
inline std::string KeyFromFile(const std::string& file)
{
    std::string k;
    for (char c : Lower(file.substr(0, file.rfind('.'))))
        k += isalnum((unsigned char)c) ? c : '-';
    return k.empty() ? "game" : k;
}

// What a ROM (as it will be played) is: one of ours, a hack of one (widescreen if the patch
// fits), or another game (played as it is).
inline void Identify(Game& g, const std::vector<uint8_t>& rom)
{
    g.sha1 = Sha1(rom);
    for (const Known& k : KnownGames())
        if (g.sha1 == k.sha1)
        {
            g.known = true;
            g.key = k.key;
            g.name = k.name;
            g.patch = k.patch;
            g.colour = k.colour;
            return;
        }
    g.key = KeyFromFile(g.fileName);
    g.name = g.fileName.substr(0, g.fileName.rfind('.'));
    for (char& c : g.name) c = (char)toupper((unsigned char)c);
    // A hack gets widescreen if it left alone everything a patch needs (whatever its code).
    std::string why;
    for (const WidePatch& p : kWidePatches)
        if (PatchFits(rom, p, why))
        {
            g.patch = p.name;
            g.colour = !strcmp(p.name, "emerald") ? 0xFF2A6E50 : !strncmp(p.name, "leafgreen", 9) ? 0xFF3E7A34
                     : !strncmp(p.name, "sapphire", 8) ? 0xFF2A4A8A : 0xFF8A3A30;
            return;
        }
    const std::string code = Header(rom, 0xAC, 4);
    const char* base = PatchForCode(code);
    const WidePatch* p = base ? FindPatch(base) : nullptr;
    if (!p)
        g.why = "NOT ONE OF THE WIDESCREEN GAMES";
    else if (code == "BPRE" && rom[0xBC] == 1)
        g.why = "FIRERED V1.1: NEEDS V1.0";
    else
    {
        PatchFits(rom, *p, why);
        g.why = why;
    }
}

// What the ROMs in the folder were found to be, kept in a file (saves/rom-list.txt): telling a
// game needs the whole ROM read, which takes seconds from an SD card. A line per ROM (or .ips
// with its base): what it was keyed on (path, size and time of the files), then the Game's
// fields, separated by tabs. Only the lines used in the last look are kept.
struct ScanCache
{
    std::string path;
    std::vector<std::pair<std::string, Game>> entries;
    std::vector<bool> used;
    bool changed = false;

    static std::string Stamp(const std::string& file)
    {
        struct stat st{};
        if (stat(file.c_str(), &st) != 0) return "";
        return file + "|" + std::to_string((long long)st.st_size) + "|" + std::to_string((long long)st.st_mtime);
    }
    void Load(const std::string& p)
    {
        path = p;
        std::vector<uint8_t> data;
        if (path.empty() || !ReadFile(path, data, 4 << 20)) return;
        std::string text(data.begin(), data.end()), line;
        size_t pos = 0;
        while (pos < text.size())
        {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            line = text.substr(pos, end - pos);
            pos = end + 1;
            std::vector<std::string> f;
            size_t a = 0;
            while (true)
            {
                const size_t t = line.find('\t', a);
                f.push_back(line.substr(a, t == std::string::npos ? std::string::npos : t - a));
                if (t == std::string::npos) break;
                a = t + 1;
            }
            if (f.size() != 11) continue;
            Game g;
            g.rom = f[1];
            g.ips = f[2];
            g.fileName = f[3];
            g.key = f[4];
            g.name = f[5];
            g.sha1 = f[6];
            g.patch = f[7];
            g.why = f[8];
            g.colour = (uint32_t)strtoul(f[9].c_str(), nullptr, 16);
            g.known = f[10] == "1";
            entries.push_back({ f[0], g });
            used.push_back(false);
        }
    }
    const Game* Find(const std::string& stamp)
    {
        if (stamp.empty()) return nullptr;
        for (size_t i = 0; i < entries.size(); i++)
            if (entries[i].first == stamp)
            {
                used[i] = true;
                return &entries[i].second;
            }
        return nullptr;
    }
    void Put(const std::string& stamp, const Game& g)
    {
        if (stamp.empty()) return;
        entries.push_back({ stamp, g });
        used.push_back(true);
        changed = true;
    }
    void Save()
    {
        for (bool u : used) changed |= !u;
        if (!changed || path.empty()) return;
        std::string out;
        auto clean = [](std::string v) {
            for (char& c : v)
                if (c == '\t' || c == '\n' || c == '\r') c = ' ';
            return v;
        };
        for (size_t i = 0; i < entries.size(); i++)
        {
            if (!used[i]) continue;
            const auto& e = entries[i];
            const Game& g = e.second;
            char colour[16];
            snprintf(colour, sizeof colour, "%08X", (unsigned)g.colour);
            out += clean(e.first) + "\t" + clean(g.rom) + "\t" + clean(g.ips) + "\t" + clean(g.fileName) + "\t" + clean(g.key) +
                   "\t" + clean(g.name) + "\t" + g.sha1 + "\t" + g.patch + "\t" + clean(g.why) + "\t" + colour + "\t" +
                   (g.known ? "1" : "0") + "\n";
        }
        WriteFile(path, std::vector<uint8_t>(out.begin(), out.end()));
        changed = false;
    }
};

// Every game in the roms folder: .gba files, and Ultra Violet's .ips with a FireRed v1.0 ROM.
inline std::vector<Game> Scan(const std::string& romsDir, const std::string& savesDir, const std::string& cacheFile = "")
{
    std::vector<std::string> gba, ips;
    if (DIR* d = opendir(romsDir.c_str()))
    {
        while (dirent* e = readdir(d))
        {
            const std::string n = e->d_name;
            if (EndsWith(n, ".gba") || EndsWith(n, ".agb") || EndsWith(n, ".bin")) gba.push_back(n);
            else if (EndsWith(n, ".ips")) ips.push_back(n);
        }
        closedir(d);
    }
    std::sort(gba.begin(), gba.end());
    std::sort(ips.begin(), ips.end());
    std::vector<Game> games;
    std::vector<std::string> fireRed10; // (bases for the .ips files)
    ScanCache cache;
    cache.Load(cacheFile);
    for (const std::string& n : gba)
    {
        Game g;
        g.rom = romsDir + "/" + n;
        g.fileName = n;
        const std::string stamp = ScanCache::Stamp(g.rom);
        if (const Game* c = cache.Find(stamp))
            g = *c;
        else
        {
            std::vector<uint8_t> rom;
            if (!ReadFile(g.rom, rom) || !LooksLikeGba(rom)) continue;
            Identify(g, rom);
            cache.Put(stamp, g);
        }
        if (g.key == "firered" && g.known) fireRed10.push_back(g.rom);
        games.push_back(g);
    }
    for (const std::string& n : ips)
    {
        // (A patch with each base: known from the list if neither file changed; "none" when
        // it gives no game we know.)
        bool done = false;
        for (const std::string& base : fireRed10)
        {
            const std::string stamp = ScanCache::Stamp(romsDir + "/" + n) + "+" + ScanCache::Stamp(base);
            if (const Game* c = cache.Find(stamp))
            {
                if (c->key != "none") games.push_back(*c);
                done = true;
                break;
            }
        }
        if (done) continue;
        std::vector<uint8_t> patch;
        if (!ReadFile(romsDir + "/" + n, patch, 16 << 20)) continue;
        for (const std::string& base : fireRed10)
        {
            const std::string stamp = ScanCache::Stamp(romsDir + "/" + n) + "+" + ScanCache::Stamp(base);
            std::vector<uint8_t> rom;
            if (!ReadFile(base, rom) || !ApplyIps(rom, patch.data(), patch.size())) continue;
            Game g;
            g.rom = base;
            g.ips = romsDir + "/" + n;
            g.fileName = n;
            g.sha1 = Sha1(rom);
            bool known = false;
            for (const Known& k : KnownGames()) known |= g.sha1 == k.sha1;
            if (!known)
            {
                // (only patches that give a game we know: anything else is the player's to make)
                Game none;
                none.key = "none";
                cache.Put(stamp, none);
                continue;
            }
            Identify(g, rom);
            cache.Put(stamp, g);
            games.push_back(g);
            break;
        }
    }
    cache.Save();
    // One of each: the same game twice (its .gba and its .ips) is listed once.
    std::vector<Game> out;
    for (const Game& g : games)
    {
        bool dup = false;
        for (const Game& o : out) dup |= o.sha1 == g.sha1;
        if (!dup) out.push_back(g);
    }
    for (Game& g : out)
        g.hasSave = Exists(savesDir + "/" + g.key + ".sav") || Exists(SaveBeside(g));
    // Two versions of one game (FireRed 1.0 and 1.1): each named with its version. (They share
    // the save, as the game's own versions do.)
    for (Game& g : out)
        for (const Game& o : out)
            if (&o != &g && g.known && o.known && o.key == g.key && o.patch != g.patch && g.name.find(" 1.") == std::string::npos)
                g.name += g.patch.size() > 2 && !g.patch.compare(g.patch.size() - 2, 2, "11") ? " 1.1" : " 1.0";
    // Ours first (Emerald, Ultra Violet, FireRed, LeafGreen), then hacks with widescreen, then the rest.
    auto rank = [](const Game& g) {
        if (g.known)
            for (size_t i = 0; i < KnownGames().size(); i++)
                if (g.key == KnownGames()[i].key) return (int)i;
        return g.patch.empty() ? 20 : 10;
    };
    std::stable_sort(out.begin(), out.end(), [&](const Game& a, const Game& b) { return rank(a) < rank(b); });
    return out;
}

// The ROM to play: read, its .ips put on, then the widescreen patch if it takes one.
// `beforeWide`, when set, receives the ROM after its own patch and before the widescreen one
// (what RetroAchievements identifies the game by).
inline bool Prepare(const Game& g, std::vector<uint8_t>& rom, std::string& error, std::vector<uint8_t>* beforeWide = nullptr)
{
    if (!ReadFile(g.rom, rom))
    {
        error = "CAN'T READ THE ROM";
        return false;
    }
    if (!g.ips.empty())
    {
        std::vector<uint8_t> p;
        if (!ReadFile(g.ips, p, 16 << 20) || !ApplyIps(rom, p.data(), p.size()))
        {
            error = "CAN'T READ ITS PATCH";
            return false;
        }
    }
    if (beforeWide) *beforeWide = rom;
    if (g.patch.empty()) return true;
    const WidePatch* p = FindPatch(g.patch);
    std::string why;
    if (!p || !PatchFits(rom, *p, why) || !ApplyIps(rom, p->ips, p->ipsSize))
    {
        error = "THE WIDESCREEN PATCH DOESN'T FIT: " + why;
        return false;
    }
    return true;
}

}

} // namespace gbaw
