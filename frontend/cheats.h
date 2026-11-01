#pragma once
// Cheats (Action Replay codes), run by melonDS's own engine (melonDS/src/AREngine.cpp) every
// time the DS's second processor answers the screen's vertical blank, as a real Action Replay
// does. They come from the cheats folder (beside the saves folder, or in the save's folder):
//
//   <game code>.txt   codes of your own: a line with the cheat's name, then its code lines
//                     ("92247612 00002801"); a line starting with # is a comment
//   <game code>.mch   a melonDS cheat file (as melonDS saves them)
//   usrcheat.dat      the R4 / DeSmuME cheat database: this game's codes are read from it
//
// Which are on is kept in <game code>-on.txt there (one name a line), so the game list's
// other games and a new database keep theirs. CHEATS in the pause menu lists the database's
// folders (a folder shows how many of its codes are on); opening one turns its codes on and off.
// Codes with no folder, such as a text file of your own, are listed above the folders.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#include "ARCodeFile.h"
#include "ARDatabaseDAT.h"
#include "AREngine.h"
#include "CRC32.h"

namespace Cheats
{

struct Cheat
{
    std::string name;     // its own name, in the menus' font (the folder is separate, below)
    std::string category; // the database folder it belongs to; empty when it has none
    std::string key;      // as kept in <code>-on.txt: where it came from and its full name
    std::vector<melonDS::u32> code;
    bool on = false;
    int group = -1;       // a folder of the database in which only one may be on (-1: none)
    std::string note;     // what the database says about it ("hold L+R", ...), in the menus' font
};

// Letters of a cheat's name beside ON / OFF. The cheats page's box is wide enough for this many
// (ui.h, the 260-wide box: 32 letters, a gap, and "< OFF >").
constexpr int LabelChars = 32;

// `s` cut to `max` letters, the last a full stop when it had to be cut.
inline std::string Fit(std::string s, size_t max)
{
    if (s.size() <= max) return s;
    if (max == 0) return "";
    s.resize(max - (max > 1 ? 1 : 0));
    while (!s.empty() && s.back() == ' ') s.pop_back();
    if (max > 1) s += '.';
    return s.empty() ? "." : s;
}

// A name in the menus' font: capitals, digits and : - + . / < > ! ( ), others as spaces, cut to
// `max` characters.
inline std::string Shown(const std::string& s, size_t max)
{
    std::string o;
    bool space = false;
    for (unsigned char c : s)
    {
        if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 32);
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr(":-+./<>!()", c);
        if (!ok || c == ' ')
        {
            if (!o.empty() && !space) o += ' ';
            space = true;
            continue;
        }
        o += (char)c;
        space = false;
    }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    if (o.size() > max) o = o.substr(0, max - 1) + ".";
    return o.empty() ? "?" : o;
}

inline bool Matches(const Cheat& cheat, const std::string& query)
{
    std::string hay=cheat.name+" "+cheat.category+" "+cheat.note, needle=query;
    for(char& c:hay)c=(char)std::toupper((unsigned char)c);
    for(char& c:needle)c=(char)std::toupper((unsigned char)c);
    size_t at=0;
    while(at<needle.size())
    {
        const size_t start=needle.find_first_not_of(' ',at);
        if(start==std::string::npos)break;
        const size_t end=needle.find(' ',start);
        if(hay.find(needle.substr(start,end-start))==std::string::npos)return false;
        if(end==std::string::npos)break;
        at=end+1;
    }
    return true;
}
inline std::vector<std::string> Lines(const std::string& text, size_t width=38)
{
    std::vector<std::string> lines;
    size_t at=0;
    while(at<text.size())
    {
        while(at<text.size() && text[at]==' ')at++;
        if(at==text.size())break;
        size_t end=std::min(text.size(),at+width);
        if(end<text.size())
        {
            const size_t space=text.rfind(' ',end);
            if(space!=std::string::npos && space>at)end=space;
        }
        lines.push_back(text.substr(at,end-at));at=end;
    }
    return lines;
}

struct List
{
    // A database folder, in the order the file lists them. Codes with an empty category
    // (a text file of your own, or a code sitting outside any folder) are not a folder here:
    // the menu lists those on the folder page itself.
    struct Folder
    {
        std::string name;
        std::string note; // what the database says about the folder, under the menu
    };

    std::vector<Cheat> items;
    std::vector<Folder> folders;
    std::string folder, code;
    std::vector<std::string> messages; // Player-readable failures and missing game entries.
    std::vector<std::string> sources; // what was read, for game.log

    static bool Exists(const std::string& p)
    {
        struct stat st;
        return stat(p.c_str(), &st) == 0;
    }

    // Two words of hex ("92247612 00002801", also run together) from a line; false for anything else.
    static bool CodeLine(const std::string& l, std::vector<melonDS::u32>& out)
    {
        std::string hex;
        for (char c : l)
        {
            if (isxdigit((unsigned char)c)) hex += c;
            else if (c != ' ' && c != '\t' && c != '\r') return false;
        }
        if (hex.empty() || hex.size() % 16) return false;
        for (size_t i = 0; i < hex.size(); i += 8) out.push_back((melonDS::u32)strtoul(hex.substr(i, 8).c_str(), nullptr, 16));
        return true;
    }

    void Add(const std::string& name, const std::string& key, const std::vector<melonDS::u32>& code, int group = -1,
             const std::string& note = "", const std::string& category = "", const std::string& catNote = "")
    {
        if (code.empty()) return;
        Cheat c;
        c.name = Shown(name, 1024);
        c.key = key;
        c.code = code;
        c.group = group;
        // (Shown under the menu, a few lines at most; the whole name first when the row has to
        // shorten it. The row keeps the code's own name, not "folder: name": that prefix used up
        // the row, so every code in a folder looked the same.)
        if (!note.empty() && Shown(note, 4096) != "?") c.note = Shown(note, 4096);
        if (c.name.size() > (size_t)LabelChars)
            c.note = c.note.empty() ? c.name : c.name + ": " + c.note;
        if (!category.empty())
        {
            const std::string cat = Shown(category, 256);
            if (cat != "?")
            {
                c.category = cat;
                bool found = false;
                for (const Folder& f : folders)
                    if (f.name == cat) { found = true; break; }
                if (!found)
                {
                    Folder f;
                    f.name = cat;
                    if (!catNote.empty() && Shown(catNote, 160) != "?") f.note = Shown(catNote, 160);
                    folders.push_back(f);
                }
            }
        }
        items.push_back(c);
    }

    // What the folder row shows on the right: the one code that is on, when its name is short,
    // otherwise how many are on.
    std::string FolderStatus(const std::string& name) const
    {
        const Cheat* one = nullptr;
        int n = 0;
        for (const Cheat& c : items)
            if (c.category == name && c.on) { one = &c; n++; }
        if (n == 0) return "";
        if (n == 1 && one->name.size() <= 12) return one->name;
        return std::to_string(n) + " ON";
    }

    void Load(const std::string& cheatFolder, const std::string& gameCode, const unsigned char* romHeader)
    {
        items.clear();
        folders.clear();
        sources.clear();
        folder = cheatFolder;
        code = gameCode;
        // Codes of one's own: a name line, then code lines.
        const std::string txt = folder + "/" + code + ".txt";
        if (FILE* f = fopen(txt.c_str(), "r"))
        {
            char buf[512];
            std::string name;
            std::vector<melonDS::u32> lines;
            int unnamed = 0;
            auto flush = [&]() {
                if (!lines.empty()) Add(name.empty() ? "CHEAT " + std::to_string(++unnamed) : name, "txt:" + name, lines);
                lines.clear();
            };
            while (fgets(buf, sizeof buf, f))
            {
                std::string l = buf;
                while (!l.empty() && (l.back() == '\n' || l.back() == '\r' || l.back() == ' ')) l.pop_back();
                size_t a = l.find_first_not_of(" \t");
                if (a == std::string::npos || l[a] == '#' || l[a] == ';') continue;
                l = l.substr(a);
                if (CodeLine(l, lines)) continue;
                flush();
                name = l;
                if (name.size() > 2 && name.front() == '[' && name.back() == ']') name = name.substr(1, name.size() - 2);
            }
            flush();
            fclose(f);
            sources.push_back(txt);
        }
        // melonDS's own cheat files.
        const std::string mch = folder + "/" + code + ".mch";
        if (Exists(mch))
        {
            melonDS::ARCodeFile file(mch);
            if (!file.Error)
            {
                for (const melonDS::ARCode& c : file.GetCodes())
                {
                    const std::string cat = (c.Parent && !c.Parent->Name.empty()) ? c.Parent->Name : "";
                    Add(c.Name, "mch:" + c.Name, c.Code, -1, c.Description, cat);
                }
                sources.push_back(mch);
            }
            else messages.push_back("CANNOT READ THE MELONDS CHEAT FILE.");
        }
        // The R4 / DeSmuME database: this game's entry (the one made for this very ROM if there
        // is one, by the checksum of its header, as melonDS picks it).
        const std::string dat = folder + "/usrcheat.dat";
        if (Exists(dat) && romHeader)
        {
            melonDS::ARDatabaseDAT db(dat);
            const melonDS::u32 gc = (melonDS::u32)romHeader[12] | romHeader[13] << 8 | romHeader[14] << 16 | (melonDS::u32)romHeader[15] << 24;
            if(db.Error)messages.push_back("USRCHEAT.DAT COULD NOT BE READ. CHECK THAT IT IS A VALID R4 DATABASE.");
            else if(!db.FindGameCode(gc))messages.push_back("USRCHEAT.DAT HAS NO ENTRY FOR THIS GAME CODE: "+code);
            if (!db.Error && db.FindGameCode(gc))
            {
                melonDS::ARDatabaseEntryList entries = db.GetEntriesByGameCode(gc);
                const melonDS::u32 sum = ~melonDS::CRC32(romHeader, 0x200, 0);
                const melonDS::ARDatabaseEntry* pick = entries.empty() ? nullptr : &entries[0];
                for (const auto& e : entries)
                    if (e.Checksum == sum) pick = &e;
                if (pick)
                {
                    int groups = 0;
                    for (const auto& item : pick->RootCat.Children)
                    {
                        if (std::holds_alternative<melonDS::ARCode>(item))
                        {
                            const auto& c = std::get<melonDS::ARCode>(item);
                            Add(c.Name, "dat:" + c.Name, c.Code, -1, c.Description);
                        }
                        else
                        {
                            const auto& cat = std::get<melonDS::ARCodeCat>(item);
                            const int g = cat.OnlyOneCodeEnabled ? groups++ : -1;
                            for (const auto& sub : cat.Children)
                                if (std::holds_alternative<melonDS::ARCode>(sub))
                                {
                                    const auto& c = std::get<melonDS::ARCode>(sub);
                                    Add(c.Name, "dat:" + cat.Name + "/" + c.Name, c.Code, g,
                                        c.Description.empty() ? cat.Description : c.Description, cat.Name, cat.Description);
                                }
                        }
                    }
                    sources.push_back(dat + " (" + pick->Name + ")");
                }
            }
        }
        // Which are on.
        if (FILE* f = fopen(OnFile().c_str(), "r"))
        {
            char buf[512];
            while (fgets(buf, sizeof buf, f))
            {
                std::string l = buf;
                while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
                for (Cheat& c : items)
                    if (c.key == l) c.on = true;
            }
            fclose(f);
        }
    }

    std::string OnFile() const { return folder + "/" + code + "-on.txt"; }

    // Turns cheat i on or off (another of its folder off, if only one may be on), and keeps it.
    void Toggle(size_t i)
    {
        if (i >= items.size()) return;
        items[i].on = !items[i].on;
        if (items[i].on && items[i].group >= 0)
            for (size_t j = 0; j < items.size(); j++)
                if (j != i && items[j].group == items[i].group) items[j].on = false;
        // (The folder made if need be: one level, beside the saves.)
#ifdef _WIN32
        _mkdir(folder.c_str());
#else
        mkdir(folder.c_str(), 0755);
#endif
        if (FILE* f = fopen(OnFile().c_str(), "w"))
        {
            for (const Cheat& c : items)
                if (c.on) fprintf(f, "%s\n", c.key.c_str());
            fclose(f);
        }
    }

    int On() const
    {
        int n = 0;
        for (const Cheat& c : items) n += c.on;
        return n;
    }

    // The ones that are on, to melonDS's engine.
    void Apply(melonDS::AREngine& engine) const
    {
        engine.Cheats.clear();
        for (const Cheat& c : items)
        {
            if (!c.on) continue;
            melonDS::ARCode a;
            a.Parent = nullptr;
            a.Name = c.key;
            a.Enabled = true;
            a.Code = c.code;
            engine.Cheats.push_back(a);
        }
    }
};

// The cheats folder for a save: beside a folder called saves, otherwise in the save's own folder.
inline std::string FolderFor(const std::string& save)
{
    const size_t slash = save.find_last_of("/\\");
    const std::string dir = slash == std::string::npos ? "." : save.substr(0, slash);
    const size_t s2 = dir.find_last_of("/\\");
    const std::string last = s2 == std::string::npos ? dir : dir.substr(s2 + 1);
    if (last == "saves") return (s2 == std::string::npos ? std::string(".") : dir.substr(0, s2)) + "/cheats";
    return dir + "/cheats";
}

} // namespace Cheats
