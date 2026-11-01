#pragma once
// Remappable controls: which keyboard key and which controller button does each of the DS's
// buttons and the program's own actions (the pause menu, fast forward, swapping the screens,
// save state and load state). Changed in the pause menu's CONTROLS page (main.cpp) and kept
// in the settings file as map_key_<action>=<SDL scancode> and map_pad_<action>=<input> lines
// (map_pad_<action>=none: that action has no controller button); the game list
// (pc_launcher.cpp, switch_platform.cpp) follows the ones that were changed.
//
// A controller is remapped where it is read (main.cpp, RemapPad): what is pressed on it becomes
// the buttons in their usual places, so everything after that (the game, the menus, START
// opening the menu) follows the new layout without knowing about it. The bindings form a
// permutation: giving an action a button that another one had swaps the two, so no button
// ever does two things. Save state and load state start with no controller button; giving
// one of them a button another action had leaves that action with none.
#ifdef __APPLE__
#include <SDL.h>
#else
#include <SDL2/SDL.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace Controls
{

enum Action
{
    A, B, X, Y, L, R, START, SELECT, UP, DOWN, LEFT, RIGHT,
    MENU,  // opens (and closes) the pause menu
    FAST,  // fast forward while held
    SWAP,  // swaps which screen is large (with STICK TOUCH CURSOR on a controller)
    SAVE,  // quick save state (one slot per game)
    LOAD,  // quick load state
    COUNT
};

// No controller button. Distinct from -1, which means "the default" (DefaultPad). Save state
// and load state start here. A swap that takes the last button off an action stores this, so
// the default does not come back and press the same thing.
enum { PAD_NONE = -2 };

// Controller inputs: SDL's button order (as main.cpp's PAD_*), then the two triggers, which
// count as pressed past half way.
enum
{
    IN_A, IN_B, IN_X, IN_Y, IN_BACK, IN_GUIDE, IN_START, IN_LS, IN_RS, IN_LB, IN_RB,
    IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT, IN_LT, IN_RT, IN_COUNT
};

inline const char* ActionKey(int a)
{
    static const char* k[COUNT] = { "a", "b", "x", "y", "l", "r", "start", "select", "up", "down", "left", "right",
                                    "menu", "fast", "swap", "save", "load" };
    return a >= 0 && a < COUNT ? k[a] : "";
}

inline const char* ActionName(int a)
{
    static const char* n[COUNT] = { "A", "B", "X", "Y", "L", "R", "START", "SELECT", "UP", "DOWN", "LEFT", "RIGHT",
                                    "PAUSE MENU", "FAST FORWARD", "SWAP SCREENS", "SAVE STATE", "LOAD STATE" };
    return a >= 0 && a < COUNT ? n[a] : "";
}

inline const char* InputKey(int i)
{
    static const char* k[IN_COUNT] = { "a", "b", "x", "y", "back", "guide", "start", "ls", "rs", "lb", "rb",
                                       "up", "down", "left", "right", "lt", "rt" };
    return i >= 0 && i < IN_COUNT ? k[i] : "";
}

inline int InputFromKey(const std::string& s)
{
    for (int i = 0; i < IN_COUNT; i++)
        if (s == InputKey(i)) return i;
    return -1;
}

// PlayStation's button symbols, as characters of the menus' font (ui.h, Glyph).
#define PS_CROSS "\x01"
#define PS_CIRCLE "\x02"
#define PS_SQUARE "\x03"
#define PS_TRIANGLE "\x04"

// Which names a controller's buttons have (StyleOf): Xbox's (and most others'), PlayStation's or
// Nintendo's.
enum Style { XBOX, PLAYSTATION, NINTENDO };

inline int StyleOf(SDL_GameController* c)
{
    if (!c) return XBOX;
    switch (SDL_GameControllerGetType(c))
    {
    case SDL_CONTROLLER_TYPE_PS3:
    case SDL_CONTROLLER_TYPE_PS4:
    case SDL_CONTROLLER_TYPE_PS5:
        return PLAYSTATION;
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
#if SDL_VERSION_ATLEAST(2, 24, 0)
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
#endif
        return NINTENDO;
    default:
        return XBOX;
    }
}

// As printed on the controller. (SDL gives A to the bottom button of an Xbox or PlayStation pad
// and to the button labelled A of a Nintendo one, as does main.cpp's ReadPad on the Switch.)
inline const char* InputName(int i, int style = XBOX)
{
#ifdef __SWITCH__
    style = NINTENDO;
#endif
    static const char* xbox[IN_COUNT] = { "A", "B", "X", "Y", "BACK", "GUIDE", "START", "LEFT STICK", "RIGHT STICK", "LB", "RB",
                                          "UP", "DOWN", "LEFT", "RIGHT", "LT", "RT" };
    // (The four symbols are characters of the menus' font, ui.h's Glyph.)
    static const char* ps[IN_COUNT] = { PS_CROSS, PS_CIRCLE, PS_SQUARE, PS_TRIANGLE, "SHARE", "PS", "OPTIONS", "L3", "R3", "L1", "R1",
                                        "UP", "DOWN", "LEFT", "RIGHT", "L2", "R2" };
    static const char* nin[IN_COUNT] = { "A", "B", "X", "Y", "-", "HOME", "+", "LEFT STICK", "RIGHT STICK", "L", "R",
                                         "UP", "DOWN", "LEFT", "RIGHT", "ZL", "ZR" };
#ifdef __SWITCH__
    // (The Switch keeps its HOME button to itself: no game ever sees it pressed.)
    if (i == IN_GUIDE) return "NOT SET";
#endif
    if (i < 0 || i >= IN_COUNT) return "NOT SET";
    return style == PLAYSTATION ? ps[i] : style == NINTENDO ? nin[i] : xbox[i];
}

// A joystick SDL has no layout for (a generic USB pad): its buttons in the usual order of such
// pads, as main.cpp's ReadPad reads them. CONTROLS can then move any of them where wanted.
inline int JoyButtonInput(int button)
{
    static const int order[] = { IN_A, IN_B, IN_X, IN_Y, IN_LB, IN_RB, IN_BACK, IN_START, IN_LS, IN_RS, IN_GUIDE };
    return button >= 0 && button < (int)(sizeof(order) / sizeof(order[0])) ? order[button] : -1;
}

// Where each action is on a controller as it comes: the usual buttons in their usual places. A
// B X Y follow BUTTONS: by label (the button printed A is the DS's A) or by position (the
// right one is, as on a DS: Xbox B). These are also the buttons main.cpp reads each action
// from once the controller has been remapped.
inline int DefaultPad(int a, bool byLabel)
{
    switch (a)
    {
    case A: return byLabel ? IN_A : IN_B;
    case B: return byLabel ? IN_B : IN_A;
    case X: return byLabel ? IN_X : IN_Y;
    case Y: return byLabel ? IN_Y : IN_X;
    case L: return IN_LB;
    case R: return IN_RB;
    case START: return IN_START;
    case SELECT: return IN_BACK;
    case UP: return IN_UP;
    case DOWN: return IN_DOWN;
    case LEFT: return IN_LEFT;
    case RIGHT: return IN_RIGHT;
    case MENU: return IN_GUIDE;
    case FAST: return IN_RT;
    case SWAP: return IN_RS;
    case SAVE: return -1; // (no button until one is chosen: F5 on the keyboard)
    case LOAD: return -1;
    default: return -1;
    }
}

// A line under the row in KEYS AND BUTTONS, or null.
inline const char* ActionHint(int a)
{
    switch (a)
    {
    case FAST: return "HOLD TO RUN AHEAD";
    case SWAP: return "SWAPS WHICH SCREEN IS LARGE";
    case SAVE: return "SAVES TO THE QUICK SLOT";
    case LOAD: return "LOADS THE QUICK SLOT";
    default: return nullptr;
    }
}

inline int DefaultKey(int a)
{
    switch (a)
    {
    case A: return SDL_SCANCODE_X;
    case B: return SDL_SCANCODE_Z;
    case X: return SDL_SCANCODE_S;
    case Y: return SDL_SCANCODE_A;
    case L: return SDL_SCANCODE_Q;
    case R: return SDL_SCANCODE_W;
    case START: return SDL_SCANCODE_RETURN;
    case SELECT: return SDL_SCANCODE_RSHIFT;
    case UP: return SDL_SCANCODE_UP;
    case DOWN: return SDL_SCANCODE_DOWN;
    case LEFT: return SDL_SCANCODE_LEFT;
    case RIGHT: return SDL_SCANCODE_RIGHT;
    case MENU: return SDL_SCANCODE_ESCAPE;
    case FAST: return SDL_SCANCODE_LSHIFT;
    case SWAP: return SDL_SCANCODE_F1;
    case SAVE: return SDL_SCANCODE_F5;
    case LOAD: return SDL_SCANCODE_F8;
    default: return SDL_SCANCODE_UNKNOWN;
    }
}

// A key's name in the menus' font (capitals, digits and : - + . / < > ! ( ) only).
inline std::string KeyName(int sc)
{
    switch (sc)
    {
    case SDL_SCANCODE_UNKNOWN: return "-";
    case SDL_SCANCODE_RETURN: return "ENTER";
    case SDL_SCANCODE_ESCAPE: return "ESC";
    case SDL_SCANCODE_COMMA: return "COMMA";
    case SDL_SCANCODE_SEMICOLON: return "SEMICOLON";
    case SDL_SCANCODE_APOSTROPHE: return "QUOTE";
    case SDL_SCANCODE_LEFTBRACKET: return "LEFT BRACKET";
    case SDL_SCANCODE_RIGHTBRACKET: return "RIGHT BRACKET";
    case SDL_SCANCODE_BACKSLASH: return "BACKSLASH";
    case SDL_SCANCODE_NONUSBACKSLASH: return "BACKSLASH 2";
    case SDL_SCANCODE_EQUALS: return "EQUALS";
    case SDL_SCANCODE_GRAVE: return "GRAVE";
    case SDL_SCANCODE_NONUSHASH: return "HASH";
    default: break;
    }
    const char* n = SDL_GetScancodeName((SDL_Scancode)sc);
    std::string s;
    for (const char* p = n; p && *p; p++)
    {
        char c = *p;
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || (c && strchr(":-+./<>!()", c));
        s += ok ? c : ' ';
    }
    return s.empty() ? "KEY " + std::to_string(sc) : s;
}

struct Bindings
{
    int pad[COUNT]; // an input, -1: the default (DefaultPad), or PAD_NONE
    int key[COUNT]; // a scancode, or -1: the default (DefaultKey)
    bool byLabel = true; // (Load: the file's BUTTONS, for the game list; the game has its own)

    Bindings() { Reset(); }
    void Reset()
    {
        for (int a = 0; a < COUNT; a++) pad[a] = key[a] = -1;
    }
    int Pad(int a, bool byLabel) const
    {
        if (pad[a] == PAD_NONE) return -1;
        return pad[a] >= 0 ? pad[a] : DefaultPad(a, byLabel);
    }
    int Key(int a) const { return key[a] >= 0 ? key[a] : DefaultKey(a); }
    bool PadCustom() const
    {
        for (int a = 0; a < COUNT; a++)
            if (pad[a] >= 0 || pad[a] == PAD_NONE) return true;
        return false;
    }
    bool KeyCustom(int a) const { return key[a] >= 0 && key[a] != DefaultKey(a); }
    // The action a key does, or -1.
    int ActionOfKey(int sc) const
    {
        for (int a = 0; a < COUNT; a++)
            if (Key(a) == sc) return a;
        return -1;
    }
    int ActionOfPad(int in, bool byLabel) const
    {
        for (int a = 0; a < COUNT; a++)
            if (Pad(a, byLabel) == in) return a;
        return -1;
    }

    // Gives `a` the input `in`; the action that had it gets a's old one.
    void SetPad(int a, int in, bool byLabel)
    {
        const int old = Pad(a, byLabel);
        for (int b = 0; b < COUNT; b++)
            if (b != a && Pad(b, byLabel) == in)
            {
                // (All fixed: BUTTONS changes no more. An action that had no button stays without
                // one, as PAD_NONE, so its default does not return.)
                for (int c = 0; c < COUNT; c++)
                {
                    const int p = Pad(c, byLabel);
                    pad[c] = p < 0 ? PAD_NONE : p;
                }
                pad[b] = old < 0 ? PAD_NONE : old;
            }
        pad[a] = in;
    }
    void SetKey(int a, int sc)
    {
        const int old = Key(a);
        for (int b = 0; b < COUNT; b++)
            if (b != a && Key(b) == sc) key[b] = old;
        key[a] = sc;
    }

    // One line of the settings file; whether it was one of these.
    bool Apply(const std::string& k, const std::string& v)
    {
        const bool isKey = !k.compare(0, 8, "map_key_"), isPad = !k.compare(0, 8, "map_pad_");
        if (!isKey && !isPad) return false;
        const std::string name = k.substr(8);
        for (int a = 0; a < COUNT; a++)
            if (name == ActionKey(a))
            {
                if (isKey)
                {
                    const int sc = atoi(v.c_str());
                    key[a] = sc > 0 && sc < SDL_NUM_SCANCODES ? sc : -1;
                }
                else if (v == "none") pad[a] = PAD_NONE;
                else pad[a] = InputFromKey(v);
            }
        return true;
    }
    template <class Out> void Save(Out& f) const
    {
        for (int a = 0; a < COUNT; a++)
        {
            if (key[a] >= 0) f << "map_key_" << ActionKey(a) << "=" << key[a] << "\n";
            if (pad[a] == PAD_NONE) f << "map_pad_" << ActionKey(a) << "=none\n";
            else if (pad[a] >= 0) f << "map_pad_" << ActionKey(a) << "=" << InputKey(pad[a]) << "\n";
        }
    }
    // Into a settings file as it is: its map_ lines replaced by these, the rest left alone (the
    // game list's KEYS AND BUTTONS; the game writes the whole file itself).
    bool SaveTo(const std::string& path) const
    {
        std::vector<std::string> keep;
        if (FILE* f = fopen(path.c_str(), "r"))
        {
            char buf[1024];
            while (fgets(buf, sizeof buf, f))
            {
                std::string l = buf;
                while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
                if (l.compare(0, 8, "map_key_") && l.compare(0, 8, "map_pad_")) keep.push_back(l);
            }
            fclose(f);
        }
        FILE* f = fopen(path.c_str(), "w");
        if (!f) return false;
        for (const std::string& l : keep) fprintf(f, "%s\n", l.c_str());
        for (int a = 0; a < COUNT; a++)
        {
            if (key[a] >= 0) fprintf(f, "map_key_%s=%d\n", ActionKey(a), key[a]);
            if (pad[a] == PAD_NONE) fprintf(f, "map_pad_%s=none\n", ActionKey(a));
            else if (pad[a] >= 0) fprintf(f, "map_pad_%s=%s\n", ActionKey(a), InputKey(pad[a]));
        }
        fclose(f);
        return true;
    }
    // From a settings file (the game list reads it this way).
    void Load(const std::string& path)
    {
        Reset();
        byLabel = true;
        FILE* f = fopen(path.c_str(), "r");
        if (!f) return;
        char buf[512];
        while (fgets(buf, sizeof buf, f))
        {
            std::string l = buf;
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            const size_t eq = l.find('=');
            if (eq == std::string::npos) continue;
            if (l.substr(0, eq) == "buttons") byLabel = l.substr(eq + 1) != "positions";
            else Apply(l.substr(0, eq), l.substr(eq + 1));
        }
        fclose(f);
    }
};

} // namespace Controls
