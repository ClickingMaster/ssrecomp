// The GBA cheats (frontend/gba_cheats.h) on a pretend GBA: nothing written while none is on; each
// one's effect; the code changes in and out again; a Pokemon kept intact (encrypted and back,
// made shiny with its nature and gender kept). Build: g++ -std=c++17 -Ifrontend tests/test_gba_cheats.cpp
#include "gba_cheats.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
using namespace GbaCheats;

struct FakeBus : Bus
{
    std::map<uint32_t, uint8_t> mem;
    int writes = 0;
    uint8_t R8(uint32_t a) override { auto i = mem.find(a); return i == mem.end() ? 0 : i->second; }
    uint16_t R16(uint32_t a) override { return (uint16_t)(R8(a) | R8(a + 1) << 8); }
    uint32_t R32(uint32_t a) override { return R16(a) | (uint32_t)R16(a + 2) << 16; }
    void W8(uint32_t a, uint8_t v) override { mem[a] = v; writes++; }
    void W16(uint32_t a, uint16_t v) override { mem[a] = (uint8_t)v; mem[a + 1] = (uint8_t)(v >> 8); writes++; }
    void W32(uint32_t a, uint32_t v) override { W16(a, (uint16_t)v); W16(a + 2, (uint16_t)(v >> 16)); writes--; }
    void Set16(uint32_t a, uint16_t v) { mem[a] = (uint8_t)v; mem[a + 1] = (uint8_t)(v >> 8); }
    void Set32(uint32_t a, uint32_t v) { Set16(a, (uint16_t)v); Set16(a + 2, (uint16_t)(v >> 16)); }
};

// A Pokemon written as the game would: plain parts in, encrypted out.
static void MakeMon(FakeBus& b, uint32_t a, uint32_t pid, uint32_t otid, uint16_t species, bool egg, uint8_t friendship)
{
    Mon m;
    m.addr = a; m.pid = pid; m.otid = otid;
    memset(m.raw, 0, 80); memset(m.sec, 0, 48);
    Put32(m.raw, pid); Put32(m.raw + 4, otid);
    m.raw[19] = 2; // hasSpecies
    Put16(m.Part(0), species);
    Put32(m.Part(0) + 4, 1234);  // experience
    m.Part(0)[9] = friendship;
    Put16(m.Part(1), 33); m.Part(1)[8] = 35; // Tackle, 35 PP
    Put32(m.Part(3) + 4, (egg ? 1u << 30 : 0) | 31); // an IV, isEgg
    for (int i = 0; i < 20; i++) b.Set32(a + 4 * i, 0xDEADBEEF); // (Store writes only what differs)
    m.Store(b);
}

int main()
{
    // ---- Emerald, as at the start of a game
    FakeBus b;
    const Layout& L = Emerald();
    const Version& V = Versions()[0];
    const uint32_t sb1 = 0x02025A00, sb2 = 0x02024A54, key = 0x12345678;
    b.Set32(L.sb1Ptr, sb1); b.Set32(L.sb2Ptr, sb2); b.Set32(sb2 + L.key, key);
    b.Set32(sb1 + L.money, 3000 ^ key);
    b.Set16(sb1 + L.items, 13); b.Set16(sb1 + L.items + 2, (uint16_t)(5 ^ key)); // 5 Potions... (any item)
    // the code the code changes look for
    b.Set16(V.collision, L.collisionFirst); b.Set16(V.collision + 2, 0x1c02);
    b.Set16(V.ballThrow + L.ballOff, 0xd92f);
    b.Set16(V.getExp + L.expBneOff, 0xd107); b.Set16(V.getExp + L.expMulOff, 0x2096);
    b.mem[L.partyCount] = 1;
    MakeMon(b, L.party, 0x87654321, 0x0000ABCD, 25, true, 20);

    Engine e;
    assert(!e.Start(b, "BPEE", 0, false));            // not known: none
    assert(!e.Start(b, "AXVE", 0, true));             // not one of the games: none
    assert(e.Start(b, "BPEE", 0, true) && e.Active());
    for (int i = 0; i < COUNT; i++) assert(e.Has(i) && !e.On(i));
    assert(e.OnCount() == 0);

    // Nothing on: 600 frames, in battle and out, B held, not one write.
    b.writes = 0;
    for (int f = 0; f < 600; f++)
    {
        b.mem[L.mainBits] = (f / 100) % 2 ? 2 : 0;
        e.Frame(b, kB);
    }
    assert(b.writes == 0);

    // Money, candies, Master Balls, TMs, badges, fly, Pokedex, no wild encounters.
    for (int id : { (int)MONEY, (int)CANDY, (int)MASTER, (int)TMS, (int)BADGES, (int)FLY, (int)DEX, (int)NOWILD }) e.Set(id, true);
    e.Frame(b, 0);
    assert((b.R32(sb1 + L.money) ^ key) == 999999);
    assert(b.R16(sb1 + L.items) == 13 && b.R16(sb1 + L.items + 4) == kRareCandy && (uint16_t)(b.R16(sb1 + L.items + 6) ^ key) == 99);
    assert(b.R16(sb1 + L.balls) == kMasterBall && (uint16_t)(b.R16(sb1 + L.balls + 2) ^ key) == 99);
    assert(b.R16(sb1 + L.tmhm) == kTm01 && (uint16_t)(b.R16(sb1 + L.tmhm + 2) ^ key) == 99);
    assert(b.R16(sb1 + L.tmhm + 4 * 57) == kHm01 + 7 && (uint16_t)(b.R16(sb1 + L.tmhm + 4 * 57 + 2) ^ key) == 1);
    for (int i = 0; i < 8; i++) assert(b.R8(sb1 + L.flags + (L.badge1 + i) / 8) & (1 << ((L.badge1 + i) & 7)));
    assert(b.R8(sb2 + 0x28) == 0xFF && b.R8(sb2 + 0x28 + 48) == 0x03 && b.R8(sb1 + L.seen2 + 47) == 0xFF);
    assert(b.R8(sb2 + L.dexMagic) == 0xDA && b.R16(sb1 + L.vars + 2 * (L.natDexVar - 0x4000)) == 0x302);
    assert(b.R8(L.wildOff) == 1);
    const std::string r = e.Report(b);
    assert(r.find("money 999999 candy 99 master 99 tmhm 58 tmcase 1 badges 8 fly 16 owned 386 natdex 1 wildoff 1") != std::string::npos);
    // Used a candy: back to 99. Kept, not written again when nothing changed.
    b.Set16(sb1 + L.items + 6, (uint16_t)(98 ^ key));
    e.Frame(b, 0);
    assert((uint16_t)(b.R16(sb1 + L.items + 6) ^ key) == 99);
    int w = b.writes;
    e.Frame(b, 0);
    assert(b.writes == w);
    // No wild encounters off: the game's switch back off.
    e.Set(NOWILD, false);
    e.Frame(b, 0);
    assert(b.R8(L.wildOff) == 0);
    for (int i = 0; i < COUNT; i++) e.Set(i, false);
    w = b.writes;
    e.Frame(b, kB);
    assert(b.writes == w); // all off again: nothing

    // Code changes: walls only while B is held, out again when let go; catch and EXP while on.
    e.Set(WALLS, true); e.Set(CATCH, true); e.Set(EXP, true);
    e.Frame(b, 0);
    assert(b.R16(V.collision) == L.collisionFirst && b.R16(V.ballThrow + L.ballOff) == 0x46c0);
    assert(b.R16(V.getExp + L.expBneOff) == 0x46c0 && b.R16(V.getExp + L.expMulOff) == 0x20c8);
    e.Frame(b, kB);
    assert(b.R16(V.collision) == 0x2000 && b.R16(V.collision + 2) == 0x4770);
    e.Frame(b, 0);
    assert(b.R16(V.collision) == L.collisionFirst && b.R16(V.collision + 2) == 0x1c02);
    e.Set(WALLS, false); e.Set(CATCH, false); e.Set(EXP, false);
    e.Frame(b, kB);
    assert(b.R16(V.ballThrow + L.ballOff) == 0xd92f && b.R16(V.getExp + L.expBneOff) == 0xd107 && b.R16(V.getExp + L.expMulOff) == 0x2096);
    // Code not as expected (another hack, a patch over it): that one isn't offered.
    {
        FakeBus c = b;
        c.Set16(V.ballThrow + L.ballOff, 0x1234);
        Engine e2;
        assert(e2.Start(c, "BPEE", 0, true) && !e2.Has(CATCH) && e2.Has(WALLS) && e2.Has(EXP));
        e2.Set(CATCH, true);
        assert(!e2.On(CATCH));
    }

    // Eggs: cycles to 0, step counter to 254; the Pokemon still sound.
    e.Set(EGGS, true);
    e.Frame(b, 0);
    {
        Mon m;
        assert(m.Load(b, L.party) && m.IsEgg() && m.Part(0)[9] == 0 && m.Species() == 25 && Get32(m.Part(0) + 4) == 1234);
        assert(b.R8(sb1 + L.daycareStep) == 254);
    }
    e.Set(EGGS, false);

    // Shiny: a wild battle begins (inBattle 0 -> 1, no trainer): its Pokemon made shiny, all else kept.
    e.Set(SHINY, true);
    b.mem[L.mainBits] = 0;
    e.Frame(b, 0);
    int shinyCount = 0;
    srand(7);
    for (int t = 0; t < 300; t++)
    {
        const uint32_t pid = (uint32_t)rand() << 16 ^ (uint32_t)rand(), otid = (uint32_t)rand() << 16 ^ (uint32_t)rand();
        MakeMon(b, L.enemyParty, pid, otid, (uint16_t)(1 + t % 386), false, 70);
        b.mem[L.mainBits] = 2;
        b.Set32(L.battleType, t % 50 == 0 ? kTrainer : 0);
        e.Frame(b, 0);
        Mon m;
        assert(m.Load(b, L.enemyParty));
        const bool shiny = ((m.pid >> 16) ^ (m.pid & 0xFFFF) ^ (otid >> 16) ^ (otid & 0xFFFF)) < 8;
        const uint16_t sp = (uint16_t)(1 + t % 386);
        assert(m.Species() == sp && Get32(m.Part(0) + 4) == 1234 && m.Part(0)[9] == 70 && Get16(m.Part(1)) == 33 && m.Part(1)[8] == 35);
        assert(m.pid % 25 == pid % 25 && (m.pid & 0xFF) == (pid & 0xFF));
        if (t % 50 == 0 || sp == kUnown) assert(m.pid == pid); // trainer's, or Unown: as they were
        else assert(shiny);
        shinyCount += shiny;
        b.mem[L.mainBits] = 0; // battle over
        e.Frame(b, 0);
    }
    assert(shinyCount >= 290);
    e.Set(SHINY, false);

    // HP and PP in battle: back up, and the party told.
    e.Set(HP, true); e.Set(PP, true);
    b.mem[L.mainBits] = 2; b.mem[L.battlers] = 2; b.Set16(L.battlerParty, 0);
    MakeMon(b, L.party, 0x11112222, 0x0000ABCD, 25, false, 70);
    const uint32_t bm = L.battleMons;
    b.Set32(bm + 0x48, 0x11112222); b.Set16(bm + 0x0C, 33); b.mem[bm + 0x24] = 35;
    b.Set16(bm + 0x28, 40); b.Set16(bm + 0x2C, 40); b.Set16(L.party + 86, 40); b.Set16(L.party + 88, 40);
    e.Frame(b, 0);
    b.Set16(bm + 0x28, 0); b.Set16(L.party + 86, 0); b.mem[bm + 0x24] = 34; // a hit that took it all, a move used
    e.Frame(b, 0);
    assert(b.R16(bm + 0x28) == 40 && b.R16(L.party + 86) == 40 && b.R8(bm + 0x24) == 35);
    {
        Mon m;
        assert(m.Load(b, L.party) && m.Part(1)[8] == 35);
    }
    // One that had fainted before HP was turned on stays fainted.
    {
        Engine e3;
        FakeBus c = b;
        assert(e3.Start(c, "BPEE", 0, true));
        c.Set16(bm + 0x28, 0);
        e3.Set(HP, true);
        e3.Frame(c, 0);
        e3.Frame(c, 0);
        assert(c.R16(bm + 0x28) == 0);
    }

    // Which are on, kept by name.
    const std::string dir = (std::filesystem::temp_directory_path() / "fulldex-gba-cheats-test").string();
    std::filesystem::remove_all(dir); // (Save makes the folder)
    e.Set(MONEY, true); e.Set(FLY, true);
    assert(e.Save(dir + "/emerald-on.txt"));
    Engine e4;
    assert(e4.Start(b, "BPEE", 0, true));
    e4.Load(dir + "/emerald-on.txt");
    assert(e4.OnCount() == e.OnCount() && e4.On(MONEY) && e4.On(FLY) && e4.On(HP) && e4.On(PP));
    std::filesystem::remove_all(dir);
    assert(FolderFor("/x/roms/GBA/saves") == "/x/roms/GBA/cheats" && FolderFor("/x/data") == "/x/data/cheats");

    // FireRed: the TMs come with the TM Case (a key item there), in the first free key item slot.
    {
        FakeBus c;
        const Layout& F = FireRedLeafGreen();
        const Version& FV = Versions()[1];
        const uint32_t s1 = 0x02025534, s2 = 0x0202552C + 0x1000, k = 0xCAFEF00D;
        c.Set32(F.sb1Ptr, s1); c.Set32(F.sb2Ptr, s2); c.Set32(s2 + F.key, k);
        c.Set16(s1 + F.keyItems, 360); c.Set16(s1 + F.keyItems + 2, (uint16_t)(1 ^ k)); // (another key item)
        Engine f;
        assert(f.Start(c, FV.code, FV.rev, true));
        f.Set(TMS, true);
        f.Frame(c, 0);
        assert(c.R16(s1 + F.keyItems) == 360 && c.R16(s1 + F.keyItems + 4) == 364 && (uint16_t)(c.R16(s1 + F.keyItems + 6) ^ k) == 1);
        assert(c.R16(s1 + F.tmhm) == kTm01 && (uint16_t)(c.R16(s1 + F.tmhm + 2) ^ k) == 99);
        assert(f.Report(c).find("tmhm 58 tmcase 1") != std::string::npos);
    }

    // FireRed, LeafGreen and Ultra Violet: their tables there.
    for (const char* code : { "BPRE", "BPGE", "PUVV" })
        for (int rev = 0; rev < 2; rev++)
        {
            Engine f;
            const bool want = !(code[1] == 'U' && rev == 1);
            assert(f.Start(b, code, rev, true) == want);
        }
    std::cout << "PASS: GBA cheats: none writes while off; money, items, TMs, badges, fly, Pokedex, eggs, shiny, HP, PP, code changes\n";
}
