#pragma once
// The cheats built into Fulldex for the GBA games: 15 for Pokemon Emerald, FireRed and LeafGreen
// (1.0 and 1.1) and Ultra Violet, all off until the player turns one on in CHEATS in the game's
// pause menu (Pokemon GBA Widescreen's, frontend/main.cpp there). Which are on is kept per game in
// the cheats folder beside the saves (<game>-on.txt, a name a line), as the DS games' are.
//
// They are not Action Replay or Code Breaker codes: each is done here on the game's own memory,
// at the addresses of pret's decompilations (pokeemerald and pokefirered: the symbols branch's
// .sym files, commits dba968c6 and 3b401eaf, and include/global.h for the save's layout), so the
// same 15 work the same way in every one. Three change a few bytes of the game's code while they
// are on and put them back when turned off (walk through walls, always catch, double EXP); each
// is only offered when those bytes are the ones expected. With none on, nothing here writes to
// the game (Frame returns at once).
//
// Ultra Violet 1.22 (LSA's FireRed hack, game code PUVV) has FireRed 1.0's code and memory where
// these look (its three code changes are checked byte for byte, like the others).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

namespace GbaCheats
{

// The running GBA, as the player gives it: reads and writes by GBA address (work RAM, and the
// ROM for the code changes).
struct Bus
{
    virtual ~Bus() {}
    virtual uint8_t R8(uint32_t a) = 0;
    virtual uint16_t R16(uint32_t a) = 0;
    virtual uint32_t R32(uint32_t a) = 0;
    virtual void W8(uint32_t a, uint8_t v) = 0;
    virtual void W16(uint32_t a, uint16_t v) = 0;
    virtual void W32(uint32_t a, uint32_t v) = 0;
};

enum Id { WALLS, MONEY, CANDY, MASTER, SHINY, CATCH, HP, PP, EXP, NOWILD, EGGS, BADGES, TMS, DEX, FLY, COUNT };

struct Info { const char* name; const char* note; };
inline const Info& About(int id)
{
    // (Names as the menus show them: capitals; a note under the menu for the chosen one.)
    static const Info k[COUNT] = {
        { "WALK THROUGH WALLS", "HOLD B WHILE YOU WALK. NOT ONTO WATER OR OFF THE MAP." },
        { "MAX MONEY", "999999, KEPT THERE WHILE ON." },
        { "99 RARE CANDIES", "IN THE ITEMS POCKET, KEPT AT 99 WHILE ON." },
        { "99 MASTER BALLS", "IN THE POKE BALLS POCKET, KEPT AT 99 WHILE ON." },
        { "WILD SHINY ENCOUNTERS", "EVERY WILD POKEMON IS SHINY (NOT UNOWN)." },
        { "ALWAYS CATCH POKEMON", "ANY BALL CATCHES ANY WILD POKEMON." },
        { "INFINITE HP IN BATTLE", "YOUR POKEMON KEEP FULL HP IN BATTLE." },
        { "INFINITE PP", "YOUR MOVES NEVER LOSE PP IN BATTLE." },
        { "DOUBLE EXP", "TWICE THE EXP FROM EVERY BATTLE." },
        { "NO WILD ENCOUNTERS", "NONE WHEN WALKING OR SURFING. FISHING STILL WORKS." },
        { "FAST EGG HATCH", "EGGS IN YOUR PARTY HATCH ON THE NEXT STEP." },
        { "ALL BADGES", "ALL 8. THEY STAY AFTER YOU TURN IT OFF." },
        { "ALL TMS AND HMS", "99 OF EVERY TM AND ALL 8 HMS (AND THE TM CASE), KEPT WHILE ON." },
        { "COMPLETE POKEDEX", "ALL 386 SEEN AND CAUGHT, NATIONAL DEX ON. STAYS." },
        { "FLY ANYWHERE", "EVERY TOWN AND CITY ON THE FLY MAP. STAYS." },
    };
    return k[id];
}

// Where things are, by family (Emerald; FireRed and LeafGreen, all versions: their RAM is the same).
struct Layout
{
    // RAM (the .sym files)
    uint32_t sb1Ptr, sb2Ptr;          // gSaveBlock1Ptr, gSaveBlock2Ptr: the save blocks move about
    uint32_t party, partyCount;       // gPlayerParty (6 x 100 bytes), gPlayerPartyCount
    uint32_t enemyParty;              // gEnemyParty
    uint32_t battleMons, battlers;    // gBattleMons (4 x 0x58), gBattlersCount
    uint32_t battlerParty;            // gBattlerPartyIndexes (u16 x 4)
    uint32_t battleType;              // gBattleTypeFlags
    uint32_t mainBits;                // gMain + 0x439: oamLoadDisabled:1, inBattle:1, ...
    uint32_t wildOff;                 // sWildEncountersDisabled
    // in SaveBlock1 (global.h)
    uint16_t money, items, balls, tmhm, flags, vars, seen1, seen2, daycareStep;
    uint8_t itemsN, ballsN, tmhmN;
    uint16_t keyItems, keyItemsN, tmCase; // FireRed and LeafGreen keep TMs in the TM Case (a key item)
    // in SaveBlock2
    uint16_t key, dexMagic;           // encryptionKey (money, bag quantities), pokedex.nationalMagic
    uint8_t dexMagicValue;
    uint16_t natDexVar, natDexValue, natDexFlag; // EnableNationalPokedex
    uint16_t badge1, flyFirst, flyLast;          // FLAG_BADGE01_GET; the fly map's town flags
    // the code changes: where in each function, and what is there
    uint16_t collisionFirst;          // MapGridGetCollisionAt's first instruction (push)
    uint16_t ballOff, expBneOff, expMulOff;
};

inline const Layout& Emerald()
{
    static const Layout l = {
        0x03005d8c, 0x03005d90, 0x020244ec, 0x020244e9, 0x02024744, 0x02024084, 0x0202406c, 0x0202406e,
        0x02022fec, 0x030022c0 + 0x439, 0x02038c00,
        0x490, 0x560, 0x650, 0x690, 0x1270, 0x139C, 0x988, 0x3B24, 0x3030 + 0x11C,
        30, 16, 64,
        0x5D8, 30, 0, // (Emerald: its own TMs and HMs pocket, no case)
        0xAC, 0x18 + 2, 0xDA,
        0x4046, 0x0302, 0x860 + 0x36,
        0x860 + 0x7, 0x860 + 0xF, 0x860 + 0x1E, // FLAG_VISITED_LITTLEROOT_TOWN .. EVER_GRANDE_CITY
        0xb510, 0x266, 0x350, 0x356,
    };
    return l;
}
inline const Layout& FireRedLeafGreen()
{
    static const Layout l = {
        0x03005008, 0x0300500c, 0x02024284, 0x02024029, 0x0202402c, 0x02023be4, 0x02023bcc, 0x02023bce,
        0x02022b4c, 0x030030f0 + 0x439, 0x020386dc,
        0x290, 0x310, 0x430, 0x464, 0xEE0, 0x1000, 0x5F8, 0x3A18, 0x2F80 + 0x11A,
        42, 13, 58,
        0x3B8, 30, 364, // ITEM_TM_CASE
        0xF20, 0x18 + 3, 0xB9,
        0x404E, 0x6258, 0x800 + 0x40,
        0x800 + 0x20, 0x800 + 0x90, 0x800 + 0xA1, // FLAG_WORLD_MAP_PALLET_TOWN .. SEVEN_ISLAND
        0xb5f0, 0x28a, 0x354, 0x35a,
    };
    return l;
}

// A version of a game: the ROM's game code and version byte (0xBC), and its three functions.
struct Version
{
    const char* code;
    int rev;
    const char* name;
    bool emerald;
    uint32_t collision, ballThrow, getExp; // MapGridGetCollisionAt, Cmd_handleballthrow, Cmd_getexp
};
inline const std::vector<Version>& Versions()
{
    static const std::vector<Version> v = {
        { "BPEE", 0, "EMERALD", true, 0x080881b0, 0x08056300, 0x0804a32c },
        { "BPRE", 0, "FIRERED 1.0", false, 0x08058dc4, 0x0802d434, 0x08021a68 },
        { "BPRE", 1, "FIRERED 1.1", false, 0x08058dd8, 0x0802d448, 0x08021a7c },
        { "BPGE", 0, "LEAFGREEN 1.0", false, 0x08058dc4, 0x0802d434, 0x08021a68 },
        { "BPGE", 1, "LEAFGREEN 1.1", false, 0x08058dd8, 0x0802d448, 0x08021a7c },
        { "PUVV", 0, "ULTRA VIOLET", false, 0x08058dc4, 0x0802d434, 0x08021a68 },
    };
    return v;
}

constexpr uint16_t kMasterBall = 1, kRareCandy = 68, kTm01 = 289, kHm01 = 339, kUnown = 201;
constexpr uint32_t kTrainer = 1u << 3, kDouble = 1u << 0;
constexpr uint32_t kB = 1u << 1; // the GBA's B button in the keys given to Frame

// ---------------------------------------------------------------------------------------------
// A Pokemon as the game keeps it (pokemon.h: BoxPokemon, 80 bytes, its four 12-byte parts in an
// order and with a key both made from its personality). Load decrypts, Store puts it back.

inline uint32_t Get32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
inline uint16_t Get16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
inline void Put32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
inline void Put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

// The parts (0 growth, 1 attacks, 2 EVs and condition, 3 misc) in the order kept, by personality % 24.
inline const uint8_t* Order(uint32_t personality)
{
    static const uint8_t k[24][4] = {
        {0,1,2,3},{0,1,3,2},{0,2,1,3},{0,2,3,1},{0,3,1,2},{0,3,2,1},
        {1,0,2,3},{1,0,3,2},{1,2,0,3},{1,2,3,0},{1,3,0,2},{1,3,2,0},
        {2,0,1,3},{2,0,3,1},{2,1,0,3},{2,1,3,0},{2,3,0,1},{2,3,1,0},
        {3,0,1,2},{3,0,2,1},{3,1,0,2},{3,1,2,0},{3,2,0,1},{3,2,1,0},
    };
    return k[personality % 24];
}

struct Mon
{
    uint32_t addr = 0, pid = 0, otid = 0;
    uint8_t raw[80] = {};
    uint8_t sec[48] = {}; // the four parts, decrypted, in the order kept

    // False for an empty slot, a bad egg, or data whose checksum is wrong (left alone).
    bool Load(Bus& b, uint32_t a)
    {
        addr = a;
        for (int i = 0; i < 20; i++) Put32(raw + 4 * i, b.R32(a + 4 * i));
        pid = Get32(raw);
        otid = Get32(raw + 4);
        const uint32_t key = pid ^ otid;
        for (int i = 0; i < 12; i++) Put32(sec + 4 * i, Get32(raw + 32 + 4 * i) ^ key);
        if (!(raw[19] & 2) || (raw[19] & 1)) return false; // hasSpecies, isBadEgg
        return Sum() == Get16(raw + 28);
    }
    uint16_t Sum() const
    {
        uint16_t s = 0;
        for (int i = 0; i < 24; i++) s = (uint16_t)(s + Get16(sec + 2 * i));
        return s;
    }
    uint8_t* Part(int type)
    {
        const uint8_t* o = Order(pid);
        for (int i = 0; i < 4; i++)
            if (o[i] == type) return sec + 12 * i;
        return sec;
    }
    uint16_t Species() { return Get16(Part(0)); }
    bool IsEgg() { return (Get32(Part(3) + 4) >> 30) & 1; }
    // Writes back what changed (the checksum made again; the parts encrypted with the key).
    void Store(Bus& b)
    {
        Put16(raw + 28, Sum());
        Put32(raw, pid);
        const uint32_t key = pid ^ otid;
        for (int i = 0; i < 12; i++) Put32(raw + 32 + 4 * i, Get32(sec + 4 * i) ^ key);
        for (int i = 0; i < 20; i++)
        {
            const uint32_t v = Get32(raw + 4 * i);
            if (b.R32(addr + 4 * i) != v) b.W32(addr + 4 * i, v);
        }
    }
    // Shiny (gen 3: trainer ID ^ secret ID ^ the personality's two halves < 8), keeping what the
    // personality decides that a player sees: gender (its low byte) and nature (personality % 25).
    // The ability is kept apart (its bit in the misc part). Unown is left alone: its letter is
    // made from the personality. False when it already is, or can't be.
    bool MakeShiny()
    {
        const uint16_t sp = Species();
        if (sp == 0 || sp == kUnown) return false;
        const uint32_t tsv = (otid >> 16) ^ (otid & 0xFFFF);
        if (((pid >> 16) ^ (pid & 0xFFFF) ^ tsv) < 8) return false;
        for (uint32_t hiByte = 0; hiByte < 256; hiByte++)
            for (uint32_t s = 0; s < 8; s++)
            {
                const uint32_t lo = (pid & 0xFF) | hiByte << 8;
                const uint32_t hi = (tsv ^ lo ^ s) & 0xFFFF;
                const uint32_t np = hi << 16 | lo;
                if (np % 25 != pid % 25) continue;
                uint8_t parts[4][12];
                const uint8_t* oo = Order(pid);
                for (int i = 0; i < 4; i++) memcpy(parts[oo[i]], sec + 12 * i, 12);
                const uint8_t* no = Order(np);
                for (int i = 0; i < 4; i++) memcpy(sec + 12 * i, parts[no[i]], 12);
                pid = np;
                return true;
            }
        return false;
    }
};

// ---------------------------------------------------------------------------------------------

class Engine
{
public:
    // The game that has just started: its ROM's game code (4 letters at 0xAC) and version byte
    // (0xBC). `known`: the player knows the ROM (by its checksum): only those get cheats, so an
    // unknown hack with one of these codes doesn't have its memory changed by guesswork (Ultra
    // Violet is known). Reads the bytes the code changes would change. False: no cheats for it.
    bool Start(Bus& b, const std::string& code, int rev, bool known)
    {
        ver = nullptr;
        for (bool& o : on) o = false;
        for (bool& a : applied) a = false;
        for (bool& h : has) h = false;
        if (!known) return false;
        for (const Version& v : Versions())
            if (code == v.code && rev == v.rev) ver = &v;
        if (!ver) return false;
        lay = ver->emerald ? &Emerald() : &FireRedLeafGreen();
        for (bool& h : has) h = true;
        // The code changes: offered only where the game's code is as expected.
        has[WALLS] = b.R16(ver->collision) == lay->collisionFirst && b.R16(ver->collision + 2) == 0x1c02;
        has[CATCH] = b.R16(ver->ballThrow + lay->ballOff) == 0xd92f;                // bls (odds <= 254)
        has[EXP] = b.R16(ver->getExp + lay->expBneOff) == 0xd107                   // bne (no Lucky Egg)
                   && b.R16(ver->getExp + lay->expMulOff) == 0x2096;               // movs r0, #150
        Reset();
        return true;
    }
    bool Active() const { return ver != nullptr; }
    const char* GameName() const { return ver ? ver->name : ""; }
    bool Has(int id) const { return ver && id >= 0 && id < COUNT && has[id]; }
    bool On(int id) const { return Has(id) && on[id]; }
    int OnCount() const
    {
        int n = 0;
        for (int i = 0; i < COUNT; i++) n += On(i);
        return n;
    }
    void Set(int id, bool v)
    {
        if (!Has(id)) return;
        // (Turned on mid-battle: that battle isn't taken for one just begun.)
        if (v && !on[id]) lastInBattle = -1;
        on[id] = v;
    }

    // After the game was reset or a save state loaded: what was seen of it is forgotten.
    void Reset()
    {
        lastInBattle = -1;
        for (int i = 0; i < 4; i++)
        {
            lastHp[i] = 0;
            lastPid[i] = 0;
            for (int m = 0; m < 4; m++) lastPP[i][m] = -1;
        }
    }

    // Which are on, by name, a line each.
    void Load(const std::string& file)
    {
        if (FILE* f = fopen(file.c_str(), "r"))
        {
            char buf[128];
            while (fgets(buf, sizeof buf, f))
            {
                std::string l = buf;
                while (!l.empty() && (l.back() == '\n' || l.back() == '\r' || l.back() == ' ')) l.pop_back();
                for (int i = 0; i < COUNT; i++)
                    if (l == About(i).name) Set(i, true);
            }
            fclose(f);
        }
    }
    // (The file is only made once a cheat has been turned on; with none on it is emptied, not
    // removed. Its folder is made if need be: one level, beside the saves.)
    bool Save(const std::string& file) const
    {
        const size_t slash = file.find_last_of("/\\");
        if (slash != std::string::npos)
        {
#ifdef _WIN32
            _mkdir(file.substr(0, slash).c_str());
#else
            mkdir(file.substr(0, slash).c_str(), 0755);
#endif
        }
        FILE* f = fopen(file.c_str(), "w");
        if (!f) return false;
        for (int i = 0; i < COUNT; i++)
            if (on[i]) fprintf(f, "%s\n", About(i).name);
        fclose(f);
        return true;
    }

    // After each emulated frame, with the GBA keys that frame was given.
    void Frame(Bus& b, uint32_t keys)
    {
        if (!ver) return;
        bool any = false;
        for (int i = 0; i < COUNT; i++) any |= on[i] || applied[i];
        if (!any && !wildSet) return;

        // The code changes: in while wanted, out as soon as not.
        Code(b, WALLS, on[WALLS] && (keys & kB), { { ver->collision, lay->collisionFirst, 0x2000 },   // movs r0, #0
                                                    { ver->collision + 2, 0x1c02, 0x4770 } });       // bx lr
        Code(b, CATCH, on[CATCH], { { ver->ballThrow + lay->ballOff, 0xd92f, 0x46c0 } });            // nop: caught
        Code(b, EXP, on[EXP], { { ver->getExp + lay->expBneOff, 0xd107, 0x46c0 },                    // as with a Lucky Egg,
                                { ver->getExp + lay->expMulOff, 0x2096, 0x20c8 } });                 // x200/100, not x150/100

        // No wild encounters: the game's own switch for that (DisableWildEncounters), held on.
        if (on[NOWILD])
        {
            if (b.R8(lay->wildOff) != 1) b.W8(lay->wildOff, 1);
            wildSet = true;
        }
        else if (wildSet)
        {
            b.W8(lay->wildOff, 0);
            wildSet = false;
        }

        const uint32_t sb1 = b.R32(lay->sb1Ptr), sb2 = b.R32(lay->sb2Ptr);
        // (Before the save blocks are set up, at boot: nothing in them yet.)
        const bool saveOk = InEwram(sb1, 0x3D00) && InEwram(sb2, 0xF30);
        if (saveOk)
        {
            const uint32_t key = b.R32(sb2 + lay->key);
            if (on[MONEY] && (b.R32(sb1 + lay->money) ^ key) != 999999) b.W32(sb1 + lay->money, 999999 ^ key);
            if (on[CANDY]) Keep(b, sb1 + lay->items, lay->itemsN, kRareCandy, 99, (uint16_t)key);
            if (on[MASTER]) Keep(b, sb1 + lay->balls, lay->ballsN, kMasterBall, 99, (uint16_t)key);
            if (on[TMS] && lay->tmCase) Keep(b, sb1 + lay->keyItems, lay->keyItemsN, lay->tmCase, 1, (uint16_t)key);
            if (on[TMS])
                for (int i = 0; i < 58 && i < lay->tmhmN; i++)
                {
                    const uint32_t s = sb1 + lay->tmhm + 4 * i;
                    const uint16_t id = (uint16_t)(kTm01 + i), q = (uint16_t)((id >= kHm01 ? 1 : 99) ^ (uint16_t)key);
                    if (b.R16(s) != id) b.W16(s, id);
                    if (b.R16(s + 2) != q) b.W16(s + 2, q);
                }
            if (on[BADGES])
                for (int i = 0; i < 8; i++) SetFlag(b, sb1, lay->badge1 + i);
            if (on[FLY])
                for (int f = lay->flyFirst; f <= lay->flyLast; f++) SetFlag(b, sb1, f);
            if (on[DEX])
            {
                for (int i = 0; i < 386; i++)
                {
                    const uint32_t o = (uint32_t)i / 8;
                    const uint8_t bit = (uint8_t)(1 << (i & 7));
                    SetBit(b, sb2 + 0x28 + o, bit); // pokedex.owned
                    SetBit(b, sb2 + 0x5C + o, bit); // pokedex.seen
                    SetBit(b, sb1 + lay->seen1 + o, bit);
                    SetBit(b, sb1 + lay->seen2 + o, bit);
                }
                if (b.R8(sb2 + lay->dexMagic) != lay->dexMagicValue) b.W8(sb2 + lay->dexMagic, lay->dexMagicValue);
                const uint32_t var = sb1 + lay->vars + 2 * (lay->natDexVar - 0x4000);
                if (b.R16(var) != lay->natDexValue) b.W16(var, lay->natDexValue);
                SetFlag(b, sb1, lay->natDexFlag);
            }
            if (on[EGGS]) Eggs(b, sb1);
        }

        const bool inBattle = (b.R8(lay->mainBits) & 2) != 0;
        const uint32_t type = b.R32(lay->battleType);
        if (on[SHINY] && inBattle && lastInBattle == 0 && !(type & kTrainer))
        {
            // A wild battle has just begun: its Pokemon made, not yet shown.
            for (int i = 0; i < ((type & kDouble) ? 2 : 1); i++)
            {
                Mon m;
                if (m.Load(b, lay->enemyParty + 100 * i) && !m.IsEgg() && m.MakeShiny()) m.Store(b);
            }
        }
        if (inBattle && lastInBattle == 0) Reset(); // (fresh for this battle)
        if (inBattle && (on[HP] || on[PP])) Battle(b);
        lastInBattle = inBattle ? 1 : 0;
    }

    // For tests only: `qty` of `item` put in the bag (a ball in the Poke Balls pocket, anything
    // else in the Items pocket), as a player would have bought them.
    void Give(Bus& b, uint16_t item, uint16_t qty)
    {
        if (!ver) return;
        const uint32_t sb1 = b.R32(lay->sb1Ptr), sb2 = b.R32(lay->sb2Ptr);
        if (!InEwram(sb1, 0x3D00) || !InEwram(sb2, 0xF30)) return;
        const uint16_t key = (uint16_t)b.R32(sb2 + lay->key);
        if (item >= 1 && item <= 12) Keep(b, sb1 + lay->balls, lay->ballsN, item, qty, key);
        else Keep(b, sb1 + lay->items, lay->itemsN, item, qty, key);
    }

    // One line about the game's state, for tests (and game.log).
    std::string Report(Bus& b)
    {
        if (!ver) return "no cheats for this game";
        const uint32_t sb1 = b.R32(lay->sb1Ptr), sb2 = b.R32(lay->sb2Ptr);
        if (!InEwram(sb1, 0x3D00) || !InEwram(sb2, 0xF30)) return "save blocks not set up";
        const uint32_t key = b.R32(sb2 + lay->key);
        auto qty = [&](uint32_t pocket, int n, uint16_t item) {
            for (int i = 0; i < n; i++)
                if (b.R16(pocket + 4 * i) == item) return (int)(uint16_t)(b.R16(pocket + 4 * i + 2) ^ (uint16_t)key);
            return 0;
        };
        int tms = 0, badges = 0, fly = 0, owned = 0;
        const int tmCase = lay->tmCase ? qty(sb1 + lay->keyItems, lay->keyItemsN, lay->tmCase) : 1;
        for (int i = 0; i < lay->tmhmN; i++) tms += b.R16(sb1 + lay->tmhm + 4 * i) >= kTm01;
        for (int i = 0; i < 8; i++) badges += Flag(b, sb1, lay->badge1 + i);
        for (int f = lay->flyFirst; f <= lay->flyLast; f++) fly += Flag(b, sb1, f);
        for (int i = 0; i < 386; i++) owned += (b.R8(sb2 + 0x28 + i / 8) >> (i & 7)) & 1;
        char s[640];
        int n = snprintf(s, sizeof s, "%s pos %d,%d money %u candy %d master %d tmhm %d tmcase %d badges %d fly %d owned %d natdex %d wildoff %d battle %d",
                         ver->name, (int16_t)b.R16(sb1), (int16_t)b.R16(sb1 + 2), b.R32(sb1 + lay->money) ^ key, qty(sb1 + lay->items, lay->itemsN, kRareCandy),
                         qty(sb1 + lay->balls, lay->ballsN, kMasterBall), tms, tmCase, badges, fly, owned,
                         b.R8(sb2 + lay->dexMagic) == lay->dexMagicValue && Flag(b, sb1, lay->natDexFlag), b.R8(lay->wildOff),
                         (b.R8(lay->mainBits) & 2) ? 1 : 0);
        const int count = b.R8(lay->partyCount);
        for (int i = 0; i < count && i < 6 && n < (int)sizeof s - 40; i++)
        {
            Mon m;
            const uint32_t a = lay->party + 100 * i;
            if (!m.Load(b, a)) continue;
            n += snprintf(s + n, sizeof s - n, " | #%d sp %d hp %d/%d exp %u%s", i, m.Species(), b.R16(a + 86), b.R16(a + 88),
                          Get32(m.Part(0) + 4), m.IsEgg() ? (" egg " + std::to_string(m.Part(0)[9])).c_str() : "");
        }
        Mon e;
        if ((b.R8(lay->mainBits) & 2) && e.Load(b, lay->enemyParty) && n < (int)sizeof s - 60)
        {
            const uint32_t tsv = (e.otid >> 16) ^ (e.otid & 0xFFFF);
            n += snprintf(s + n, sizeof s - n, " | wild sp %d shiny %d", e.Species(), ((e.pid >> 16) ^ (e.pid & 0xFFFF) ^ tsv) < 8);
            const uint32_t bm = lay->battleMons;
            n += snprintf(s + n, sizeof s - n, " | battle hp %d/%d pp %d,%d,%d,%d", b.R16(bm + 0x28), b.R16(bm + 0x2C), b.R8(bm + 0x24),
                          b.R8(bm + 0x25), b.R8(bm + 0x26), b.R8(bm + 0x27));
        }
        return s;
    }

private:
    const Version* ver = nullptr;
    const Layout* lay = nullptr;
    bool on[COUNT] = {}, has[COUNT] = {}, applied[COUNT] = {};
    bool wildSet = false;
    int lastInBattle = -1;
    int lastHp[4] = {};
    uint32_t lastPid[4] = {};
    int lastPP[4][4] = {};

    struct Change { uint32_t addr; uint16_t from, to; };
    void Code(Bus& b, int id, bool want, std::initializer_list<Change> changes)
    {
        if (!has[id] || want == applied[id]) return;
        for (const Change& c : changes) b.W16(c.addr, want ? c.to : c.from);
        applied[id] = want;
    }
    static bool InEwram(uint32_t a, uint32_t size) { return a >= 0x02000000 && a + size <= 0x02040000; }
    static void SetBit(Bus& b, uint32_t a, uint8_t bit)
    {
        const uint8_t v = b.R8(a);
        if (!(v & bit)) b.W8(a, (uint8_t)(v | bit));
    }
    void SetFlag(Bus& b, uint32_t sb1, int flag) { SetBit(b, sb1 + lay->flags + flag / 8, (uint8_t)(1 << (flag & 7))); }
    int Flag(Bus& b, uint32_t sb1, int flag) { return (b.R8(sb1 + lay->flags + flag / 8) >> (flag & 7)) & 1; }
    // `item` in a pocket, `qty` of it (bag quantities are kept xor the save's key): its slot, or
    // the first empty one (none: nothing).
    static void Keep(Bus& b, uint32_t pocket, int n, uint16_t item, uint16_t qty, uint16_t key)
    {
        int empty = -1;
        for (int i = 0; i < n; i++)
        {
            const uint16_t id = b.R16(pocket + 4 * i);
            if (id == item)
            {
                if ((uint16_t)(b.R16(pocket + 4 * i + 2) ^ key) != qty) b.W16(pocket + 4 * i + 2, (uint16_t)(qty ^ key));
                return;
            }
            if (id == 0 && empty < 0) empty = i;
        }
        if (empty < 0) return;
        b.W16(pocket + 4 * empty, item);
        b.W16(pocket + 4 * empty + 2, (uint16_t)(qty ^ key));
    }
    // Eggs: their cycles (kept as friendship) at 0, and the day care's step counter one short of
    // the 255 at which the party's eggs are looked at (daycare.c, TryProduceOrHatchEgg).
    void Eggs(Bus& b, uint32_t sb1)
    {
        bool egg = false;
        const int count = b.R8(lay->partyCount);
        for (int i = 0; i < count && i < 6; i++)
        {
            Mon m;
            if (!m.Load(b, lay->party + 100 * i) || !m.IsEgg()) continue;
            egg = true;
            if (m.Part(0)[9] != 0)
            {
                m.Part(0)[9] = 0;
                m.Store(b);
            }
        }
        if (egg && b.R8(sb1 + lay->daycareStep) < 254) b.W8(sb1 + lay->daycareStep, 254);
    }
    // The player's Pokemon in battle (battlers 0 and 2): HP back to full, PP never down. The party
    // is told too, as the game tells it of every change (else the battle's ending would leave it
    // with what it had lost).
    void Battle(Bus& b)
    {
        const int battlers = b.R8(lay->battlers);
        for (int bt = 0; bt < battlers && bt < 4; bt += 2)
        {
            const uint32_t m = lay->battleMons + 0x58 * bt;
            const uint32_t pid = b.R32(m + 0x48);
            const int idx = b.R16(lay->battlerParty + 2 * bt);
            const uint32_t pm = lay->party + 100 * idx;
            const bool partyOk = idx < 6 && b.R32(pm) == pid;
            if (pid != lastPid[bt])
            {
                lastPid[bt] = pid;
                lastHp[bt] = 0;
                for (int i = 0; i < 4; i++) lastPP[bt][i] = -1;
            }
            if (on[HP])
            {
                const uint16_t hp = b.R16(m + 0x28), max = b.R16(m + 0x2C);
                // (Not one that fainted before: only one that had HP, even if this frame took it.)
                if (max && hp < max && (hp > 0 || lastHp[bt] > 0))
                {
                    b.W16(m + 0x28, max);
                    if (partyOk && b.R16(pm + 86) != max) b.W16(pm + 86, max);
                }
                lastHp[bt] = b.R16(m + 0x28);
            }
            if (on[PP])
            {
                bool lowered = false;
                for (int i = 0; i < 4; i++)
                {
                    const int pp = b.R8(m + 0x24 + i);
                    if (lastPP[bt][i] >= 0 && pp < lastPP[bt][i])
                    {
                        b.W8(m + 0x24 + i, (uint8_t)lastPP[bt][i]);
                        lowered = true;
                    }
                    else lastPP[bt][i] = pp;
                }
                Mon p;
                if (lowered && partyOk && p.Load(b, pm))
                {
                    bool changed = false;
                    uint8_t* at = p.Part(1); // moves[4] (u16), then pp[4]
                    for (int i = 0; i < 4; i++)
                        if (Get16(at + 2 * i) == b.R16(m + 0x0C + 2 * i) && at[8 + i] != b.R8(m + 0x24 + i))
                        {
                            at[8 + i] = b.R8(m + 0x24 + i);
                            changed = true;
                        }
                    if (changed) p.Store(b);
                }
            }
        }
    }
};

// The cheats folder for a GBA game's saves folder: beside it when it is called saves (as the DS
// games', cheats.h FolderFor), else inside it.
inline std::string FolderFor(const std::string& saves)
{
    const size_t s = saves.find_last_of("/\\");
    const std::string last = s == std::string::npos ? saves : saves.substr(s + 1);
    if (last == "saves") return (s == std::string::npos ? std::string(".") : saves.substr(0, s)) + "/cheats";
    return saves + "/cheats";
}

} // namespace GbaCheats
