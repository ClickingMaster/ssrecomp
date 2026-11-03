// Pokemon Platinum (English, CPUE): which of its screens is up, for its layouts.
//
// Platinum runs like Black 2 (melonDS's JIT, layouts that follow the game's own functions),
// with a function table made by tools/platinum/funcmap.py from a build of the pokeplatinum
// decompilation. Its screens are Gen 4's, much like SoulSilver's: the overworld and its menus
// on the top screen (the Poketch on the touch screen beside it), battles with their commands
// on the touch screen. pickSceneBw2 in main.cpp picks the scenes from the functions below.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "funcwatch.h"
#include "gamecode.h"

struct PtFuncs
{
    // The introduction's two questions answered only by a touch (Professor Rowan's), from the
    // touch screen's picture (0xRRGGBB pixels): INTRO_YESNO, his YES / NO (YES red at x 100-140,
    // y 70-92, NO blue at y 110-132); INTRO_BALL, "Touch the button on the middle of the Poke
    // Ball" (the ball at x 58-200, y 26-170, its pale blue button at 128, 96); 0 for neither.
    // (Diamond and Pearl's NO is higher, at y 102-124: IntroNoY says where NO was seen.)
    enum { INTRO_YESNO = 1, INTRO_BALL = 2 };
    // The ball is visible before its touch prompt: A must still advance that dialogue.
    // YES/NO uses touch alone, so its button keys are consumed while that prompt is up.
    static uint32_t IntroDialogueKeys(int kind, uint32_t keys, uint32_t answer)
    {
        return kind == INTRO_YESNO ? keys & ~answer : keys;
    }
    static inline int IntroNoY = 121;
    static int IntroTouch(const uint32_t* bottom)
    {
        if (!bottom) return 0;
        auto is = [&](int x, int y, int r, int g, int b) {
            const uint32_t c = bottom[y * 256 + x];
            return std::abs((int)((c >> 16) & 0xFF) - r) < 40 && std::abs((int)((c >> 8) & 0xFF) - g) < 40 &&
                   std::abs((int)(c & 0xFF) - b) < 40;
        };
        // Find the coloured button areas independently of translated lettering.
        int red = 0, blue = 0, blueY = 0;
        for (int y = 70; y < 92; ++y)
            for (int x = 100; x < 140; ++x) red += is(x, y, 251, 89, 89);
        for (int y = 96; y < 140; ++y)
            for (int x = 100; x < 140; ++x)
                if (is(x, y, 73, 154, 219)) { ++blue; blueY += y; }
        if (red >= 250 && blue >= 250)
        {
            IntroNoY = blueY / blue;
            return INTRO_YESNO;
        }
        if (is(108, 76, 251, 89, 89) && is(132, 84, 251, 89, 89) && is(104, 108, 73, 154, 219) && is(136, 116, 73, 154, 219))
        {
            IntroNoY = 113;
            return INTRO_YESNO;
        }
        if (is(100, 70, 219, 97, 97) && is(156, 70, 219, 97, 97) && is(128, 60, 219, 97, 97) && is(100, 120, 251, 251, 251) &&
            is(156, 120, 251, 251, 251) && is(128, 130, 251, 251, 251) && is(128, 96, 211, 227, 251))
            return INTRO_BALL;
        return 0;
    }

    // The Pokedex list's three touch-screen buttons (SEARCH, SWITCH, CHECK), from the colours
    // at their centres. The Poke Ball beside them is what a click on the right of the view hits.
    static bool DexListButtons(const uint32_t* bottom)
    {
        if (!bottom) return false;
        auto at = [&](int x, int y, int* r, int* g, int* b) {
            const uint32_t c = bottom[y * 256 + x] & 0xFFFFFF;
            *r = (int)((c >> 16) & 0xFF);
            *g = (int)((c >> 8) & 0xFF);
            *b = (int)(c & 0xFF);
        };
        int r, g, b;
        at(60, 42, &r, &g, &b);
        if (!(g > 140 && g > r + 20 && b < 160)) return false;
        at(60, 88, &r, &g, &b);
        if (!(b > 140 && g > 140 && r < 140)) return false;
        at(60, 160, &r, &g, &b);
        return g > 140 && g > r + 20 && b < 160;
    }

    // Whether a weather with particles ran within the last `frames` frames.
    bool Weather(const FuncWatch& fw, unsigned frames) const
    {
        for (int f : weather)
            if (fw.Ran(f, frames)) return true;
        return false;
    }

    // The names of Platinum's application layouts (pickSceneBw2), which stay up while the
    // application fades out and change at once between two of them with both screens black.
    static bool AppScene(const char* n)
    {
        static const char* const names[] = { "party", "bag", "summary", "options", "journal", "move-reminder",
            "pokedex", "trainer-card", "pc-box", "map", "naming" };
        for (const char* m : names)
            if (!strcmp(n, m)) return true;
        return false;
    }

    bool on = false;
    bool dp = false; // Diamond or Pearl (their battle panels are told by which was drawn, not by colour)
    int field = -1;          // the overworld, every frame
    int startMenu = -1;      // the X menu (and the applications started from it)
    int dex = -1, party = -1, bag = -1, trainerCard = -1, options = -1, summary = -1;
    int townMap = -1, box = -1, naming = -1, title = -1, mainMenu = -1, intro = -1;
    int starter = -1, journal = -1, moveReminder = -1, evolution = -1, eggHatch = -1;
    int hallOfFame = -1, credits = -1;
    int battle = -1;         // any battle
    int battleInput = -1;    // the battle waiting for a command or a move on the touch screen
    int battleBag = -1, battleParty = -1; // the Bag and the party in a battle (touch screen)
    // The touch-screen panels a battle draws: the commands (FIGHT, BAG, RUN, POKeMON), the first
    // battle's FIGHT alone, the four moves, and the others (YES / NO, "Use next Pokemon?", forget
    // a move, a target...). Which was drawn last says what the touch screen shows: the colours
    // alone take "Use next Pokemon?" (a red bar on top) for the first battle's FIGHT.
    int battleActionMenu = -1, battleFirstMenu = -1, battleMoveMenu = -1;
    std::vector<int> battleOtherMenus;
    // The last of these panels drawn: 1 the commands, 2 FIGHT alone, 3 the moves, 4 another, 0
    // none seen yet (a snapshot loaded in a battle).
    int BattlePanel(const FuncWatch& fw) const
    {
        uint32_t best = 0;
        int kind = 0;
        auto take = [&](int f, int k) {
            const uint32_t t = fw.LastFrame(f);
            if (t && t >= best) { best = t; kind = k; }
        };
        for (int f : battleOtherMenus) take(f, 4);
        take(battleActionMenu, 1);
        take(battleFirstMenu, 2);
        take(battleMoveMenu, 3);
        return kind;
    }
    int saving = -1;         // the save being written (several seconds, nothing else running)
    // The weathers with particles (sprites over the world): each one's task, every frame while
    // it lasts (pokeplatinum's src/overlay005/ov5_021D5EB8.c, its table of weathers: rain, heavy
    // rain, thunderstorm, snow, heavy snow, blizzard, ashfall, sandstorm, hail, spirits and two
    // more).
    std::vector<int> weather;

    // romPath: the game being started; names: the functions the layouts watch (Platinum's are
    // added to it when the game is Platinum).
    void Init(const std::string& romPath, const FuncWatch& fw, std::vector<std::string>& names)
    {
        char code[5] = {};
        if (FILE* f = fopen(romPath.c_str(), "rb"))
        {
            if (fseek(f, 0x0C, SEEK_SET) != 0 || fread(code, 1, 4, f) != 4) code[0] = 0;
            fclose(f);
        }
        // (Diamond and Pearl: the same engine, their table has Platinum's names: tools/port_family.py.)
        on = IsGame(code, "CPUE") || IsGame(code, "ADAE") || IsGame(code, "APAE");
        dp = IsGame(code, "ADAE") || IsGame(code, "APAE");
        if (!on) return;
        auto find = [&](const char* name) {
            names.push_back(name);
            return fw.Find(name);
        };
        field = find("FieldMap_Main");
        startMenu = find("StartMenu_Main");
        dex = find("PokedexMain_Main");
        party = find("PartyMenu_Main");
        bag = find("BagApplication_Main");
        trainerCard = find("TrainerCaseApp_Main");
        options = find("OptionsMenu_Main");
        summary = find("PokemonSummaryScreen_Main");
        townMap = find("TownMap_Main");
        box = find("BoxAppMan_Main");
        naming = find("NamingScreen_Main");
        title = find("TitleScreen_Main");
        mainMenu = find("MainMenu_Main");
        intro = find("RowanIntro_Main");
        starter = find("ChooseStarter_Main");
        journal = find("JournalController_Main");
        moveReminder = find("MoveReminder_Main");
        evolution = find("Evolution_Main");
        eggHatch = find("EggHatch_Main");
        hallOfFame = find("HallOfFameManager_Main");
        credits = find("EndCreditsApp_Main");
        battle = find("Battle_Main");
        battleInput = find("BattleSystem_MenuInput");
        battleBag = find("BattleBagTask_Tick");
        battleParty = find("BattlePartyTask_Tick");
        battleActionMenu = find("BattleSubscreen_DrawActionMenu");
        battleFirstMenu = find("BattleSubscreen_DrawFirstBattleMenu");
        battleMoveMenu = find("BattleSubscreen_DrawMoveSelectMenu");
        for (const char* m : { "BattleSubscreen_DrawPalParkMenu", "BattleSubscreen_DrawYesNoMenu", "BattleSubscreen_DrawForgetMoveMenu",
                               "BattleSubscreen_DrawGiveUpMoveMenu", "BattleSubscreen_DrawStopMenu", "BattleSubscreen_DrawUseNextMonMenu",
                               "BattleSubscreen_DrawSwitchMonMenu", "BattleSubscreen_DrawTargetSelectMenu" })
            battleOtherMenus.push_back(find(m));
        saving = find("SaveData_CardSave_Main");
        for (const char* w : { "ov5_021D7658", "ov5_021D9FF8", "ov5_021D9F0C", "ov5_021D79F0", "ov5_021D7E54", "ov5_021D8FF8",
                               "ov5_021DA244", "ov5_021D8D08", "ov5_021D9464", "ov5_021DA8A0", "ov5_021D823C", "ov5_021D8948" })
            weather.push_back(find(w));
        fprintf(stderr, "[platinum] field %d, menu %d, battle %d\n", field, startMenu, battle);
    }
};
