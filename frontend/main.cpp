// ssrecomp frontend: boots the game on the melonDS core with an SDL2 window,
// or headless with scripted input, screenshots and state hashes for testing.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <chrono>
#include <thread>
#include "sampler.h"
#include <vector>
#include <tuple>
#include <deque>

#if defined(__APPLE__) || defined(_MSC_VER)
// (SDL's package for Visual Studio has SDL.h directly in its include folder.)
#include <SDL.h>
#else
#include <SDL2/SDL.h>
#endif
#if defined(__linux__) // (Android too)
#include <unistd.h>
#endif
#ifndef SSR_NO_PNG
#include <png.h>
#endif
#ifdef __ANDROID__
#include <atomic>
#include <jni.h>
#endif
#if defined(__ANDROID__) && defined(SSR_LAUNCHER)
#include "launchmenu.h"
#endif
#ifdef SSR_UWP
#include <windows.h>
#include <fileapifromapp.h>
#include <roapi.h>
// SDL declares these only for a WINAPI_FAMILY_APP build (__WINRT__), which the C++
// library no longer compiles in; they are in the SDL2.dll built for UWP.
extern "C" {
DECLSPEC int SDLCALL SDL_WinRTRunApp(SDL_main_func mainFunction, void* reserved);
enum { SSR_WINRT_PATH_INSTALLED_LOCATION = 0, SSR_WINRT_PATH_LOCAL_FOLDER = 1 }; // SDL_WinRT_Path
DECLSPEC const char* SDLCALL SDL_WinRTGetFSPathUTF8(int pathType);
}
#endif

#include "NDS.h"
#include "NDSCart.h"
#include "Args.h"
#include "GPU.h"
#include "GPU2D_Soft.h"
#include "SPU.h"
#include "RTC.h"
#include "Savestate.h"
#include "host.h"
#include "layout.h"
#include "ui.h"
#include "merged.h"
#include "gpucompose.h"
#include "battlewide.h"
#include "recomp_rt.h"
#include "recomp_api.h"
#include "GPU3D_Wide.h"

#ifdef __ANDROID__
// The app holds the player's own game, so it cannot update itself as the setup window does:
// at start it asks GitHub for the newest release, and if that is newer than this build
// (SSR_VERSION, from CHANGELOG.md) the game says so, to rebuild it on the PC.
namespace UpdateCheck {
static std::atomic<bool> g_done{false};
static std::string g_newer; // set before g_done

// The body of an HTTPS GET through Java (the NDK has no TLS of its own); empty on failure.
static std::string Get(JNIEnv* env, const char* address)
{
    std::string body;
    auto failed = [&]() {
        if (!env->ExceptionCheck()) return false;
        env->ExceptionClear();
        return true;
    };
    jclass urlClass = env->FindClass("java/net/URL");
    jclass connClass = env->FindClass("java/net/URLConnection");
    jclass streamClass = env->FindClass("java/io/InputStream");
    if (failed() || !urlClass || !connClass || !streamClass) return body;
    jobject url = env->NewObject(urlClass, env->GetMethodID(urlClass, "<init>", "(Ljava/lang/String;)V"), env->NewStringUTF(address));
    if (failed()) return body;
    jobject conn = env->CallObjectMethod(url, env->GetMethodID(urlClass, "openConnection", "()Ljava/net/URLConnection;"));
    if (failed() || !conn) return body;
    env->CallVoidMethod(conn, env->GetMethodID(connClass, "setConnectTimeout", "(I)V"), 10000);
    env->CallVoidMethod(conn, env->GetMethodID(connClass, "setReadTimeout", "(I)V"), 10000);
    env->CallVoidMethod(conn, env->GetMethodID(connClass, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V"),
        env->NewStringUTF("Accept"), env->NewStringUTF("application/vnd.github+json"));
    if (failed()) return body;
    jobject in = env->CallObjectMethod(conn, env->GetMethodID(connClass, "getInputStream", "()Ljava/io/InputStream;"));
    if (failed() || !in) return body;
    jmethodID read = env->GetMethodID(streamClass, "read", "([BII)I");
    jbyteArray buf = env->NewByteArray(4096);
    while (body.size() < (1 << 20))
    {
        jint n = env->CallIntMethod(in, read, buf, 0, 4096);
        if (failed()) { body.clear(); break; }
        if (n < 0) break;
        const size_t at = body.size();
        body.resize(at + n);
        env->GetByteArrayRegion(buf, 0, n, (jbyte*)&body[at]);
    }
    env->CallVoidMethod(in, env->GetMethodID(streamClass, "close", "()V"));
    failed();
    return body;
}

static std::vector<int> Key(const std::string& v)
{
    std::vector<int> key;
    std::stringstream ss(v);
    std::string part;
    while (std::getline(ss, part, '.'))
    {
        if (part.empty() || part.find_first_not_of("0123456789") != std::string::npos) return {};
        key.push_back(atoi(part.c_str()));
    }
    return key;
}

static void Start()
{
    const std::string current = SSR_VERSION;
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JavaVM* vm = nullptr;
    if (current.empty() || !env || env->GetJavaVM(&vm) != JNI_OK) return;
    std::thread([vm, current]() {
        JNIEnv* env = nullptr;
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        const std::string body = Get(env, "https://api.github.com/repos/ClickingMaster/ssrecomp/releases/latest");
        vm->DetachCurrentThread();
        // "tag_name": "v1.2.3"
        std::string latest;
        size_t at = body.find("\"tag_name\"");
        if (at != std::string::npos && (at = body.find('"', body.find(':', at))) != std::string::npos)
        {
            const size_t end = body.find('"', at + 1);
            if (end != std::string::npos) latest = body.substr(at + 1, end - at - 1);
        }
        if (!latest.empty() && latest[0] == 'v') latest.erase(0, 1);
        const std::vector<int> a = Key(latest), b = Key(current);
        if (latest.empty() || a.empty())
            fprintf(stderr, "update check: no answer from GitHub\n");
        else
        {
            fprintf(stderr, "update check: this is %s, the newest release is %s\n", current.c_str(), latest.c_str());
            if (!b.empty() && a > b) g_newer = latest;
        }
        g_done = true;
    }).detach();
}
}
#endif

// With --log-sections and a shot directory, a screenshot is taken around every change of merged scene.
static std::set<uint32_t> g_sceneShots;

// Defined in the 2D renderer: layers not to draw, per engine (see GPU2D_Soft.cpp).
extern unsigned rc_hide_layers[2];

using namespace melonDS;

namespace Host
{
bool StopRequested = false;
int LogLevel = 2;

static std::vector<uint8_t> SaveData;
static bool SaveIsDirty = false;

void SaveDirty(const uint8_t* data, uint32_t len)
{
    SaveData.assign(data, data + len);
    SaveIsDirty = true;
}
}

enum
{
    KEY_A, KEY_B, KEY_SELECT, KEY_START, KEY_RIGHT, KEY_LEFT, KEY_UP, KEY_DOWN, KEY_R, KEY_L, KEY_X, KEY_Y,
};

struct ScriptEvent
{
    uint32_t start, end;
    uint32_t keys = 0;
    bool touch = false;
    int tx = 0, ty = 0;
    // Virtual controller input, for testing the controller paths without hardware.
    uint32_t padButtons = 0;
    float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
};

// Controller state in SDL's button order, filled from a real pad or from a test script.
enum
{
    PAD_A, PAD_B, PAD_X, PAD_Y, PAD_BACK, PAD_GUIDE, PAD_START, PAD_LS, PAD_RS, PAD_LB, PAD_RB,
    PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_COUNT
};

struct PadState
{
    bool btn[PAD_COUNT] = {};
    float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
};

#include "touchpad.h"

static int PadButtonFromName(const std::string& n)
{
    static const char* names[PAD_COUNT] = { "a", "b", "x", "y", "back", "guide", "start", "ls", "rs", "lb", "rb",
        "up", "down", "left", "right" };
    for (int i = 0; i < PAD_COUNT; i++)
        if (n == names[i]) return i;
    return -1;
}

static int KeyFromName(const std::string& n)
{
    static const char* names[] = { "A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L", "X", "Y" };
    for (int i = 0; i < 12; i++)
        if (n == names[i]) return i;
    return -1;
}

// Script lines: "<first frame> <last frame> KEY [KEY...]" or "<first> <last> touch <x> <y>"
static std::vector<ScriptEvent> LoadScript(const std::string& path)
{
    std::vector<ScriptEvent> out;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line))
    {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        ScriptEvent ev;
        std::string tok;
        if (!(ss >> ev.start >> ev.end)) continue;
        while (ss >> tok)
        {
            if (tok == "touch")
            {
                ev.touch = true;
                ss >> ev.tx >> ev.ty;
            }
            else if (tok.rfind("pad:", 0) == 0)
            {
                // pad:a, pad:guide, ... for buttons; pad:rt, pad:lt, pad:rx+, pad:ry-, pad:lx+ ... for axes
                std::string n = tok.substr(4);
                int b = PadButtonFromName(n);
                if (b >= 0) ev.padButtons |= 1u << b;
                else if (n == "rt") ev.rt = 1;
                else if (n == "lt") ev.lt = 1;
                else if (n.size() == 3 && (n[2] == '+' || n[2] == '-'))
                {
                    float v = n[2] == '+' ? 1.0f : -1.0f;
                    if (n.compare(0, 2, "lx") == 0) ev.lx = v;
                    else if (n.compare(0, 2, "ly") == 0) ev.ly = v;
                    else if (n.compare(0, 2, "rx") == 0) ev.rx = v;
                    else if (n.compare(0, 2, "ry") == 0) ev.ry = v;
                    else fprintf(stderr, "script: unknown pad input '%s'\n", tok.c_str());
                }
                else fprintf(stderr, "script: unknown pad input '%s'\n", tok.c_str());
            }
            else
            {
                int k = KeyFromName(tok);
                if (k < 0) fprintf(stderr, "script: unknown key '%s'\n", tok.c_str());
                else ev.keys |= 1u << k;
            }
        }
        out.push_back(ev);
    }
    return out;
}

static uint32_t TopChanges = 0;
static int AudioPeak = 0;
static double AudioSumSq = 0;
static uint64_t AudioSamples = 0;

static uint64_t Fnv1a(const void* data, size_t len, uint64_t h = 0xcbf29ce484222325ull)
{
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++)
    {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

static bool WritePNG(const std::string& path, const uint32_t* pixels, int w, int h)
{
    std::vector<uint8_t> rgb((size_t)w * h * 3);
    for (int i = 0; i < w * h; i++)
    {
        rgb[i * 3 + 0] = (pixels[i] >> 16) & 0xFF;
        rgb[i * 3 + 1] = (pixels[i] >> 8) & 0xFF;
        rgb[i * 3 + 2] = pixels[i] & 0xFF;
    }
#ifdef SSR_NO_PNG
    (void)path;
    return false;
#else
    png_image img;
    memset(&img, 0, sizeof(img));
    img.version = PNG_IMAGE_VERSION;
    img.width = w;
    img.height = h;
    img.format = PNG_FORMAT_RGB;
    return png_image_write_to_file(&img, path.c_str(), 0, rgb.data(), 0, nullptr) != 0;
#endif
}

static std::vector<uint8_t> ReadFile(const std::string& path)
{
    std::vector<uint8_t> data;
#ifdef __ANDROID__
    // Relative paths are files packed in the APK (the ROM); SDL reads both kinds.
    if (SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb"))
    {
        Sint64 len = SDL_RWsize(rw);
        if (len > 0)
        {
            data.resize((size_t)len);
            if (SDL_RWread(rw, data.data(), 1, (size_t)len) != (size_t)len) data.clear();
        }
        SDL_RWclose(rw);
    }
    return data;
#endif
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return data;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    data.resize(len);
    if (fread(data.data(), 1, len, f) != (size_t)len) data.clear();
    fclose(f);
    return data;
}

static void FlushSave(const std::string& path)
{
    if (!Host::SaveIsDirty || path.empty()) return;
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return;
    bool ok = fwrite(Host::SaveData.data(), 1, Host::SaveData.size(), f) == Host::SaveData.size();
    ok = fclose(f) == 0 && ok;
    if (!ok) return;
    // std::filesystem::rename replaces an existing file on Windows too (plain rename()
    // fails there when the target exists, which kept every save after the first).
    std::error_code err;
    std::filesystem::rename(tmp, path, err);
    if (err)
    {
        fprintf(stderr, "cannot write save %s: %s\n", path.c_str(), err.message().c_str());
        return;
    }
    Host::SaveIsDirty = false;
}

struct Options
{
    std::string rom;
    std::string save;
    std::string script;
    std::string shotDir;
    std::set<uint32_t> shotAt;
    uint32_t shotEvery = 0;
    uint32_t hashEvery = 0;
    std::string nav;                     // test aid: walk to these tiles, "x,z;x,z;...", holding the script meanwhile
    bool navStopOnBattle = false;        // end --nav when a battle starts, so the script can play it
    uint32_t stopAt = 0;                 // real frame at which to quit, whatever the script clock says
    uint32_t navAfter = 0;               // script frame at which --nav starts
    std::vector<std::pair<int, std::string>> navMapIds; // grids of single interior maps
    std::string navMap;                  // permission grid from tools/navmap.py for --nav
    uint32_t posEvery = 0;               // test aid: print map and player tile every N frames
    uint32_t frames = 0;
    bool headless = false;
    bool noSave = false;
    bool recomp = true;
    bool lockstep = false;
    bool fullscreen = false;
    bool swapped = false;
    bool logSections = false;
    bool noWide = false;                 // turn the widescreen 3D renderer off
    bool benchCompose = false;           // headless: compose the merged picture every frame, to time it
    bool autoFocus = true;
    bool layoutGiven = false;
    bool audioStats = false;
    int interp60 = -1;                   // 3D frame interpolation: -1 = from settings (off when headless)
    bool battleMash = false;             // test aid: tap A through battles, pausing the script meanwhile
    bool countChanges = false;           // report how many frames changed the top screen
    bool shotLayers = false;             // also write a per-pixel layer map with each screenshot
    std::set<uint32_t> traceFuncsAt;     // frames at which to list the functions that just ran
    std::string symbols = "gen/symbols.txt";
    std::string config;          // settings file
    std::string stateLoad;       // snapshot to resume from
    std::string stateSave;       // snapshot to write
    uint32_t stateSaveAt = 0;    // frame at which to write it
    int composeW = 0, composeH = 0; // screenshots use the single-display layout at this size
    int scale = 3;
    std::string layout = "merged";
};

static void Usage()
{
    fprintf(stderr,
        "usage: ssrecomp <rom.nds> [options]\n"
        "  --save <file>        save file (default: <rom>.sav)\n"
        "  --no-save            never write the save file\n"
        "  --layout <name>      merged | hybrid | side | vertical | top | bottom (default merged)\n"
        "  --config <file>      settings file (default: ssrecomp.cfg beside the save)\n"
        "  --scale <n>          window scale (default 3)\n"
        "  --fullscreen         start in fullscreen (F11 toggles)\n"
        "  --headless           no window or audio; run as fast as possible\n"
        "  --frames <n>         stop after n frames\n"
        "  --input <file>       scripted input\n"
        "  --swap               start with the bottom screen as the large one (hybrid)\n"
        "  --no-auto-focus      keep the top screen large instead of choosing automatically\n"
        "  --compose <WxH>      screenshots show the chosen layout at this size instead of\n"
        "                       the two raw screens stacked\n"
        "  --state-save <file> --state-at <frame>   write a snapshot at that frame\n"
        "  --state-load <file>  resume from a snapshot (frame numbering continues)\n"
        "  --shot-dir <dir>     where screenshots go\n"
        "  --shot-at <a,b,c>    screenshot these frames\n"
        "  --shot-every <n>     screenshot every n frames\n"
        "  --hash-every <n>     print a state hash every n frames\n"
        "  --log-sections       print which overlays run code and which merged scene is shown, on change\n"
        "  --interp60 / --no-interp60   force the 60 fps 3D interpolation on or off\n"
        "  --battle-mash        while a battle is running, tap A and hold the input script (and the\n"
        "                       frame used by --state-at) still, so scripts can walk through grass\n"
        "  --count-changes      report on how many frames the top screen image changed\n"
        "  --audio-stats        print the peak and RMS level of the generated audio at exit\n"
        "  --shot-layers        with each screenshot, also write layers_<frame>.png colouring every\n"
        "                       pixel by the 2D layer it came from\n"

        "  --trace-funcs <a,b>  at these frames, write the names of all game functions entered in\n"
        "                       the last two frames to funcs_<frame>.txt in the screenshot directory\n"
        "  --symbols <file>     function list for --trace-funcs (default gen/symbols.txt)\n"
        "  --interp             run the ARM9 on the interpreter instead of recompiled code\n"
        "  --lockstep           run interpreter and recompiled code side by side and compare\n"
        "                       every ARM9 instruction (headless only)\n"
        "  --log <0-3>          core log level (0 = everything)\n");
}

static bool ParseArgs(int argc, char** argv, Options& o)
{
    for (int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--save") o.save = next();
        else if (a == "--no-save") o.noSave = true;
        else if (a == "--layout") { o.layout = next(); o.layoutGiven = true; }
        else if (a == "--config") o.config = next();
        else if (a == "--audio-stats") o.audioStats = true;
        else if (a == "--shot-layers") o.shotLayers = true;
        else if (a == "--count-changes") o.countChanges = true;
        else if (a == "--battle-mash") o.battleMash = true;
        else if (a == "--interp60") o.interp60 = 1;
        else if (a == "--no-interp60") o.interp60 = 0;

        else if (a == "--symbols") o.symbols = next();
        else if (a == "--trace-funcs")
        {
            std::istringstream ss(next());
            std::string tok;
            while (std::getline(ss, tok, ',')) o.traceFuncsAt.insert(strtoul(tok.c_str(), nullptr, 10));
        }
        else if (a == "--scale") o.scale = atoi(next().c_str());
        else if (a == "--headless") o.headless = true;
        else if (a == "--fullscreen") o.fullscreen = true;
        else if (a == "--swap") o.swapped = true;
        else if (a == "--log-sections") o.logSections = true;
        else if (a == "--no-wide") o.noWide = true;
        else if (a == "--bench-compose") o.benchCompose = true;
        else if (a == "--no-auto-focus") o.autoFocus = false;
        else if (a == "--state-save") o.stateSave = next();
        else if (a == "--state-at") o.stateSaveAt = strtoul(next().c_str(), nullptr, 10);
        else if (a == "--state-load") o.stateLoad = next();
        else if (a == "--compose") sscanf(next().c_str(), "%dx%d", &o.composeW, &o.composeH);
        else if (a == "--interp") o.recomp = false;
        else if (a == "--lockstep") { o.lockstep = true; o.headless = true; o.noSave = true; }
        else if (a == "--frames") o.frames = strtoul(next().c_str(), nullptr, 10);
        else if (a == "--input") o.script = next();
        else if (a == "--shot-dir") o.shotDir = next();
        else if (a == "--shot-every") o.shotEvery = strtoul(next().c_str(), nullptr, 10);
        else if (a == "--nav") o.nav = next();
        else if (a == "--nav-stop-on-battle") o.navStopOnBattle = true;
        else if (a == "--stop-at") o.stopAt = strtoul(next().c_str(), nullptr, 10);
        else if (a == "--nav-after") o.navAfter = strtoul(next().c_str(), nullptr, 10);
        else if (a == "--nav-map") o.navMap = next();
        else if (a == "--nav-map-id")
        {
            std::string v = next();
            size_t eq = v.find('=');
            if (eq != std::string::npos) o.navMapIds.push_back({ atoi(v.c_str()), v.substr(eq + 1) });
        }
        else if (a == "--pos-every") o.posEvery = strtoul(next().c_str(), nullptr, 10);
        else if (a == "--hash-every") o.hashEvery = strtoul(next().c_str(), nullptr, 10);
        else if (a == "--log") Host::LogLevel = atoi(next().c_str());
        else if (a == "--shot-at")
        {
            std::istringstream ss(next());
            std::string tok;
            while (std::getline(ss, tok, ',')) o.shotAt.insert(strtoul(tok.c_str(), nullptr, 10));
        }
        else if (a[0] == '-') return false;
        else o.rom = a;
    }
    return !o.rom.empty();
}

// The DS clock. The game lights the overworld by the time of day (dark blue at night,
// orange in the evening), so it follows the computer's clock as on a real DS; the tests
// use a fixed noon so their pictures do not depend on when they run.
// SSR_CLOCK="YYYY-MM-DD HH:MM" overrides it.
static void SetClock(NDS& nds, bool fixed)
{
    int y = 2026, mo = 1, d = 1, h = 12, mi = 0, sec = 0;
    if (const char* c = getenv("SSR_CLOCK"))
        sscanf(c, "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi);
    else if (!fixed)
    {
        time_t now = time(nullptr);
        struct tm t = *localtime(&now);
        y = t.tm_year + 1900; mo = t.tm_mon + 1; d = t.tm_mday;
        h = t.tm_hour; mi = t.tm_min; sec = t.tm_sec;
    }
    nds.RTC.SetDateTime(y, mo, d, h, mi, sec);
}

static std::unique_ptr<NDS> MakeConsole(const std::vector<uint8_t>& rom, const std::vector<uint8_t>& sav, bool fixedClock)
{
    NDSCart::NDSCartArgs cartArgs;
    if (!sav.empty())
    {
        cartArgs.SRAM = std::make_unique<u8[]>(sav.size());
        memcpy(cartArgs.SRAM.get(), sav.data(), sav.size());
        cartArgs.SRAMLength = (u32)sav.size();
    }
    auto cart = NDSCart::ParseROM(rom.data(), (u32)rom.size(), nullptr, std::move(cartArgs));
    if (!cart)
    {
        fprintf(stderr, "ROM rejected by the core\n");
        return nullptr;
    }
    NDSArgs args;
    args.JIT = std::nullopt;
    auto nds = std::make_unique<NDS>(std::move(args));
    nds->SetNDSCart(std::move(cart));
    nds->Reset();
    nds->SetupDirectBoot("game.nds");
    SetClock(*nds, fixedClock);
    nds->Start();
    return nds;
}

struct TraceState
{
    u32 r[16];
    u32 cpsr;
    s32 cycles;
    u64 ts;
};

static std::vector<TraceState> TraceLog;
static size_t TracePos = 0;
static uint32_t TraceFrame = 0;
static bool TraceFailed = false;
static u32 TracePrevR15 = 0, TracePrevCPSR = 0;

static void TraceCapture(ARMv5* cpu, TraceState& st)
{
    memcpy(st.r, cpu->R, sizeof(st.r));
    st.cpsr = cpu->CPSR;
    st.cycles = cpu->Cycles;
    st.ts = cpu->NDS.ARM9Timestamp;
}

static void TraceRecord(ARMv5* cpu, void*)
{
    TraceLog.emplace_back();
    TraceCapture(cpu, TraceLog.back());
}

static void TraceDump(const char* tag, const TraceState& st)
{
    fprintf(stderr, "  %s:", tag);
    for (int i = 0; i < 16; i++) fprintf(stderr, " r%d=%08x", i, st.r[i]);
    fprintf(stderr, " cpsr=%08x cycles=%d ts=%llu\n", st.cpsr, st.cycles, (unsigned long long)st.ts);
}

static void TraceCompare(ARMv5* cpu, void*)
{
    if (TraceFailed) return;
    TraceState now;
    memset(&now, 0, sizeof(now));
    TraceCapture(cpu, now);
    bool bad = TracePos >= TraceLog.size();
    if (!bad)
    {
        const TraceState& want = TraceLog[TracePos];
        bad = memcmp(want.r, now.r, sizeof(now.r)) != 0 || want.cpsr != now.cpsr ||
            want.cycles != now.cycles || want.ts != now.ts;
    }
    if (bad)
    {
        TraceFailed = true;
        bool thumb = (TracePrevCPSR & 0x20) != 0;
        fprintf(stderr, "LOCKSTEP MISMATCH frame %u instruction #%zu at %08x (%s) opcode %08x\n",
            TraceFrame, TracePos, TracePrevR15 - (thumb ? 2 : 4), thumb ? "thumb" : "arm", cpu->CurInstr);
        if (TracePos < TraceLog.size()) TraceDump("interp", TraceLog[TracePos]);
        else fprintf(stderr, "  interp: ran fewer instructions this frame (%zu)\n", TraceLog.size());
        TraceDump("recomp", now);
    }
    TracePos++;
    TracePrevR15 = cpu->R[15];
    TracePrevCPSR = cpu->CPSR;
}

// Decides which DS screen should be the large one on a single display.
//
// Two signals: a screen showing one flat colour has nothing to look at, and some
// applications (recognised by their per-frame function having just run) are driven
// from the bottom screen. A manual swap overrides the decision until it next changes.
struct AutoFocus
{
    int mode = 0;              // 0 automatic, 1 top screen large, 2 bottom screen large
    bool wantBottom = false;   // current automatic decision
    bool manualFlip = false;
    int pending = 0;           // frames the opposite decision has been stable
    std::vector<const RcApp*> bottomApps;

    void Init()
    {
        // Applications whose controls and content are on the bottom screen.
        static const char* names[] = {
            "Bag", "PartyMenu", "NamingScreen", "Pokegear", "GearConfigure", "GearMap", "GearPhone", "GearRadio",
            "TownMap", "PCBox", "EasyChat", "VoltorbFlip", "Pokedex", "ScratchOffCards", "AlphPuzzle",
            "TrainerCard", "TrainerCardSignature", "BugContestSwapMon", "WirelessTradeSelectMon", "OptionsMenu",
            "MainMenu", "CheckSave", "DeleteSave",
        };
        for (unsigned i = 0; i < rc_num_apps; i++)
            for (const char* n : names)
                if (!strcmp(rc_apps[i].name, n)) bottomApps.push_back(&rc_apps[i]);
    }

    static bool Blank(const u32* fb)
    {
        u32 c = fb[0] & 0xFFFFFF;
        for (int i = 1; i < 256 * 192; i++)
            if ((fb[i] & 0xFFFFFF) != c) return false;
        return true;
    }

    // appBottom: one of bottomApps is starting, running or ending (its probes, which see
    // the start and end too, unlike the check below).
    void Update(const u32* top, const u32* bot, bool fieldNow = false, bool appBottom = false)
    {
        bool topBlank = Blank(top), botBlank = Blank(bot);
        bool want = wantBottom;
        int hold = 12;
        if (topBlank && botBlank)
            ; // fade or load: keep whatever is showing
        else if (appBottom && !fieldNow)
        {
            // Straight to the bottom screen, even while it is still dark and the other one
            // is not: an application loading draws one screen before the other, and
            // following that showed the top screen first and then switched.
            want = true;
            hold = 1;
        }
        else if (fieldNow && !topBlank)
        {
            // Back in the overworld: the application that was open is over, even though
            // its code ran a few frames ago.
            want = false;
            hold = 1;
        }
        else if (topBlank)
        {
            // A top screen of one colour is also what a flash or fade looks like for a
            // moment, so this has to last before the bottom screen is shown instead.
            want = true;
            hold = 45;
        }
        else if (botBlank)
            want = false;
        else
        {
            want = false;
            for (const RcApp* app : bottomApps)
                if (rc_ran_recently(app->section, app->execAddr, 6)) want = true;
        }
        if (want == wantBottom)
        {
            pending = 0;
            return;
        }
        // Require the new state to hold briefly so transitions do not flicker.
        if (++pending >= hold)
        {
            wantBottom = want;
            manualFlip = false;
            pending = 0;
        }
    }

    bool Swapped() const
    {
        bool base = mode == 0 ? wantBottom : mode == 2;
        return base != manualFlip;
    }
};

// Software version of what the window shows, for screenshots.
static void Compose(const Layout& layout, int w, int h, const u32* top, const u32* bot, std::vector<uint32_t>& out)
{
    out.assign((size_t)w * h, 0);
    SDL_Rect rects[2];
    LayoutRects(layout, w, h, rects);
    int first = LayoutDrawFirst(layout);
    for (int n = 0; n < 2; n++)
    {
        int i = n == 0 ? first : 1 - first;
        const SDL_Rect& r = rects[i];
        const u32* src = i == 0 ? top : bot;
        if (r.w <= 0 || r.h <= 0) continue;
        for (int y = 0; y < r.h; y++)
        {
            int dy = r.y + y;
            if (dy < 0 || dy >= h) continue;
            const u32* row = src + (y * 192 / r.h) * 256;
            for (int x = 0; x < r.w; x++)
            {
                int dx = r.x + x;
                if (dx >= 0 && dx < w) out[(size_t)dy * w + dx] = row[x * 256 / r.w];
            }
        }
    }
}

static uint32_t KeyboardKeys()
{
    const Uint8* st = SDL_GetKeyboardState(nullptr);
    uint32_t k = 0;
    if (st[SDL_SCANCODE_X]) k |= 1 << KEY_A;
    if (st[SDL_SCANCODE_Z]) k |= 1 << KEY_B;
    if (st[SDL_SCANCODE_S]) k |= 1 << KEY_X;
    if (st[SDL_SCANCODE_A]) k |= 1 << KEY_Y;
    if (st[SDL_SCANCODE_Q]) k |= 1 << KEY_L;
    if (st[SDL_SCANCODE_W]) k |= 1 << KEY_R;
    if (st[SDL_SCANCODE_RETURN]) k |= 1 << KEY_START;
    if (st[SDL_SCANCODE_RSHIFT] || st[SDL_SCANCODE_BACKSPACE]) k |= 1 << KEY_SELECT;
    if (st[SDL_SCANCODE_UP]) k |= 1 << KEY_UP;
    if (st[SDL_SCANCODE_DOWN]) k |= 1 << KEY_DOWN;
    if (st[SDL_SCANCODE_LEFT]) k |= 1 << KEY_LEFT;
    if (st[SDL_SCANCODE_RIGHT]) k |= 1 << KEY_RIGHT;
    return k;
}

// The controllers in use: all of them at once, as which one is first is not to be relied on
// (Windows often lists a virtual or idle one first: Steam's, DS4Windows', a wheel's). Those
// SDL knows the layout of are game controllers; any other joystick (a generic USB pad) is
// read with the usual layout: the first buttons as A B X Y, L R, Select Start, the hat and
// the first two axes as the D-pad and the left stick.
struct Pads
{
    std::vector<SDL_GameController*> controllers;
    std::vector<SDL_Joystick*> joysticks;
    // Per joystick, whether its first two axes rest in the middle (1), not (2) or not known
    // yet (0, until half a second after it was opened, when its state has surely arrived):
    // some keyboards, mice and wheels report themselves as joysticks with an axis resting at
    // an end, which would walk the player on its own. And when it was opened.
    std::vector<int> axesUsable;
    std::vector<Uint32> openedAt;

    bool Has(SDL_JoystickID id) const
    {
        for (SDL_GameController* c : controllers)
            if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(c)) == id) return true;
        for (SDL_Joystick* j : joysticks)
            if (SDL_JoystickInstanceID(j) == id) return true;
        return false;
    }
    bool Empty() const { return controllers.empty() && joysticks.empty(); }

    // Opens the device at `index` unless already open; its name, or null.
    const char* Open(int index)
    {
        if (getenv("SSR_NO_PAD") || Has(SDL_JoystickGetDeviceInstanceID(index))) return nullptr; // tests must not hear a real controller
        if (SDL_IsGameController(index))
        {
            SDL_GameController* c = SDL_GameControllerOpen(index);
            if (!c)
            {
                fprintf(stderr, "controller %d could not be opened: %s\n", index, SDL_GetError());
                return nullptr;
            }
            controllers.push_back(c);
            const char* name = SDL_GameControllerName(c);
            fprintf(stderr, "controller: %s\n", name ? name : "?");
            return name ? name : "";
        }
#ifdef __ANDROID__
        // Android reports some of a device's own keys as a joystick, and using one hides the
        // on-screen controls.
        return nullptr;
#endif
        SDL_Joystick* j = SDL_JoystickOpen(index);
        if (!j)
        {
            fprintf(stderr, "joystick %d could not be opened: %s\n", index, SDL_GetError());
            return nullptr;
        }
        // Not one without buttons (an accelerometer, a wheel's pedals alone).
        if (SDL_JoystickNumButtons(j) < 2)
        {
            SDL_JoystickClose(j);
            return nullptr;
        }
        joysticks.push_back(j);
        axesUsable.push_back(SDL_JoystickNumAxes(j) >= 2 ? 0 : 2);
        openedAt.push_back(SDL_GetTicks());
        const char* name = SDL_JoystickName(j);
        fprintf(stderr, "joystick without a known layout: %s (%d buttons, %d axes, %d hats)\n", name ? name : "?",
            SDL_JoystickNumButtons(j), SDL_JoystickNumAxes(j), SDL_JoystickNumHats(j));
        return name ? name : "";
    }

    // Closes the device with instance `id`; whether it was open.
    bool Close(SDL_JoystickID id)
    {
        for (size_t i = 0; i < controllers.size(); i++)
            if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controllers[i])) == id)
            {
                SDL_GameControllerClose(controllers[i]);
                controllers.erase(controllers.begin() + i);
                return true;
            }
        for (size_t i = 0; i < joysticks.size(); i++)
            if (SDL_JoystickInstanceID(joysticks[i]) == id)
            {
                SDL_JoystickClose(joysticks[i]);
                joysticks.erase(joysticks.begin() + i);
                axesUsable.erase(axesUsable.begin() + i);
                openedAt.erase(openedAt.begin() + i);
                return true;
            }
        return false;
    }
};

static PadState ReadPad(Pads& pads)
{
    PadState st;
    // Each button pressed on any of them; each stick as far as on the one pushed furthest.
    auto merge = [](float& to, float v) { if (fabsf(v) > fabsf(to)) to = v; };
    for (SDL_GameController* pad : pads.controllers)
    {
        for (int i = 0; i < PAD_COUNT; i++)
            st.btn[i] |= SDL_GameControllerGetButton(pad, (SDL_GameControllerButton)i) != 0;
        auto axis = [&](SDL_GameControllerAxis a) { return SDL_GameControllerGetAxis(pad, a) / 32767.0f; };
        merge(st.lx, axis(SDL_CONTROLLER_AXIS_LEFTX));
        merge(st.ly, axis(SDL_CONTROLLER_AXIS_LEFTY));
        merge(st.rx, axis(SDL_CONTROLLER_AXIS_RIGHTX));
        merge(st.ry, axis(SDL_CONTROLLER_AXIS_RIGHTY));
        merge(st.lt, axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT));
        merge(st.rt, axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT));
    }
    for (size_t k = 0; k < pads.joysticks.size(); k++)
    {
        SDL_Joystick* j = pads.joysticks[k];
        static const int order[] = { PAD_A, PAD_B, PAD_X, PAD_Y, PAD_LB, PAD_RB, PAD_BACK, PAD_START };
        const int n = std::min(SDL_JoystickNumButtons(j), (int)(sizeof(order) / sizeof(order[0])));
        for (int i = 0; i < n; i++)
            st.btn[order[i]] |= SDL_JoystickGetButton(j, i) != 0;
        if (SDL_JoystickNumHats(j) > 0)
        {
            const Uint8 h = SDL_JoystickGetHat(j, 0);
            st.btn[PAD_UP] |= (h & SDL_HAT_UP) != 0;
            st.btn[PAD_DOWN] |= (h & SDL_HAT_DOWN) != 0;
            st.btn[PAD_LEFT] |= (h & SDL_HAT_LEFT) != 0;
            st.btn[PAD_RIGHT] |= (h & SDL_HAT_RIGHT) != 0;
        }
        int& usable = pads.axesUsable[k];
        if (usable == 0 && SDL_GetTicks() - pads.openedAt[k] >= 500)
            usable = (abs(SDL_JoystickGetAxis(j, 0)) < 8000 && abs(SDL_JoystickGetAxis(j, 1)) < 8000) ? 1 : 2;
        if (usable == 1)
        {
            merge(st.lx, SDL_JoystickGetAxis(j, 0) / 32767.0f);
            merge(st.ly, SDL_JoystickGetAxis(j, 1) / 32767.0f);
        }
    }
    return st;
}

// DS buttons from the controller. With byLabel the button printed "A" is A on any pad;
// otherwise the DS diamond is mapped by position (right = A, bottom = B).
static uint32_t PadKeys(const PadState& p, bool byLabel)
{
    uint32_t k = 0;
    if (p.btn[byLabel ? PAD_A : PAD_B]) k |= 1 << KEY_A;
    if (p.btn[byLabel ? PAD_B : PAD_A]) k |= 1 << KEY_B;
    if (p.btn[byLabel ? PAD_X : PAD_Y]) k |= 1 << KEY_X;
    if (p.btn[byLabel ? PAD_Y : PAD_X]) k |= 1 << KEY_Y;
    if (p.btn[PAD_LB]) k |= 1 << KEY_L;
    if (p.btn[PAD_RB]) k |= 1 << KEY_R;
    if (p.btn[PAD_START]) k |= 1 << KEY_START;
    if (p.btn[PAD_BACK]) k |= 1 << KEY_SELECT;
    if (p.btn[PAD_UP] || p.ly < -0.5f) k |= 1 << KEY_UP;
    if (p.btn[PAD_DOWN] || p.ly > 0.5f) k |= 1 << KEY_DOWN;
    if (p.btn[PAD_LEFT] || p.lx < -0.5f) k |= 1 << KEY_LEFT;
    if (p.btn[PAD_RIGHT] || p.lx > 0.5f) k |= 1 << KEY_RIGHT;
    return k;
}

// Settings that persist between runs.
struct Settings
{
    std::string layout = "merged";
    int focusMode = 0;
    bool fullscreen = false;
    bool smooth = false;
    bool wide = true;   // widescreen 3D overworld
    int wideLines = 720; // height the widescreen 3D is drawn at, at most
    bool byLabel = true;
    bool fps60 = true;
    // Right stick as a touch cursor, right trigger as the press, and the stick clicks that
    // switch screens. Off unless asked for: brushing the stick put the bottom screen up.
    bool padTouch = false;
    bool showFps = false; // frames shown per second, in a corner

    void Load(const std::string& path)
    {
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line))
        {
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            // "view" replaced the older "layout" key when the merged view became the default.
            if (k == "view") layout = v;
            else if (k == "large_screen") focusMode = v == "top" ? 1 : v == "bottom" ? 2 : 0;
            else if (k == "fullscreen") fullscreen = v == "1";
            else if (k == "smooth") smooth = v == "1";
            else if (k == "wide") wide = v == "1";
            else if (k == "wide_lines") wideLines = std::clamp(atoi(v.c_str()), 192, 2160);
            else if (k == "buttons") byLabel = v != "positions";
            else if (k == "fps60") fps60 = v == "1";
            else if (k == "pad_touch") padTouch = v == "1";
            else if (k == "show_fps") showFps = v == "1";
        }
    }

    void Save(const std::string& path) const
    {
        std::ofstream f(path);
        f << "view=" << layout << "\n";
        f << "large_screen=" << (focusMode == 1 ? "top" : focusMode == 2 ? "bottom" : "auto") << "\n";
        f << "fullscreen=" << (fullscreen ? 1 : 0) << "\n";
        f << "smooth=" << (smooth ? 1 : 0) << "\n";
        f << "wide=" << (wide ? 1 : 0) << "\n";
        f << "wide_lines=" << wideLines << "\n";
        f << "buttons=" << (byLabel ? "labels" : "positions") << "\n";
        f << "fps60=" << (fps60 ? 1 : 0) << "\n";
        f << "pad_touch=" << (padTouch ? 1 : 0) << "\n";
        f << "show_fps=" << (showFps ? 1 : 0) << "\n";
    }
};

static const char* LayoutName(LayoutMode m)
{
    switch (m)
    {
    case LayoutMode::Merged: return "merged";
    case LayoutMode::Hybrid: return "hybrid";
    case LayoutMode::Side: return "side";
    case LayoutMode::Vertical: return "vertical";
    case LayoutMode::Top: return "top";
    default: return "bottom";
    }
}

// Draws the overlay canvas over an ARGB image of the same size.
static void BlendCanvas(const Canvas& c, std::vector<uint32_t>& img)
{
    for (size_t i = 0; i < img.size() && i < c.px.size(); i++)
    {
        uint32_t o = c.px[i];
        uint32_t a = o >> 24;
        if (!a) continue;
        if (a == 255)
        {
            img[i] = o;
            continue;
        }
        uint32_t d = img[i];
        uint32_t r = (((o >> 16) & 0xFF) * a + ((d >> 16) & 0xFF) * (255 - a)) / 255;
        uint32_t g = (((o >> 8) & 0xFF) * a + ((d >> 8) & 0xFF) * (255 - a)) / 255;
        uint32_t b2 = ((o & 0xFF) * a + (d & 0xFF) * (255 - a)) / 255;
        img[i] = 0xFF000000 | (r << 16) | (g << 8) | b2;
    }
}

static bool WriteSnapshot(NDS& nds, const std::string& path, uint32_t frame)
{
    Savestate st;
    if (!nds.DoSavestate(&st) || st.Error) return false;
    st.Finish();
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fwrite(&frame, 4, 1, f);
    fwrite(st.Buffer(), 1, st.Length(), f);
    fclose(f);
    return true;
}

// Snapshot file: 4-byte frame number, then the core's savestate.
static bool ReadSnapshot(NDS& nds, const std::string& path, uint32_t* frame)
{
    std::vector<uint8_t> blob = ReadFile(path);
    if (blob.size() <= 4) return false;
    Savestate st(blob.data() + 4, (u32)blob.size() - 4, false);
    if (st.Error || !nds.DoSavestate(&st)) return false;
    if (frame) memcpy(frame, blob.data(), 4);
    rc_reset();
    return true;
}

#ifdef SSR_UWP
// The first .nds file in dir or (depth levels down) in a folder in it; empty if none.
static std::wstring FindDevelopmentRom(const std::wstring& dir, int depth)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExFromAppW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::wstring found;
    std::vector<std::wstring> folders;
    do
    {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) folders.push_back(dir + L"\\" + name);
        else if (name.size() > 4 && _wcsicmp(name.c_str() + name.size() - 4, L".nds") == 0)
        {
            found = dir + L"\\" + name;
            break;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (depth > 0)
        for (const auto& f : folders)
            if (found.empty()) found = FindDevelopmentRom(f, depth - 1);
    return found;
}
#endif

int main(int argc, char** argv)
{
    Options opt;
#ifdef __ANDROID__
    // Started from the app icon: the ROM is packed in the APK, the save and the settings
    // go in the app's own storage.
    std::vector<std::string> androidArgs;
    std::vector<char*> androidArgv;
    if (argc < 2)
    {
        const char* dir = SDL_AndroidGetInternalStoragePath();
        // Files the emulator core keeps by relative name go there too.
        if (dir) chdir(dir);
        // Messages go to log.txt (Android drops them otherwise), with the frame timing,
        // for bug reports: in the app's folder on the shared storage
        // (Android/data/io.github.clickingmaster.ssrecomp/files), which a file manager or
        // a PC over USB can open, or else beside the save.
        const char* ext = SDL_AndroidGetExternalStoragePath();
        if (!ext || !freopen((std::string(ext) + "/log.txt").c_str(), "w", stderr))
            freopen("log.txt", "w", stderr);
        setvbuf(stderr, nullptr, _IOLBF, 0);
        setenv("SSR_PERF", "1", 0);
#ifdef SSR_LAUNCHER
        // The TV launcher (tools/android/tv_launcher.py) holds neither the ROM nor the
        // game's code: its start menu finds the player's ROM on the device. The save and
        // the settings go in the folder ssrecomp on the shared storage, so that they stay
        // when the app is uninstalled (an update signed with another key needs that).
        SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0)
        {
            fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return 1;
        }
        // Reading the Download folder and USB sticks (the app targets Android 10, whose
        // storage rules allow it, see tools/android/tv/AndroidManifest.xml).
        if (!SDL_AndroidRequestPermission("android.permission.WRITE_EXTERNAL_STORAGE"))
            fprintf(stderr, "no permission to read the shared storage\n");
        std::string saveDir = "/storage/emulated/0/ssrecomp";
        std::error_code ec;
        std::filesystem::create_directories(saveDir, ec);
        if (ec) saveDir = dir ? dir : ".";
        std::string last;
        std::getline(std::ifstream(saveDir + "/last-rom.txt"), last);
        const std::string rom = LaunchMenu::Run(ext ? ext : "", last);
        if (rom.empty()) return 0;
        std::ofstream(saveDir + "/last-rom.txt") << rom << "\n";
        fprintf(stderr, "ROM: %s\n", rom.c_str());
        androidArgs = { "ssrecomp", rom, "--save", saveDir + "/soulsilver.sav", "--config", saveDir + "/ssrecomp.cfg",
                        "--fullscreen", "--interp" };
#else
        androidArgs = { "ssrecomp", "rom.nds", "--save", std::string(dir ? dir : ".") + "/soulsilver.sav", "--fullscreen" };
#endif
        for (auto& a : androidArgs) androidArgv.push_back(a.data());
        argc = (int)androidArgv.size();
        argv = androidArgv.data();
    }
#endif
#ifdef SSR_UWP
    // Started from the Xbox's (or Windows') app list. The player puts the ROM in the app's
    // own folder (LocalState, which the Xbox Device Portal's file explorer can open; the
    // package from tools/xbox/xbox.py does not hold it); the save, the settings and the
    // messages go there too.
    std::vector<std::string> uwpArgs;
    std::vector<char*> uwpArgv;
    if (argc < 2)
    {
        const char* installed = SDL_WinRTGetFSPathUTF8(SSR_WINRT_PATH_INSTALLED_LOCATION);
        const char* local = SDL_WinRTGetFSPathUTF8(SSR_WINRT_PATH_LOCAL_FOLDER);
        const std::string dir = local ? local : ".";
        // Files the emulator core keeps by relative name go there too.
        std::error_code ec;
        std::filesystem::current_path(std::filesystem::u8path(dir), ec);
        if (freopen((dir + "\\game.log").c_str(), "w", stderr))
            setvbuf(stderr, nullptr, _IONBF, 0); // Windows has no line buffering
        // Frame timings in game.log every 5 seconds, for reports from the console. And the
        // widescreen 3D's drawing threads: Xbox One's Jaguar runs one thread per core (the
        // renderer counts on two, as PC processors have), with the emulation and the 3D
        // thread on two of the cores a game gets.
        if (!getenv("SSR_PERF")) _putenv_s("SSR_PERF", "1");
        // The console reports all 8 cores, but a game gets only some of them: Windows' CPU
        // sets may say how many (those allocated to this process).
        // With more busy threads than cores, the 3D threads take turns and the emulation
        // waits for them (measured: a DS-size 3D frame took 14-23 ms beside 4 drawing threads).
        int cores = 0, total = 0, mine = 0, taken = 0;
        {
            ULONG len = 0;
            GetSystemCpuSetInformation(nullptr, 0, &len, GetCurrentProcess(), 0);
            std::vector<char> buf(len);
            if (len && GetSystemCpuSetInformation((PSYSTEM_CPU_SET_INFORMATION)buf.data(), len, &len, GetCurrentProcess(), 0))
                for (ULONG off = 0; off < len;)
                {
                    const auto* info = (const SYSTEM_CPU_SET_INFORMATION*)(buf.data() + off);
                    if (!info->Size) break;
                    if (info->Type == CpuSetInformation)
                    {
                        total++;
                        if (info->CpuSet.AllocatedToTargetProcess) mine++;
                        else if (info->CpuSet.Allocated || info->CpuSet.Parked) taken++;
                    }
                    off += info->Size;
                }
            cores = mine;
        }
        fprintf(stderr, "processor threads: %u; CPU sets: %d, allocated to this app %d, taken or parked %d\n",
            std::thread::hardware_concurrency(), total, mine, taken);
        if (!getenv("SSR_WIDE_THREADS"))
        {
            // The emulation and the DS-size 3D thread each need a core of their own; the
            // widescreen drawing gets what is left, and 2 when the count is unknown (the 8
            // reported cores are not all the game's).
            const int wide = cores > 0 ? std::clamp(cores - 2, 1, 4) : 2;
            _putenv_s("SSR_WIDE_THREADS", std::to_string(wide).c_str());
            fprintf(stderr, "widescreen drawing threads: %d\n", wide);
        }
        // rom.nds in the package (if it was packed with one) or in that folder, else the
        // first .nds file there, whatever it is called.
        std::string rom;
        for (const std::string& p : { std::string(installed ? installed : ".") + "\\rom.nds", dir + "\\rom.nds" })
            if (rom.empty() && std::filesystem::exists(std::filesystem::u8path(p), ec)) rom = p;
        if (rom.empty())
            for (const auto& e : std::filesystem::directory_iterator(std::filesystem::u8path(dir), ec))
            {
                std::string ext = e.path().extension().u8string();
                for (char& c : ext) c = (char)tolower((unsigned char)c);
                if (ext == ".nds" && e.is_regular_file(ec)) { rom = e.path().u8string(); break; }
            }
        if (rom.empty())
        {
            // On a Developer Mode Xbox the Device Portal's file explorer can also upload into
            // DevelopmentFiles, which is easier to find. Only "FromApp" file functions reach
            // it (with the broadFileSystemAccess capability); the ROM is copied into the
            // app's folder once, so that everything else reads it as before.
            const std::wstring dev = FindDevelopmentRom(L"D:\\DevelopmentFiles", 1);
            const std::string copy = dir + "\\rom.nds";
            if (!dev.empty())
            {
                fprintf(stderr, "copying the ROM from %s\n", std::filesystem::path(dev).u8string().c_str());
                if (CopyFileFromAppW(dev.c_str(), std::filesystem::u8path(copy).wstring().c_str(), FALSE)) rom = copy;
                else fprintf(stderr, "could not copy it (error %lu)\n", GetLastError());
            }
        }
        if (rom.empty())
        {
            const std::string say = "Put your Pokemon SoulSilver (US) ROM, a file ending in .nds, in the Xbox's "
                "DevelopmentFiles folder (Device Portal -> File explorer -> DevelopmentFiles) or in this app's "
                "folder, and start it again:\n\n" + dir;
            fprintf(stderr, "%s\n", say.c_str());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "SoulSilver: no ROM yet", say.c_str(), nullptr);
            return 1;
        }
        fprintf(stderr, "ROM: %s\n", rom.c_str());
        uwpArgs = { "ssrecomp", rom, "--save", dir + "\\soulsilver.sav", "--fullscreen" };
#ifdef SSR_LAUNCHER
        // Built without the game's code (tools/xbox/launcher.py): all of it runs on the
        // interpreter.
        uwpArgs.push_back("--interp");
#endif
        for (auto& a : uwpArgs) uwpArgv.push_back(a.data());
        argc = (int)uwpArgv.size();
        argv = uwpArgv.data();
    }
#endif
    if (!ParseArgs(argc, argv, opt))
    {
        Usage();
        return 2;
    }
    if (opt.save.empty()) opt.save = opt.rom + ".sav";

    std::vector<uint8_t> rom = ReadFile(opt.rom);
    if (rom.empty())
    {
        fprintf(stderr, "cannot read ROM: %s\n", opt.rom.c_str());
        return 1;
    }

    std::vector<uint8_t> sav = ReadFile(opt.save);
    fprintf(stderr, "ROM %s: %zu bytes; save %s: %zu bytes\n", opt.rom.c_str(), rom.size(), opt.save.c_str(), sav.size());
    auto nds = MakeConsole(rom, sav, opt.headless);
    if (!nds) return 1;
    nds->ARM9.RecompEnable = opt.recomp;
    // On the interpreter with a table to watch (the Xbox launcher has no recompiled code):
    // every jump tells the scene detection what the game is doing, from the ROM's own code.
    if (!opt.recomp && !opt.lockstep && rc_watch_enable(rom.data(), rom.size()))
    {
        nds->ARM9.RecompWatch = true;
        fprintf(stderr, "watching the game's code on the interpreter\n");
    }

    // Lockstep: a second console on the plain interpreter runs each frame first and
    // records the CPU state after every instruction; the recompiled console must match.
    std::unique_ptr<NDS> ref;
    if (opt.lockstep)
    {
        ref = MakeConsole(rom, sav, true);
        if (!ref) return 1;
        nds->ARM9.RecompEnable = true;
        rc_single = true;
        ref->ARM9.TraceHook = TraceRecord;
        nds->ARM9.TraceHook = TraceCompare;
    }

    std::vector<ScriptEvent> script;
    if (!opt.script.empty()) script = LoadScript(opt.script);

    // Function tracing: a probe on every known function.
    std::vector<std::pair<int, std::string>> traceProbes;
    if (!opt.traceFuncsAt.empty())
    {
        std::ifstream f(opt.symbols);
        std::string sec, name;
        unsigned addr, size;
        while (f >> sec >> std::hex >> addr >> size >> name)
        {
            int p = rc_add_probe(sec.c_str(), addr);
            if (p >= 0) traceProbes.emplace_back(p, sec + ":" + name);
        }
        fprintf(stderr, "[trace] %zu function probes\n", traceProbes.size());
    }

    uint32_t frame = 0;
    if (!opt.stateLoad.empty())
    {
        for (NDS* n : { ref.get(), nds.get() })
        {
            if (n && !ReadSnapshot(*n, opt.stateLoad, &frame))
            {
                fprintf(stderr, "cannot load snapshot: %s\n", opt.stateLoad.c_str());
                return 1;
            }
        }
    }
    // A snapshot carries the clock of when it was made; play goes on at the current time.
    if (!opt.headless || getenv("SSR_CLOCK")) SetClock(*nds, opt.headless);

    const size_t slash = opt.save.find_last_of("/\\");
    std::string dir = slash == std::string::npos ? "." : opt.save.substr(0, slash);
    if (opt.config.empty()) opt.config = dir + "/ssrecomp.cfg";
    const std::string quickState = opt.save + ".state";

    Settings cfg;
    if (!opt.headless) cfg.Load(opt.config);
    if (opt.layoutGiven || opt.headless) cfg.layout = opt.layout;
    if (opt.fullscreen) cfg.fullscreen = true;
    if (!opt.autoFocus) cfg.focusMode = 1;
    if (opt.headless) cfg.fps60 = false; // tests compare images with the plain renderer unless asked
    if (opt.interp60 >= 0) cfg.fps60 = opt.interp60 == 1;
    nds->GPU.GPU3D.InterpolationEnabled = cfg.fps60 && !opt.lockstep;
    // The DS-resolution 3D picture is drawn on its own thread, alongside the CPU emulation.
    if (!getenv("SSR_NO_RENDER_THREAD"))
        static_cast<melonDS::SoftRenderer&>(nds->GPU.GetRenderer3D()).SetThreaded(true, nds->GPU);

    // SSR_PERF=1 prints where the time of a frame goes, averaged over 300 frames.
    const bool perfOn = getenv("SSR_PERF") != nullptr;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* tex[2] = { nullptr, nullptr };
    SDL_Texture* overlayTex = nullptr;
    int overlayTexW = 0, overlayTexH = 0;
    SDL_AudioDeviceID audio = 0;
    Pads pads;
    Layout layout = LayoutFromName(cfg.layout);
    const uint8_t* singleScreenFlag = nullptr; // set by the ROM: nonzero while a scene needs both screens
    LayoutMode preSingleLayout = LayoutMode::Merged;
    uint32_t singleFieldFrame = 0; // the last frame the overworld ran in a single-screen build
    bool singleScreenRom = false; // a SINGLE_SCREEN build of the decompilation (see the main loop)
    AutoFocus focus;
    focus.mode = cfg.focusMode;
    focus.manualFlip = opt.swapped;
    focus.Init();
    layout.swapped = focus.Swapped();

    auto applySmooth = [&]() {
        for (int i = 0; i < 2; i++)
            if (tex[i]) SDL_SetTextureScaleMode(tex[i], cfg.smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    };

    if (!opt.headless)
    {
#ifdef __ANDROID__
        SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
        SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
        // Fingers are handled one by one (on-screen controls and the touch screen).
        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
        // Tilting the phone must not move the player.
        SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
        // Controllers come through Android's own input, which knows every gamepad. With SDL's
        // HIDAPI drivers on, a pad one of them claims (a Backbone One, a PlayStation pad) is
        // left out of Android's list, and is lost if SDL cannot open it over USB itself.
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "0");
#endif
#ifdef SSR_UWP
        // On the Xbox the controller's B is also the system's "back", which would send the
        // game to the background; SDL keeps it in the game when this is set (see the
        // SDLK_AC_BACK it then reports, below).
        SDL_SetHint(SDL_HINT_WINRT_HANDLE_BACK_BUTTON, "1");
#endif
        // The controller keeps working while another window has the focus (on Windows SDL
        // otherwise ignores it then, as when the game was started from the setup window).
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0)
        {
            fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return 1;
        }
        int w, h;
        LayoutNativeSize(layout, w, h);
        window = SDL_CreateWindow("Pokemon SoulSilver", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            w * opt.scale, h * opt.scale,
            SDL_WINDOW_RESIZABLE |
#ifdef __APPLE__
            SDL_WINDOW_ALLOW_HIGHDPI |
#endif
            (cfg.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
        // The OpenGL ES renderer uploads the picture several times faster than SDL's default
        // (desktop OpenGL) on the machines this was measured on. SDL_RENDER_DRIVER overrides.
        // Not on Windows: there OpenGL ES can come with a framebuffer that converts to sRGB,
        // which washes the colours out (seen on AMD), and SDL's default is Direct3D anyway.
#if defined(__APPLE__)
        if (!getenv("SDL_RENDER_DRIVER")) SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
#elif !defined(_WIN32)
        if (!getenv("SDL_RENDER_DRIVER")) SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
#endif
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        if (!renderer)
        {
            fprintf(stderr, "SDL accelerated renderer failed: %s; trying fallback\n", SDL_GetError());
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "");
            renderer = SDL_CreateRenderer(window, -1, 0);
        }
        if (!window || !renderer)
        {
            fprintf(stderr, "cannot create window: %s\n", SDL_GetError());
            return 1;
        }
        {
            // In every log: bug reports about the picture need to know this.
            SDL_RendererInfo info;
            if (SDL_GetRendererInfo(renderer, &info) == 0)
                fprintf(stderr, "SDL renderer %s, video driver %s\n", info.name, SDL_GetCurrentVideoDriver());
        }
        for (int i = 0; i < 2; i++)
            tex[i] = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 256, 192);
        applySmooth();
        SDL_ShowCursor(cfg.fullscreen ? SDL_DISABLE : SDL_ENABLE);

        SDL_AudioSpec want;
        SDL_zero(want);
        want.freq = 48000;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 1024;
        audio = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
        if (audio) SDL_PauseAudioDevice(audio, 0);

        // For bug reports about controllers: what SDL sees, and what may hide a device from it
        // (Steam sets these variables for the games it starts; a container may lack udev).
        SDL_version sdlv;
        SDL_GetVersion(&sdlv);
        fprintf(stderr, "SDL %d.%d.%d, %d joystick device(s)\n", sdlv.major, sdlv.minor, sdlv.patch, SDL_NumJoysticks());
#ifdef __linux__
        if (access("/run/.containerenv", F_OK) == 0 || access("/.dockerenv", F_OK) == 0)
            fprintf(stderr, "running in a container\n");
#endif
        for (const char* var : {"SteamGameId", "SDL_GAMECONTROLLER_IGNORE_DEVICES", "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT",
                                "SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD", "SDL_JOYSTICK_DISABLE_UDEV", "SDL_GAMECONTROLLERCONFIG"})
            if (const char* v = getenv(var))
                fprintf(stderr, "  %s=%.200s\n", var, v);
        for (int i = 0; i < SDL_NumJoysticks(); i++)
        {
            const char* name = SDL_JoystickNameForIndex(i);
            fprintf(stderr, "  device %d: %s, %04x:%04x, %s\n", i, name ? name : "?", SDL_JoystickGetDeviceVendor(i),
                SDL_JoystickGetDeviceProduct(i), SDL_IsGameController(i) ? "known layout" : "no known layout");
            pads.Open(i);
        }
    }

    // Functions whose activity selects the merged scene.
    auto focusProbe = [&](const char* name) {
        for (unsigned i = 0; i < rc_num_focus_syms; i++)
            if (!strcmp(rc_focus_syms[i].name, name))
                return rc_add_probe(rc_focus_syms[i].section, rc_focus_syms[i].execAddr);
        return -1;
    };
    const int probeStartMenu = focusProbe("Task_StartMenu");
    const int probeStartMenuWait = focusProbe("Task_StartMenu_WaitApp");
    const int probeBattleInput = focusProbe("BattleInput_CheckTouch");
    const int probeSave[] = { focusProbe("sub_0203D264"), focusProbe("sub_0203D288"), focusProbe("sub_0203D2CC"),
        focusProbe("sub_0203D2EC"), focusProbe("sub_0203D304"), focusProbe("TouchSaveApp_HandleSaveConfirmation") };
    const int probeChoice[] = { focusProbe("ov27_0225C434") };
    const int probeMart = focusProbe("ov31_0225D7A0");
    const int probeTouch[] = { focusProbe("TouchscreenHitbox_FindRectAtTouchHeld"), focusProbe("TouchscreenHitbox_FindRectAtTouchNew"),
        focusProbe("TouchscreenHitbox_FindHitboxAtTouchNew"), focusProbe("TouchscreenHitbox_FindHitboxAtTouchHeld"),
        focusProbe("TouchscreenHitbox_TouchHeldIsIn"), focusProbe("TouchscreenHitbox_TouchNewIsIn") };
    const int probeFieldMap = focusProbe("FieldMap_Main");
    // Writing the save: for the seconds this takes the game sits in its flash write loop
    // and the code of the screen it shows (the save question, the overworld) runs only now
    // and then, so nothing on screen changes but the probes go quiet.
    const int probeSaveWrite[] = { focusProbe("HandleWriteSaveAsync_NormalData"), focusProbe("HandleWriteSaveAsync_PCBoxes") };
    auto probeActive = [&](int probe) {
        unsigned last = rc_probe_last_frame(probe);
        return last && last + 3 >= rc_current_frame();
    };
    auto saveWriting = [&]() {
        for (int probe : probeSaveWrite)
        {
            unsigned last = rc_probe_last_frame(probe);
            if (last && last + 8 >= rc_current_frame()) return true;
        }
        return false;
    };
    // Whether the overworld was running when the save write began: it counts as running
    // until the write ends, so the widescreen picture does not drop out meanwhile.
    bool fieldHeldBySave = false;
    // The overworld's main function is not entered for stretches of about ten frames while
    // some scripts run (a nurse bowing, for one), so it gets a longer memory than the rest:
    // otherwise the widescreen picture and the menu layout drop out for those frames.
    // A single-screen build of the game shows one screen, the bottom one, and the merged pipeline
    // treats it as the "top" screen, so the widescreen 3D and the battle widening apply to it.
    auto singleView = [&]() { return singleScreenRom && !opt.layoutGiven && singleScreenFlag && *singleScreenFlag == 0; };
    auto topUnitOf = [&]() {
        const int normal = (nds->PowerControl9 & (1 << 15)) ? 0 : 1;
        return singleView() ? 1 - normal : normal;
    };
    auto fieldActive = [&]() {
        unsigned last = rc_probe_last_frame(probeFieldMap);
        return (last && last + 16 >= rc_current_frame()) || fieldHeldBySave;
    };
    // Coming back from the Bag, the Pokegear and so on, the game fades the overworld in with
    // the X menu already drawn, a little before the menu's task runs again. The strip it
    // puts along the bottom of the top screen gives that away; while this counts down, the
    // menu layout is used although the task's probe is still silent.
    int menuReturn = 0;
    // Frames in a row in which the battle's touch screen showed something that is neither
    // of the menus with a layout of their own nor the idle backdrop.
    int battleUnknown = 0;
    // The frame until which the battle strip stays up while its buttons are not recognised:
    // between the commands and the moves the game slides one set out and the other in, and
    // the strip would otherwise vanish for those frames, uncovering the message box.
    uint32_t battleStripUntil = 0;
    // The button chosen in a battle strip (see the input handling before the frame runs).
    int battleSel = 0;
    // Per application, the overlay that holds its code when no other application's code is
    // there (-1 otherwise), and per section the frames on which it has run since it started
    // running (see where it is counted).
    std::vector<int> appOwnSection(rc_num_apps, -1);
    std::vector<unsigned> sectionStreak(rc_num_sections, 0);
    for (unsigned i = 0; i < rc_num_apps; i++)
    {
        if (!strcmp(rc_apps[i].section, "main") || !strcmp(rc_apps[i].section, "field")) continue;
        unsigned sharing = 0;
        for (unsigned j = 0; j < rc_num_apps; j++)
            if (!strcmp(rc_apps[j].section, rc_apps[i].section)) sharing++;
        if (sharing != 1) continue;
        for (u32 sec = 0; sec < rc_num_sections; sec++)
            if (!strcmp(rc_sections[sec].name, rc_apps[i].section)) appOwnSection[i] = (int)sec;
    }
    // Probes per game application, to know which one is running: on its per-frame function
    // and on the functions that start and end it, which run every frame while it loads and
    // fades in and while it fades out. Its layout then stays from the first frame to the
    // last instead of other layouts showing in between.
    std::vector<std::pair<const char*, int>> appProbes;
    std::vector<std::pair<const char*, int>> appMainProbes; // the per-frame functions alone
    for (unsigned i = 0; i < rc_num_apps; i++)
    {
        appProbes.emplace_back(rc_apps[i].name, rc_add_probe(rc_apps[i].section, rc_apps[i].execAddr));
        appMainProbes.push_back(appProbes.back());
        if (rc_apps[i].initAddr)
            appProbes.emplace_back(rc_apps[i].name, rc_add_probe(rc_apps[i].initSection, rc_apps[i].initAddr));
        if (rc_apps[i].exitAddr)
            appProbes.emplace_back(rc_apps[i].name, rc_add_probe(rc_apps[i].exitSection, rc_apps[i].exitAddr));
    }
    // The application's per-frame function alone, without its start and end: what the test
    // options that wait for a battle (--battle-mash, --nav-stop-on-battle) go by.
    auto appRunning = [&](const char* name) {
        for (auto& ap : appMainProbes)
            if (!strcmp(ap.first, name))
            {
                unsigned last = rc_probe_last_frame(ap.second);
                if (last && last + 6 >= rc_current_frame()) return true;
            }
        return false;
    };
    auto appActive = [&](const char* name) {
        for (auto& ap : appProbes)
            if (!strcmp(ap.first, name))
            {
                unsigned last = rc_probe_last_frame(ap.second);
                if (last && last + 6 >= rc_current_frame()) return true;
            }
        // Opened from the start menu, an application is prepared by the field with code of
        // its own overlay for a while before its start function runs: that counts too, when
        // the overlay is that application's alone and has kept running for a while
        // (the start menu calls into some overlays once in passing).
        const unsigned waitLast = rc_probe_last_frame(probeStartMenuWait);
        if (waitLast && waitLast + 3 >= rc_current_frame())
            for (unsigned i = 0; i < rc_num_apps; i++)
                if (!strcmp(rc_apps[i].name, name) && appOwnSection[i] >= 0 && sectionStreak[appOwnSection[i]] >= 8)
                    return true;
        return false;
    };

    auto sectionActive = [&](const char* name) {
        for (u32 i = 0; i < rc_num_sections; i++)
            if (!strcmp(rc_sections[i].name, name))
                return rc_section_last_frame[i] && rc_section_last_frame[i] + 3 >= rc_current_frame();
        return false;
    };

    // Whether an application driven from the bottom screen is starting, running or ending
    // (worked out each frame before the scene is chosen).
    bool appBottomNow = false;
    // The command strip's own cursor: d-pad presses seen by the frontend this frame, and the
    // selection they have moved it to (reset whenever the ROM's published selection changes).
    std::string stripNav; int stripSel = 0, stripPubSel = -1, stripPubMenu = 0;
    // Chooses what the single merged view shows this frame.
    auto pickScene = [&](bool cursorInUse, float viewWidth, const ScreenImage* screens) {
        Scene sc;
        auto only = [&](const char* name, int screen) {
            sc.name = name;
            sc.items = { FullScreen(screen) };
        };
        if (singleView())
        {
            // The game has put everything on the visible screen: the world is widened on a wide
            // view, a battle's edges are continued, and any other screen's bars take its colours.
            only("single", 0);
            if (sectionActive("OVY_12"))
            {
                sc.name = "battle";
                // The bag and party screens are 4:3 pages: no scene to continue past their edges.
                sc.edgeFill = viewWidth >= 300 && !singleScreenFlag[3];
                // The ROM says which command panel is up (1 main, 2 fight only, 4 moves) and what
                // is chosen. On a wide view they stand as a strip along the bottom, as in the
                // recomp of the original game, with the scene above left alone.
                const int menu = singleScreenFlag[1], sel = singleScreenFlag[2];
                if (menu != stripPubMenu || sel != stripPubSel) { stripPubMenu = menu; stripPubSel = sel; stripSel = sel; }
                // Same moves as the ROM's main menu: left/right along the row, up/down between rows of 2.
                if (menu == 1 || menu == 2)
                    for (char k : stripNav)
                        stripSel = k == 'L' ? std::max(0, stripSel - 1) : k == 'R' ? std::min(3, stripSel + 1)
                                 : k == 'U' ? (stripSel >= 2 ? stripSel - 2 : stripSel)
                                            : (stripSel < 2 ? stripSel + 2 : stripSel);
                stripNav.clear();
                const int shown = (menu == 1 || menu == 2) ? stripSel : sel;
                if (viewWidth >= 300 && (menu == 1 || menu == 2 || menu == 4))
                {
                    const float L = 128 - viewWidth / 2;
                    sc.name = menu == 4 ? "battle-moves" : "battle-cmd";
                    sc.items.push_back(FillRect(L, 137, (int)viewWidth + 1, 55, 0x20242C));
                    sc.items.push_back(FillRect(L, 137, (int)viewWidth + 1, 1, 0x5A6270));
                    Element label = { 0, 16, 153, 62, 16, L + 4.5f, 139.5f, 1.0f, 0, 255, false };
                    Element label2 = { 0, 16, 169, 80, 16, L + 64.5f, 139.5f, 1.0f, 0, 255, false };
                    if (menu != 4) {
                        sc.items.push_back(FillRect(L + 4, 139, 141, 16, 0xFFFFFF));
                        sc.items.push_back(label);
                        sc.items.push_back(label2);
                    }
                    const size_t firstButton = sc.items.size();
                    const float gap = 6;
                    const bool moves = menu == 4;
                    const int bw = moves ? 78 : 50, bh = moves ? 22 : 18;
                    const float sb = std::min(33.0f / bh, (viewWidth - 8 - 3 * gap) / (4 * bw));
                    float x = 128 - (4 * bw * sb + 3 * gap) / 2;
                    const float y = 156.5f + (33 - bh * sb) / 2;
                    for (int i = 0; i < 4; i++)
                    {
                        const int sx = moves ? 5 + (i & 1) * 80 : 133 + (i & 1) * 52;
                        const int sy = moves ? 141 + (i >> 1) * 24 : 149 + (i >> 1) * 20;
                        Element b = { 0, sx, sy, bw, bh, x, y, sb, 0, 255, false };
                        b.border = true;
                        b.borderColor = i == shown ? 0xF04040 : 0x101218;
                        b.borderWidth = i == shown ? 16 : 6;
                        sc.items.push_back(b);
                        x += bw * sb + gap;
                    }
                    (void)firstButton;
                }
            }
            else
            {
                if (sectionActive("field")) singleFieldFrame = rc_current_frame();
                sc.wide = viewWidth >= 300 && (fieldActive() || (singleFieldFrame && rc_current_frame() - singleFieldFrame < 20));
            }
            return sc;
        }
        auto startMenu = [&]() {
            only("start-menu", 0);
            // On a wide view the panel stands at the right edge, mostly beside the overworld.
            const float menuScale = viewWidth >= 300 ? 0.8f : 0.75f;
            const float menuX = viewWidth >= 300 ? 128 + viewWidth / 2 - 161 * menuScale - 2 : 131;
            sc.items.push_back({ 1, 4, 0, 161, 192, menuX, (192 - 192 * menuScale) / 2, menuScale, 0, 255, false });
            sc.items.back().border = !AutoFocus::Blank(screens[1].pixels);
        };

        // The safety net below must not flash up for the few frames in which the field map is
        // not running during a door transition, so its condition has to hold for a while.
        static int spreadFrames = 0;
        if (std::any_of(std::begin(probeTouch), std::end(probeTouch), probeActive) && !fieldActive() &&
            !appActive("Battle") && !appActive("TitleScreen") && !appActive("IntroMovie") && !appActive("Credits"))
            spreadFrames++;
        else
            spreadFrames = 0;
        if (focus.manualFlip)
            only("manual", focus.wantBottom ? 0 : 1);
        else if (appActive("PokemonSummary"))
        {
            // The page's card is one tall card split across the screens; its two halves stand
            // side by side after the column with the page's name and the Pokemon's name,
            // picture and item, on the page's colour. The page tabs and the party grid are
            // left out; the D-pad does both. The game frames the card on its right only (it
            // runs off the screen on the left), so the halves are cut from inside that frame
            // and each gets an even frame of its own, with what is drawn on it centred (the
            // moves list, for one, stands left of the middle).
            const uint32_t page = screens[0].pixels[100 * 256 + 1] & 0xFFFFFF;
            const uint32_t frame = 0x414141;
            Element side = Crop(0, 152, 8, 104, 184), upper = Crop(0, 0, 22, 147, 170), lower = Crop(1, 0, 0, 147, 160);
            // The lower half keeps the EXP bar, which overlaps the frame's bottom edge: the
            // edge's shading and its rounded corner are left out around it.
            lower.keys[0] = screens[1].pixels[158 * 256 + 1] & 0xFFFFFF;
            lower.keyRing = 2;
            lower.cutX = 143;
            lower.cutY = 154;
            const float pad = 4, gap = 6, margin = 6, h = 189;
            const int cardW = upper.sw;
            const float w = side.sw + 2 * (cardW + 2 * pad) + 2 * gap;
            const float scale = std::min((192 - 2 * margin) / h, (viewWidth - 2 * margin) / w);
            float x = 128 - w * scale / 2;
            const float y = 96 - h * scale / 2;
            sc.name = "summary";
            {
                // The name and item boxes run off the screen at the right, and the item box
                // off its bottom too: they are closed with mirrored copies of their left and
                // top edges, and the column's shaded strip at the right is painted over
                // beside them, as is the header's shaded end.
                const uint32_t* px = screens[0].pixels;
                const uint32_t column = px[100 * 256 + 154] & 0xFFFFFF;
                Element back = FillRect(x, y, side.sw, (int)h, column);
                back.scale = scale;
                back.border = true;
                back.borderColor = frame;
                back.borderRadius = 3;
                sc.items.push_back(back);
                auto at = [&](Element e, int sx, int sy) {
                    e.scale = scale;
                    e.dx = x + (sx - 152) * scale;
                    e.dy = y + (sy - 8) * scale;
                    sc.items.push_back(e);
                };
                at(side, 152, 8);
                at(FillRect(0, 0, 10, 16, px[15 * 256 + 153] & 0xFFFFFF), 246, 8);
                auto inBox = [&](int sy) {
                    for (int sx = 156; sx <= 158; sx++)
                        if ((px[sy * 256 + sx] & 0xFFFFFF) == frame) return true;
                    return false;
                };
                for (int sy = 26; sy < 192;)
                {
                    int end = sy;
                    while (end < 192 && inBox(end) == inBox(sy)) end++;
                    if (!inBox(sy)) at(FillRect(0, 0, 4, end - sy, column), 246, sy);
                    sy = end;
                }
                Element right = Crop(0, 152, 26, 6, 166);
                right.flipX = true;
                at(right, 250, 26);
                Element bottom = Crop(0, 152, 156, 98, 2);
                bottom.flipY = true;
                at(bottom, 152, 192);
                Element corner = Crop(0, 152, 156, 6, 2);
                corner.flipX = corner.flipY = true;
                at(corner, 250, 192);
            }
            x += (side.sw + gap) * scale;
            for (Element* e : { &upper, &lower })
            {
                Element back = FillRect(x, y, cardW + 2 * (int)pad, (int)h, page);
                back.scale = scale;
                back.border = true;
                back.borderColor = frame;
                back.borderRadius = 3;
                sc.items.push_back(back);
                // What is drawn on the card comes in blocks, one above the other with the page's
                // colour between them; each block is centred across the panel, and all of them
                // together top to bottom.
                const uint32_t* px = screens[e->screen].pixels;
                const uint32_t edge = e->keys[0];
                auto drawn = [&](int sx, int sy) {
                    const uint32_t c = px[sy * 256 + sx] & 0xFFFFFF;
                    return c != page && !(sy >= e->sy + e->sh - e->keyRing && c == edge) &&
                        (sx < e->cutX || sy < e->cutY);
                };
                struct Block { int y0, y1, x0, x1; };
                std::vector<Block> blocks;
                int blank = 2;
                for (int sy = e->sy; sy < e->sy + e->sh; sy++)
                {
                    int r0 = cardW, r1 = -1;
                    for (int sx = 0; sx < cardW; sx++)
                        if (drawn(sx, sy))
                        {
                            r0 = std::min(r0, sx);
                            r1 = sx;
                        }
                    if (r1 < 0)
                    {
                        blank++;
                        continue;
                    }
                    if (blank >= 2 || blocks.empty())
                        blocks.push_back({ sy, sy, r0, r1 });
                    Block& bl = blocks.back();
                    bl.y1 = sy;
                    bl.x0 = std::min(bl.x0, r0);
                    bl.x1 = std::max(bl.x1, r1);
                    blank = 0;
                }
                if (!blocks.empty())
                {
                    const float top = y + (pad + (h - 2 * pad - (blocks.back().y1 + 1 - blocks[0].y0)) / 2.0f) * scale;
                    for (const Block& bl : blocks)
                    {
                        Element piece = *e;
                        piece.sx = bl.x0;
                        piece.sw = bl.x1 - bl.x0 + 1;
                        piece.sy = bl.y0;
                        piece.sh = bl.y1 - bl.y0 + 1;
                        piece.scale = scale;
                        piece.dx = x + (pad + (cardW - piece.sw) / 2.0f) * scale;
                        piece.dy = top + (bl.y0 - blocks[0].y0) * scale;
                        sc.items.push_back(piece);
                        // A box the game runs off the screen or into the card's frame (the
                        // ability, the number of ribbons) gets sides of its own.
                        if (bl.x0 == 0 || bl.x1 == cardW - 1)
                            for (float bx : { piece.dx - 2 * scale, piece.dx + piece.sw * scale })
                            {
                                Element side = FillRect(bx, piece.dy, 2, piece.sh, frame);
                                side.scale = scale;
                                sc.items.push_back(side);
                            }
                    }
                }
                x += (cardW + 2 * pad + gap) * scale;
            }
            sc.bgScreen = 0;
            sc.bgX = 1;
            sc.bgY = 100;
        }
        else if (appActive("Bag"))
            // Just the bag screen; the description box on the other screen is left out.
            sc = Stack("bag", { Crop(1, 0, 0, 256, 192) }, false, viewWidth);
        else if (appActive("PCBox"))
            // The selected Pokemon's info card from the other screen beside the box.
        {
            sc = SideBySide("pc-box", Crop(0, 0, 34, 216, 156), Crop(1, 0, 0, 256, 192), viewWidth);
            sc.bgScreen = 0;
            sc.bgX = 236;
            sc.bgY = 100;
        }
        else if (appActive("Pokedex"))
        {
            // Every Pokedex page fills both screens, so they are joined into one page: as one
            // tall picture for the closed cover (red at both left edges), otherwise as a spread
            // with the entry on the left and the list or controls on the right.
            auto red = [&](int screen) {
                uint32_t c = screens[screen].pixels[100 * 256 + 10];
                return ((c >> 16) & 0xFF) > 180 && ((c >> 8) & 0xFF) < 100 && (c & 0xFF) < 100;
            };
            bool cover = red(0) && red(1);
            uint32_t title = screens[0].pixels[8 * 256 + 200];
            auto light = [](uint32_t c) { return ((c >> 16) & 0xFF) > 235 && ((c >> 8) & 0xFF) > 235 && (c & 0xFF) > 235; };
            // The list page has a red title bar and, once the cover has slid away, bare paper
            // between the picture and the description.
            int bare = 0;
            for (int y = 127; y < 131; y++)
                for (int x = 4; x < 96; x++) bare += light(screens[0].pixels[y * 256 + x]);
            bool listPage = ((title >> 16) & 0xFF) > 200 && ((title >> 8) & 0xFF) < 90 && (title & 0xFF) < 90 && bare > 280;
            // The detail pages have a coloured title bar on the top screen and a blue tab bar
            // at the foot of the bottom screen.
            uint32_t tab = screens[1].pixels[176 * 256 + 63];
            bool tabPage = !light(title) && (tab & 0xFF) > 150 && ((tab >> 16) & 0xFF) < 120 && !listPage &&
                (((title >> 16) & 0xFF) < 200 || ((title >> 8) & 0xFF) > 90);
            auto put = [&](Element e, float x, float y, float scale) {
                e.dx = x;
                e.dy = y;
                e.scale = scale;
                sc.items.push_back(e);
            };
            const float W = viewWidth, L = 128 - W / 2;
            if (cover || W < 300)
                sc = Stack("pokedex", { Crop(0, 0, 0, 256, 192), Crop(1, 0, 0, 256, 192) }, !cover, viewWidth);
            else if (listPage)
            {
                // The list as one page: the pieces of the entry on the top screen are laid out
                // again around the number grid. A title bar runs across the top; on the left
                // are the name, the picture and the type and size card; on the right the
                // description sits above the grid and its buttons.
                sc.name = "pokedex-list";
                sc.items.clear();
                // The page is drawn on plain paper: the two most common colours in an empty
                // strip of the top screen are the paper and its ruling, and the ruling is
                // left out of the rearranged pieces so that they do not show as patches.
                uint32_t paper = 0;
                std::vector<std::pair<int, uint32_t>> ruling;
                {
                    // Sampled from the empty area between the picture and the description.
                    std::map<uint32_t, int> count;
                    for (int y = 113; y < 131; y++)
                        for (int x = 4; x < 96; x++) count[screens[0].pixels[y * 256 + x] & 0xFFFFFF]++;
                    int best = 0;
                    for (auto& c : count)
                        if (c.second > best)
                        {
                            best = c.second;
                            paper = c.first;
                        }
                    uint32_t line = paper;
                    int lineCount = 0;
                    for (auto& c : count)
                        if (c.first != paper && c.second > lineCount)
                        {
                            lineCount = c.second;
                            line = c.first;
                        }
                    // The ruling also has lighter greys where lines cross; those are between
                    // the line colour and the paper, and losing one inside a picture would
                    // hardly show.
                    count.clear();
                    for (int y = 24; y < 126; y++)
                        for (int x = 4; x < 100; x++) count[screens[0].pixels[y * 256 + x] & 0xFFFFFF]++;
                    for (auto& c : count)
                    {
                        uint32_t r = (c.first >> 16) & 0xFF, g = (c.first >> 8) & 0xFF, bl = c.first & 0xFF;
                        if (r == g && g == bl && r > (line & 0xFF) && r < (paper & 0xFF)) ruling.push_back({ -c.second, c.first });
                    }
                    std::sort(ruling.begin(), ruling.end());
                    if (ruling.size() > 3) ruling.resize(3);
                    if (line != paper) ruling.push_back({ 0, line });
                    sc.bgScreen = 0;
                    for (int i = 127 * 256 + 4; i < 131 * 256; i++)
                        if ((screens[0].pixels[i] & 0xFFFFFF) == paper)
                        {
                            sc.bgX = i % 256;
                            sc.bgY = i / 256;
                            break;
                        }
                }
                auto putKeyed = [&](Element e, float x, float y, float scale) {
                    for (size_t k = 0; k < 4 && k < ruling.size(); k++) e.keys[k] = ruling[k].second;
                    put(e, x, y, scale);
                };
                const float t = 0.77f, barH = 17 * t;
                for (float x = L; x < L + W; x += 100 * t) put(Crop(0, 140, 0, 100, 17), x, 0, t);
                put(Crop(0, 0, 0, 80, 17), L, 0, t);
                const float leftW = W * 0.43f, rightW = W - leftW, rx = L + leftW;
                const float nameW = 156 * t;
                putKeyed(Crop(0, 100, 21, 156, 39), L + (leftW - nameW) / 2, barH + 3, t);
                const float statsY = 192 - 64 * t - 2;
                putKeyed(Crop(0, 100, 62, 156, 64), L + (leftW - nameW) / 2, statsY, t);
                const float picTop = barH + 3 + 39 * t, picS = std::min((statsY - picTop) / 102, leftW / 96);
                putKeyed(Crop(0, 4, 24, 96, 102), L + (leftW - 96 * picS) / 2, picTop + (statsY - picTop - 102 * picS) / 2, picS);
                const float descS = std::min(t, (rightW - 4) / 248), descH = 56 * descS;
                putKeyed(Crop(0, 4, 132, 248, 56), rx + (rightW - 248 * descS) / 2, barH + 2, descS);
                const float gridTop = barH + 2 + descH + 1, gridS = std::min((192 - gridTop) / 192, rightW / 256);
                put(Crop(1, 0, 0, 256, 192), rx + (rightW - 256 * gridS) / 2, 192 - 192 * gridS, gridS);
            }
            else if (!tabPage)
            {
                // Search and anything else: both screens as the two columns of a page.
                sc = Stack("pokedex", { Crop(0, 0, 0, 256, 192), Crop(1, 0, 0, 256, 192) }, true, viewWidth);
                if (light(screens[0].pixels[2 * 256 + 250]))
                {
                    sc.bgScreen = 0;
                    sc.bgX = 250;
                    sc.bgY = 2;
                }
            }
            else
            {
                // The other pages (area, size, forms): the title bar of the top screen and the
                // tab bar of the bottom screen frame the whole view, with the two bodies as
                // the columns of one page between them.
                sc.name = "pokedex-page";
                sc.items.clear();
                for (int i = 24 * 256; i < 192 * 256; i++)
                {
                    uint32_t c = screens[0].pixels[i];
                    if (((c >> 16) & 0xFF) > 235 && ((c >> 8) & 0xFF) > 235 && (c & 0xFF) > 235)
                    {
                        sc.bgScreen = 0;
                        sc.bgX = i % 256;
                        sc.bgY = i / 256;
                        break;
                    }
                }
                const float headH = 18, footH = 28, room = 192 - headH - footH;
                const float b = std::min(W / 512, room / 170);
                put(Crop(0, 0, 21, 256, 169), L + (W / 2 - 256 * b) / 2, headH + (room - 169 * b) / 2, b);
                put(Crop(1, 0, 0, 256, 162), 128 + (W / 2 - 256 * b) / 2, headH + (room - 162 * b) / 2, b);
                for (float x = L + 200; x < L + W; x += 40) put(Crop(0, 100, 0, 40, 18), x, 0, 1);
                put(Crop(0, 0, 0, 220, 18), L, 0, 1);
                for (float x = L; x < L + W; x += 6) put(Crop(1, 61, 162, 6, 28), x, 192 - footH, 1);
                put(Crop(1, 0, 162, 256, 28), 0, 192 - footH, 1);
            }
        }
        else if (appActive("GearMap") || appActive("TownMap"))
        {
            // The location's name and description from the other screen beside the map.
            sc = SideBySide("map", Crop(0, 0, 64, 256, 128), Crop(1, 0, 0, 256, 192), viewWidth);
            sc.bgScreen = 0;
            sc.bgX = 128;
            sc.bgY = 58;
        }
        else if (appActive("GearPhone") && screens[1].flags &&
            LayerClass(screens[1].flags[170 * 256 + 30]) == LAYER_OBJ)
            // A call in progress (the contact list is inactive and a sprite panel covers its
            // tab bar): the caller's words are in a box on the top screen.
            only("call", 0);
        else if (appActive("GearPhone"))
            // The clock and call text from the other screen above the contact list.
            sc = Stack("phone", { Crop(0, 0, 16, 256, 58), Crop(1, 0, 0, 256, 192) }, false, viewWidth);
        else if (appActive("ChooseStarter"))
        {
            // The Pokemon preview and the professor's text, with the Poke Balls from the
            // other screen inset in a corner.
            only("starter", 0);
            Element balls = Crop(1, 0, 0, 256, 150);
            if (viewWidth >= 300)
            {
                // Wide view: the preview moves to the left edge and the tray of Poke Balls
                // stands in the space that frees up on the right.
                const float L = 128 - viewWidth / 2, R = 128 + viewWidth / 2;
                sc.items[0].dx = L;
                balls.scale = std::min(0.62f, (viewWidth - 256 + 36) / 256);
                balls.dx = R - 256 * balls.scale;
                balls.dy = 72 - 75 * balls.scale;
            }
            else
            {
                balls.dx = 3;
                balls.dy = 3;
                balls.scale = 0.42f;
            }
            balls.border = viewWidth >= 300 && !AutoFocus::Blank(screens[1].pixels);
            sc.items.push_back(balls);
        }
        else if (appActive("NamingScreen"))
            only("naming", 1);
        else if (appActive("OakSpeech") && AutoFocus::Blank(screens[0].pixels) && !AutoFocus::Blank(screens[1].pixels))
            // Pages of the intro that use the bottom screen alone.
            only("bottom", 1);
        else if (appActive("OakSpeech"))
        {
            // The professor on the top screen; choices from the bottom screen (its backdrop art
            // dropped) are placed where they hide the least text.
            only("intro", 0);
            Element el;
            const uint8_t drop = LAYER_BG3 | LAYER_BACKDROP;
            if (viewWidth >= 300)
            {
                // Wide view: the professor's screen moves to the left edge and its backdrop,
                // which is the same all the way across, is continued to the right edge. The
                // choices from the bottom screen (buttons, the boy and the girl) stand in
                // that extra space instead of on top of the professor.
                const float L = 128 - viewWidth / 2, R = 128 + viewWidth / 2, free = viewWidth - 256;
                // Everything on the bottom screen that is not its plain background colour is a
                // choice: buttons, or the cards of the boy and the girl. The cards and the red
                // frame around the selected one are part of the background layer, so the
                // pieces are found and cut out by colour rather than by layer.
                const uint32_t plain = screens[1].pixels[96 * 256 + 2] & 0xFFFFFF;
                // A faint Poke Ball is sometimes drawn into that background in nearly the
                // same blue; it counts as background too.
                auto nearPlain = [&](uint32_t c) {
                    return std::abs((int)((c >> 16) & 0xFF) - (int)((plain >> 16) & 0xFF)) <= 56 &&
                        std::abs((int)((c >> 8) & 0xFF) - (int)((plain >> 8) & 0xFF)) <= 56 &&
                        std::abs((int)(c & 0xFF) - (int)(plain & 0xFF)) <= 56;
                };
                int bx0 = 256, by0 = 192, bx1 = -1, by1 = -1;
                for (int y = 8; y < 184; y++)
                    for (int x = 0; x < 256; x++)
                        if (!nearPlain(screens[1].pixels[y * 256 + x] & 0xFFFFFF))
                        {
                            bx0 = std::min(bx0, x);
                            bx1 = std::max(bx1, x);
                            by0 = std::min(by0, y);
                            by1 = std::max(by1, y);
                        }
                // Alone in the lower right corner there is only a "touch to continue" hint.
                const bool found = bx1 >= bx0 && bx1 - bx0 < 250 && !(bx0 >= 144 && by0 >= 128);
                // The box around the choices only ever grows while they are up: a button that
                // flashes when pressed must not make the whole group jump about.
                static int cx0, cy0, cx1, cy1, cedge;
                static unsigned choiceSeen = 0;
                const unsigned now = rc_current_frame();
                const bool fresh = !choiceSeen || now - choiceSeen > 40;
                if (found)
                {
                    int edge = 0;
                    if (screens[0].flags)
                        for (int y = 8; y < 140; y++)
                            for (int x = 255; x > edge; x--)
                                if (!(LayerClass(screens[0].flags[y * 256 + x]) & drop))
                                {
                                    edge = x;
                                    break;
                                }
                    cx0 = fresh ? bx0 : std::min(cx0, bx0);
                    cy0 = fresh ? by0 : std::min(cy0, by0);
                    cx1 = fresh ? bx1 : std::max(cx1, bx1);
                    cy1 = fresh ? by1 : std::max(cy1, by1);
                    cedge = fresh ? edge : std::max(cedge, edge);
                    choiceSeen = now;
                }
                const bool showing = choiceSeen && now - choiceSeen <= 40;
                // While he only talks, his screen is in the middle. For a choice it slides to
                // the left edge and the choices stand in the space that frees up beside him,
                // instead of on top of him. His backdrop is the same all the way across, so it
                // is continued to both edges of the view.
                static float slide = 0;
                static unsigned slideFrame = 0;
                if (now != slideFrame)
                {
                    slide += std::clamp((showing ? 1.0f : 0.0f) - slide, -1.0f / 14, 1.0f / 14);
                    slideFrame = now;
                }
                const float ease = slide * slide * (3 - 2 * slide);
                const float picX = L * ease;
                sc.items[0].dx = picX;
                for (float x = picX - 2; x > L - 2; x -= 2)
                {
                    Element strip = Crop(0, 0, 0, 2, 192);
                    strip.dx = x;
                    strip.crisp = true;
                    sc.items.push_back(strip);
                }
                for (float x = picX + 256; x < R; x += 2)
                {
                    Element strip = Crop(0, 0, 0, 2, 192);
                    strip.dx = x;
                    strip.crisp = true;
                    sc.items.push_back(strip);
                }
                if (showing)
                {
                    int x0 = std::max(0, cx0 - 2), y0 = std::max(0, cy0 - 2);
                    el = Crop(1, x0, y0, std::min(255, cx1 + 2) - x0 + 1, std::min(191, cy1 + 2) - y0 + 1);
                    el.keys[0] = plain;
                    el.keyTolerance = 56;
                    // They may reach over the right of the professor's screen as far as
                    // there is nothing but backdrop there (above the message box).
                    float room = R - 3 - (L + cedge + 6);
                    if (room < free) room = free;
                    el.scale = std::min({ 1.25f, room / el.sw, 136.0f / el.sh });
                    float w = el.sw * el.scale, h = el.sh * el.scale;
                    el.dx = R - 3 - room + (room - w) / 2;
                    el.dy = 4 + (136 - h) / 2;
                    el.alpha = (uint8_t)(255 * ease);
                    sc.items.push_back(el);
                }
            }
            else if (SmartOverlay(screens, 1, 160, 144, drop, 180, 120, LAYER_BG0, 255, el) &&
                SmartOverlay(screens, 1, 200, 152, drop, 180, 120, LAYER_BG0, 255, el))
                // The lower right corner only ever holds a "touch to continue" hint.
                sc.items.push_back(el);
        }
        else if (std::any_of(std::begin(probeSave), std::end(probeSave), probeActive) &&
                 // Closing, the X menu's save step runs on for a few frames after the save
                 // screen is gone and the menu is back.
                 (sectionActive("touch_save_app") || !probeActive(probeStartMenu)))
            only("save", 1);
        else if (probeActive(probeMart))
            // Poke Mart: the item's description from the other screen above the list of goods.
        {
            sc = Stack("mart", { Crop(0, 0, 140, 256, 52), Crop(1, 0, 0, 256, 192) }, false, viewWidth);
            sc.bgScreen = 1;
            sc.bgX = 6;
            sc.bgY = 100;
        }
        else if (std::any_of(std::begin(probeChoice), std::end(probeChoice), probeActive))
        {
            // A question asked in the overworld: the answer buttons are on the bottom screen.
            // They are drawn, without that screen's background, at the right above the message box.
            only("choice", 0);
            Element el;
            if (SmartOverlay(screens, 1, 256, 192, LAYER_BG2 | LAYER_BG3 | LAYER_BACKDROP, 168, 136, 0, 255, el))
            {
                // Keep the piece the same while the question is up, growing it if needed, so
                // that a button flashing does not move or resize the group.
                static Element held;
                static unsigned heldAt = 0;
                const unsigned now = rc_current_frame();
                if (heldAt && now - heldAt <= 8)
                {
                    int x0 = std::min(held.sx, el.sx), y0 = std::min(held.sy, el.sy);
                    int x1 = std::max(held.sx + held.sw, el.sx + el.sw), y1 = std::max(held.sy + held.sh, el.sy + el.sh);
                    el.sx = x0;
                    el.sy = y0;
                    el.sw = x1 - x0;
                    el.sh = y1 - y0;
                    el.scale = std::min({ 1.0f, 168.0f / el.sw, 136.0f / el.sh });
                }
                held = el;
                heldAt = now;
                el.dx = (viewWidth >= 300 ? 128 + viewWidth / 2 - 3 : 252) - el.sw * el.scale;
                el.dy = 142 - el.sh * el.scale;
                sc.items.push_back(el);
            }
        }
        else if (menuReturn > 0)
            startMenu();
        else if (probeActive(probeStartMenuWait) && !appBottomNow)
        {
            // An application chosen in the start menu is loading (the field prepares it for a
            // while before it starts): the menu while it fades, then the bottom screen,
            // where these applications appear, instead of the top screen and both.
            if (!AutoFocus::Blank(screens[1].pixels))
                startMenu();
            else
                only("bottom", 1);
        }
        else if (focus.wantBottom || appBottomNow)
            only("bottom", 1);
        else if (sectionActive("OVY_8"))
            // In-battle bag and party screens (overlay 8) fill the bottom screen.
            only("battle-submenu", 1);
        else if (probeActive(probeBattleInput) ||
            // While the commands slide out and the moves in, the game stops reading the touch
            // screen for a few frames; the strip stays up meanwhile (see battleStripUntil).
            (frame < battleStripUntil && sectionActive("OVY_12") && !AutoFocus::Blank(screens[1].pixels)))
        {
            // Battle, choosing a command or a move. On a wide view the buttons are taken off
            // the touch screen one by one and laid out as a strip along the bottom, in place
            // of the message box (which only says "What will X do?" at that point): nothing
            // of the scene is covered. Which menu is up is read from the colours of its
            // buttons; while they slide in or out neither is recognised and no buttons are
            // shown. Anything else the battle asks on the touch screen (a target, yes or no)
            // has no strip of its own and is shown whole, as a card at the lower left.
            only("battle", 0);
            sc.edgeFill = true;
            const uint32_t* b = screens[1].pixels;
            auto rgb = [&](int x, int y, int& r, int& g, int& bl) {
                uint32_t c = b[y * 256 + x];
                r = (c >> 16) & 0xFF; g = (c >> 8) & 0xFF; bl = c & 0xFF;
            };
            auto blue = [&](int x, int y) { int r, g, bl; rgb(x, y, r, g, bl); return bl > 150 && r < 110 && g > 90 && g < 200 && bl > r + 60; };
            auto red = [&](int x, int y) { int r, g, bl; rgb(x, y, r, g, bl); return r > 180 && g < 120 && bl < 120; };
            auto orange = [&](int x, int y) { int r, g, bl; rgb(x, y, r, g, bl); return r > 180 && g > 110 && g < 200 && bl < 110; };
            const bool cmd = red(128, 60) && orange(40, 160) && blue(128, 166);
            const bool moves = !cmd && blue(40, 166) && blue(128, 166) && blue(216, 166);
            const bool idle = AutoFocus::Blank(screens[1].pixels) || (!blue(128, 166) && !red(128, 60));
            battleUnknown = (cmd || moves || idle) ? 0 : battleUnknown + 1;
            // Only after the commands (FIGHT): after a move is chosen the battle goes on and
            // its messages must not be covered.
            if (cmd) battleStripUntil = frame + 20;
            else if (moves || idle) battleStripUntil = 0;
            const float L = 128 - viewWidth / 2;
            auto put = [&](int sx, int sy, int sw, int sh, float x, float y, float scale) {
                sc.items.push_back({ 1, sx, sy, sw, sh, x, y, scale, 0, 255, false });
            };
            if (viewWidth >= 300 && (cmd || moves || frame < battleStripUntil))
            {
                // (Neither: the strip without buttons, while the next ones slide in.)
                sc.name = cmd ? "battle-cmd" : moves ? "battle-moves" : "battle-strip";
                // The strip: the first line of the message box ("What will X do?") as a
                // label at the top, the buttons under it.
                sc.items.push_back(FillRect(L, 137, (int)viewWidth + 1, 55, 0x20242C));
                sc.items.push_back(FillRect(L, 137, (int)viewWidth + 1, 1, 0x5A6270));
                {
                    // (Only the text part: the right end holds the battle's own little indicator boxes.)
                    Element label = { 0, 10, 149, 190, 18, L + 4.5f, 139.5f, 0.78f, 0, 255, false };
                    label.border = true;
                    label.borderColor = 0x101218;
                    label.borderWidth = 6;
                    label.borderRadius = 1.5f;
                    sc.items.push_back(label);
                }
                const size_t firstButton = sc.items.size();
                if (cmd)
                {
                    // FIGHT, BAG, POKeMON, RUN: the coloured inside of each button with its
                    // label, all cut to the same height and drawn at one scale, each with an
                    // outline of its own. (The buttons' own frames differ in size, and RUN's
                    // runs off the bottom of the touch screen, so they are not used.)
                    struct Cut { int sx, sy, sw; };
                    static const Cut cuts[4] = { { 68, 67, 120 }, { 6, 152, 66 }, { 182, 151, 68 }, { 94, 158, 68 } };
                    const int ch = 34, total = 120 + 66 + 68 + 68;
                    const float gap = 5;
                    const float sb = std::min(33.0f / ch, (viewWidth - 8 - 3 * gap) / total);
                    float x = 128 - (total * sb + 3 * gap) / 2;
                    for (const Cut& c : cuts)
                    {
                        put(c.sx, c.sy, c.sw, ch, x, 156.5f + (33 - ch * sb) / 2, sb);
                        x += c.sw * sb + gap;
                    }
                }
                else if (moves)
                {
                    // The four moves in a row, each its whole button with its frame; the touch
                    // screen's background around the rounded corners is left out so the button
                    // stands on the strip. There is no
                    // room for the CANCEL bar at a readable size; B cancels.
                    const int mw = 126, mh = 54;
                    const float gap = 5;
                    const float sm = std::min(33.0f / mh, (viewWidth - 8 - 3 * gap) / (4 * mw));
                    float x = 128 - (4 * mw * sm + 3 * gap) / 2;
                    const float y = 156.5f + (33 - mh * sm) / 2;
                    static const int slotX[4] = { 1, 129, 1, 129 }, slotY[4] = { 25, 25, 89, 89 };
                    for (int i = 0; i < 4; i++)
                    {
                        put(slotX[i], slotY[i], mw, mh, x, y, sm);
                        // The background is its own layer (BG2), so it is simply left out. The
                        // frame's rounded corners are shaded with the button's tan against that
                        // background; on the dark strip those pixels read as stray dots, so the
                        // tan is also left out along the rim (it is the main colour inside).
                        Element& el = sc.items.back();
                        el.drop = LAYER_BG2;
                        el.keys[0] = 0xCBBAA2;
                        el.keyRing = 5;
                        x += mw * sm + gap;
                    }
                }
                // The chosen button is outlined. (The game's own cursor is not used by the
                // strip; the keys that would bring up its red corners do not reach the game.)
                for (size_t i = firstButton; i < sc.items.size(); i++)
                {
                    Element& el = sc.items[i];
                    // Move buttons have frames of their own; only the chosen one is outlined.
                    el.border = cmd;
                    el.borderColor = 0x101218;
                    el.borderWidth = 6;
                    if ((int)(i - firstButton) == battleSel)
                    {
                        el.border = true;
                        // Around the rounded move buttons the outline is rounded to match.
                        if (moves) el.borderRadius = 4.0f;
                        el.borderColor = 0xF04040;
                        el.borderWidth = 16;
                    }
                }
            }
            else if (viewWidth < 300 || battleUnknown >= 20)
            {
                sc.name = "battle-menu";
                float scale = std::min((126 - L) / 256, 98.0f / 192);
                sc.items.push_back({ 1, 0, 0, 256, 192, L + 1, 144 - 192 * scale, scale, 0, 255, false });
                sc.items.back().border = !AutoFocus::Blank(screens[1].pixels);
            }
        }
        else if (probeActive(probeStartMenu) && fieldActive())
            startMenu();
        else if (spreadFrames >= 40)
            // Safety net for screens without a layout of their own: something is waiting for a
            // touch, so both screens are shown joined as one spread rather than hiding either.
            sc = Stack("spread", { Crop(0, 0, 0, 256, 192), Crop(1, 0, 0, 256, 192) }, true, viewWidth);
        else if (cursorInUse)
            // Nothing on screen maps to the touchscreen, so show it while the cursor is out.
            only("bottom", 1);
        else
        {
            only("top", 0);
            if (sectionActive("OVY_12"))
            {
                // A battle outside its menus: messages and animations.
                sc.name = "battle";
                sc.edgeFill = true;
            }
        }
        // The overworld is drawn across the whole wide view by the widescreen 3D renderer.
        sc.wide = viewWidth >= 300 && (fieldActive() || menuReturn > 0) && !sc.items.empty() && sc.items[0].screen == 0 &&
            sc.items[0].sw == 256 && sc.items[0].sh == 192 && sc.items[0].dx == 0 && sc.items[0].scale == 1.0f;
        return sc;
    };
    Scene scene;
    scene.items = { FullScreen(0) };
    std::vector<uint32_t> mergedImage;
    SDL_Texture* mergedTex = nullptr;
    GpuCompositor* gpuCompose = nullptr;
    BattleWide battleWide;
    const bool cpuCompose = getenv("SSR_CPU_COMPOSE") != nullptr;
    int mergedTexW = 0, mergedTexH = 0;

    // Test navigator (--nav): walks the player to a list of tiles, learning which moves
    // are blocked as it goes and routing around them.
    struct NavState
    {
        std::vector<std::pair<int, int>> way;
        std::vector<int> push;
        int pushDir = -1;
        size_t idx = 0;
        std::set<std::tuple<int, int, int>> blocked;
        int dir = -1, lastDir = -1, fromX = -1, fromZ = -1, map = -1;
        uint32_t since = 0, started = 0;
        bool active = false;
        bool outdoor = true; // the permission grid only describes the overworld matrix
    } navState;
    {
        std::istringstream ss(opt.nav);
        std::string item;
        while (std::getline(ss, item, ';'))
        {
            int x, z;
            char push = 0;
            // "x,z" or "x,z,U|D|L|R": on arriving, keep walking that way until the map changes (a door).
            int n = sscanf(item.c_str(), "%d,%d,%c", &x, &z, &push);
            if (n >= 2)
            {
                navState.way.push_back({ x, z });
                const char* dirs = "UDLR";
                const char* p = n == 3 ? strchr(dirs, push) : nullptr;
                navState.push.push_back(p ? (int)(p - dirs) : -1);
            }
        }
        navState.active = !navState.way.empty();
    }
    auto readRam = [&](uint32_t a) -> uint32_t {
        if (a < 0x02000000 || a >= 0x02400000) return 0;
        uint32_t v;
        memcpy(&v, nds->MainRAM + (a & nds->MainRAMMask), 4);
        return v;
    };
    // sFieldSysPtr -> location (map id) and playerAvatar -> mapObject -> current tile.
    auto playerPos = [&](int& map, int& x, int& z) {
        uint32_t fs = readRam(0x021D4158), loc = readRam(fs + 0x20), obj = readRam(readRam(fs + 0x40) + 0x30);
        map = (int)readRam(loc);
        x = (int)readRam(obj + 0x64);
        z = (int)readRam(obj + 0x6C);
        return obj != 0;
    };
    // Movement permissions of the overworld, if given: lets the navigator plan around
    // walls and one-way ledges instead of discovering them.
    struct NavGrid { int w = 0, h = 0; std::vector<uint16_t> cells; };
    auto loadNavGrid = [](const std::string& path) {
        NavGrid g;
        std::ifstream f(path, std::ios::binary);
        uint16_t dims[2] = {};
        f.read((char*)dims, 4);
        g.w = dims[0];
        g.h = dims[1];
        g.cells.resize((size_t)g.w * g.h);
        f.read((char*)g.cells.data(), g.cells.size() * 2);
        return g;
    };
    NavGrid navOverworld;
    std::map<int, NavGrid> navInterior;
    if (!opt.navMap.empty()) navOverworld = loadNavGrid(opt.navMap);
    for (auto& e : opt.navMapIds) navInterior[e.first] = loadNavGrid(e.second);
    static const int navDX[4] = { 0, 0, -1, 1 }, navDZ[4] = { -1, 1, 0, 0 };
    // Where a step in direction d from (x, z) ends up; false if it cannot be made.
    auto navMove = [&](int x, int z, int d, int& ox, int& oz) -> bool {
        if (navState.blocked.count({ x, z, d })) return false;
        ox = x + navDX[d];
        oz = z + navDZ[d];
        const NavGrid* grid = &navOverworld;
        if (!navState.outdoor)
        {
            auto it = navInterior.find(navState.map);
            grid = it == navInterior.end() ? nullptr : &it->second;
        }
        if (!grid || grid->cells.empty() || navState.map < 0) return true;
        auto perm = [&](int px, int pz) -> uint16_t {
            return px < 0 || pz < 0 || px >= grid->w || pz >= grid->h ? 0x8000 : grid->cells[(size_t)pz * grid->w + px];
        };
        if (navState.idx < navState.way.size() && navState.way[navState.idx] == std::make_pair(ox, oz)) return true; // doors
        uint16_t p = perm(ox, oz);
        int b = p & 0xFF;
        // Ladders, stairs and door mats warp: only step on one when it is the waypoint.
        if ((b >= 0x3C && b <= 0x3F) || (b >= 0x5E && b <= 0x6F)) return false;
        if (b >= 0x38 && b <= 0x3B)
        {
            // Ledge: 0x38 jumps right, 0x39 left, 0x3A up, 0x3B down, landing on the far side.
            static const int ledgeDir[4] = { 3, 2, 0, 1 };
            if (ledgeDir[b - 0x38] != d) return false;
            ox += navDX[d];
            oz += navDZ[d];
            return !(perm(ox, oz) & 0x8000);
        }
        return !(p & 0x8000);
    };
    // Next direction (0 up, 1 down, 2 left, 3 right) on a shortest path.
    auto navStep = [&](int x, int z, int tx, int tz) -> int {
        const int R = 64;
        if (x < 0 || z < 0 || x > 4096 || z > 4096) return -1; // not a position (mid-warp)
        int x0 = std::min(x, tx) - R, z0 = std::min(z, tz) - R;
        int w = std::abs(x - tx) + 2 * R + 1, h = std::abs(z - tz) + 2 * R + 1;
        std::vector<int8_t> first((size_t)w * h, -1);
        std::deque<std::pair<int, int>> q;
        q.push_back({ x, z });
        first[(size_t)(z - z0) * w + (x - x0)] = 4;
        while (!q.empty())
        {
            auto [cx, cz] = q.front();
            q.pop_front();
            int8_t f = first[(size_t)(cz - z0) * w + (cx - x0)];
            if (cx == tx && cz == tz) return f;
            for (int d = 0; d < 4; d++)
            {
                int nx, nz;
                if (!navMove(cx, cz, d, nx, nz)) continue;
                if (nx < x0 || nz < z0 || nx >= x0 + w || nz >= z0 + h) continue;
                int8_t& slot = first[(size_t)(nz - z0) * w + (nx - x0)];
                if (slot != -1) continue;
                slot = f == 4 ? (int8_t)d : f;
                q.push_back({ nx, nz });
            }
        }
        return -1;
    };
    uint32_t scriptClock = frame; // equals frame unless --battle-mash holds it during battles

    Canvas canvas;
    Menu menu;
    enum
    {
        M_RESUME, M_LAYOUT, M_LARGE, M_FPS60, M_FULLSCREEN, M_SMOOTH, M_WIDE, M_WIDELINES, M_SHOWFPS, M_BUTTONS, M_PADTOUCH, M_FAST, M_SAVESTATE, M_LOADSTATE, M_RESET, M_QUIT
    };
    bool fastLock = false;
#if defined(SSR_UWP) || defined(SSR_LAUNCHER)
    // On the consoles and TVs these builds are for, the processor may not keep up with every
    // setting: when frames keep coming late, the costliest are lowered one step at a time
    // (see where frames are paced), until the player changes one of them in the menu.
    bool autoDetail = true;
#else
    bool autoDetail = false;
#endif
    // Per 4-second window: frames whose work took nearly a whole frame or more, and the
    // work time summed; whether a raise has been undone (then no more raises).
    int slowFrames = 0, pacedFrames = 0;
    double workSum = 0;
    bool raised = false, raiseUndone = false;
    // World detail choices: the most lines the widescreen 3D is drawn at (192 is the DS's own).
    static const int detailLines[] = { 192, 288, 384, 540, 720, 1080, 1440 };
    const int detailCount = (int)(sizeof(detailLines) / sizeof(detailLines[0]));
    auto detailIndex = [&]() {
        int i = 0;
        while (i + 1 < detailCount && detailLines[i + 1] <= cfg.wideLines) i++;
        return i;
    };
    auto buildMenu = [&]() {
        static const char* large[] = { "AUTO", "TOP", "BOTTOM" };
        menu.items = {
            { "RESUME", "" },
            { "SCREEN LAYOUT", LayoutName(layout.mode) },
            { "LARGE SCREEN", large[focus.mode] },
            { "60 FPS", cfg.fps60 ? "ON" : "OFF" },
            { "FULLSCREEN", cfg.fullscreen ? "ON" : "OFF" },
            { "SMOOTH SCALING", cfg.smooth ? "ON" : "OFF" },
            { "WIDESCREEN WORLD", cfg.wide ? "ON" : "OFF" },
            { "WORLD DETAIL", std::to_string(detailLines[detailIndex()]) },
            { "FPS COUNTER", cfg.showFps ? "ON" : "OFF" },
            { "BUTTONS", cfg.byLabel ? "BY LABEL" : "BY POSITION" },
            { "STICK TOUCH CURSOR", cfg.padTouch ? "ON" : "OFF" },
            { "FAST FORWARD", fastLock ? "ON" : "OFF" },
            { "SAVE STATE", "" },
            { "LOAD STATE", "" },
            { "RESET GAME", "" },
            { "QUIT", "" },
        };
    };
    buildMenu();

    std::string toast;
    uint32_t toastUntil = 0;
    int shownFps = 0;
    uint32_t presented = 0, fpsSince = 0;
    auto say = [&](const std::string& msg) {
        toast = msg;
        toastUntil = frame + 120;
    };
#if defined(__ANDROID__) && !defined(SSR_LAUNCHER) // (the launcher is not rebuilt on a PC)
    bool updateSaid = false;
    UpdateCheck::Start();
#endif

    std::vector<uint32_t> shot;
    std::vector<int16_t> audioBuf(48000 * 2);
    bool running = true;
    bool mouseDown = false;
    int mouseX = 0, mouseY = 0;
    // On-screen controls (touch screens): shown while no controller is connected.
    TouchPad touchPad;
    bool touchPadUsed = false, touchMenuHeld = false, prevTouchMenu = false;
#ifdef __ANDROID__
    touchPadUsed = true;
#ifdef SSR_LAUNCHER
    // A TV has no touch screen: no on-screen controls over the picture there.
    touchPadUsed = SDL_GetNumTouchDevices() > 0;
#endif
#endif
    // Touch cursor for devices without a touchscreen: moved with the right stick or IJKL,
    // pressed with the right trigger or Space.
    float cursorX = 128, cursorY = 96;
    uint32_t cursorShownUntil = 0;
    PadState prevPad;
    MenuInput heldNav;          // navigation directions held last tick, for edge detection
    bool prevComboMenu = false;
    bool swallowMenuButtons = false; // keeps the buttons that closed the menu from reaching the game
    const double frameTime = 1.0 / 59.8261;
    uint64_t perfFreq = opt.headless ? 1 : SDL_GetPerformanceFrequency();
    uint64_t nextTick = opt.headless ? 0 : SDL_GetPerformanceCounter();

    Sampler::Start();
    double perfAcc[6] = {};
    double perfWorst = 0;
    int perfN = 0;
    auto perfNow = []() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    // Parts of the scene and present phases (screen analysis, scene choice, audio, handing the
    // frame to the graphics card), printed on a second line: where those phases spend it.
    double perfPart[4] = {};
    auto perfTimed = [&](int part, auto&& work) {
        if (!perfOn) return work();
        const double t = perfNow();
        work();
        perfPart[part] += perfNow() - t;
    };
    double perfT = perfNow(), perfFrameStart = perfT;
    auto perfMark = [&](int phase) {
        if (!perfOn) return;
        double t = perfNow();
        perfAcc[phase] += t - perfT;
        perfT = t;
    };

    while (running && !Host::StopRequested)
    {
        const uint64_t frameWorkStart = opt.headless ? 0 : SDL_GetPerformanceCounter();
        if (perfOn)
        {
            double t = perfNow();
            if (t - perfFrameStart > perfWorst) perfWorst = t - perfFrameStart;
            perfFrameStart = perfT = t;
            if (++perfN == 300)
            {
                fprintf(stderr, "[perf] input %.2f  emulate %.2f  scene %.2f  compose %.2f  present %.2f  wait %.2f ms; worst frame %.1f ms\n",
                    perfAcc[0] / perfN, perfAcc[1] / perfN, perfAcc[2] / perfN, perfAcc[3] / perfN, perfAcc[4] / perfN,
                    perfAcc[5] / perfN, perfWorst);
                // (3D: drawing time of the DS-resolution 3D on its own thread, alongside emulate.)
                auto& soft3d = static_cast<melonDS::SoftRenderer&>(nds->GPU.GetRenderer3D());
                fprintf(stderr, "[perf parts] screens %.2f  scene choice %.2f  audio %.2f  present call %.2f  3D %.2f ms\n",
                    perfPart[0] / perfN, perfPart[1] / perfN, perfPart[2] / perfN, perfPart[3] / perfN,
                    soft3d.RenderMicros.exchange(0) / 1000.0 / perfN);
                for (double& a : perfAcc) a = 0;
                for (double& a : perfPart) a = 0;
                perfN = 0;
                perfWorst = 0;
            }
        }
        uint32_t keys = 0;
        bool touching = false;
        int touchX = 0, touchY = 0;
        MenuInput nav;              // edge-triggered menu input for this tick
        bool toggleMenu = false;
        bool kbFast = false;
        float curDX = 0, curDY = 0;
        bool curPress = false;
        PadState ps;

        int outW = opt.composeW > 0 ? opt.composeW : 1280, outH = opt.composeH > 0 ? opt.composeH : 720;

        if (!opt.headless)
        {
            SDL_Event ev;
            touchPad.NextFrame();
            while (SDL_PollEvent(&ev))
            {
#ifdef SSR_UWP
                // That "back" is the B button, which also arrives from the controller as
                // itself: as a key it must not open the menu as well.
                if ((ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) && ev.key.keysym.sym == SDLK_AC_BACK) continue;
#endif
                if (ev.type == SDL_QUIT) running = false;
                else if (ev.type == SDL_APP_WILLENTERBACKGROUND || ev.type == SDL_APP_TERMINATING)
                {
                    // A phone may close the app at any time once it is in the background.
                    if (!opt.noSave) FlushSave(opt.save);
                    cfg.Save(opt.config);
                }
                else if (touchPadUsed && (ev.type == SDL_FINGERDOWN || ev.type == SDL_FINGERMOTION || ev.type == SDL_FINGERUP))
                {
                    int ow = 0, oh = 0;
                    SDL_GetRendererOutputSize(renderer, &ow, &oh);
                    touchPad.Layout(ow, oh);
                    float fx = ev.tfinger.x * ow, fy = ev.tfinger.y * oh;
                    if (ev.type == SDL_FINGERDOWN)
                    {
                        // A finger on the screen brings the controls back (that finger only
                        // touches the picture, see Down).
                        touchPad.Down(ev.tfinger.fingerId, fx, fy);
                        touchPad.visible = true;
                    }
                    else if (ev.type == SDL_FINGERMOTION) touchPad.Move(ev.tfinger.fingerId, fx, fy);
                    else touchPad.Up(ev.tfinger.fingerId);
                }
                else if (ev.type == SDL_KEYDOWN && !ev.key.repeat)
                {
                    SDL_Keycode k = ev.key.keysym.sym;
                    if (k == SDLK_ESCAPE || k == SDLK_AC_BACK)
                    {
                        if (menu.open) nav.back = true;
                        else toggleMenu = true;
                    }
                    else if (menu.open)
                    {
                        if (k == SDLK_UP) nav.up = true;
                        else if (k == SDLK_DOWN) nav.down = true;
                        else if (k == SDLK_LEFT) nav.left = true;
                        else if (k == SDLK_RIGHT) nav.right = true;
                        else if (k == SDLK_RETURN || k == SDLK_x) nav.accept = true;
                        else if (k == SDLK_z) nav.back = true;
                    }
                    else if (k == SDLK_TAB) layout = LayoutNext(layout);
                    else if (k == SDLK_F1) focus.manualFlip = !focus.manualFlip;
                    else if (k == SDLK_F2) focus.mode = (focus.mode + 1) % 3;
                    else if (k == SDLK_F5) say(WriteSnapshot(*nds, quickState, frame) ? "STATE SAVED" : "SAVE STATE FAILED");
                    else if (k == SDLK_F8)
                    {
                        say(ReadSnapshot(*nds, quickState, nullptr) ? "STATE LOADED" : "NO STATE TO LOAD");
                        SetClock(*nds, false);
                    }
                    else if (k == SDLK_F11
#ifdef __APPLE__
                        || (k == SDLK_f && (ev.key.keysym.mod & KMOD_GUI) && (ev.key.keysym.mod & KMOD_CTRL))
#endif
                    )
                    {
                        cfg.fullscreen = !cfg.fullscreen;
                        SDL_SetWindowFullscreen(window, cfg.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        SDL_ShowCursor(cfg.fullscreen ? SDL_DISABLE : SDL_ENABLE);
                    }
                }
                else if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                    mouseDown = false;
                else if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) mouseDown = true;
                else if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) mouseDown = false;
                // (Every joystick is announced by SDL_JOYDEVICEADDED, game controllers also
                // by SDL_CONTROLLERDEVICEADDED; Open takes each once.)
                else if (ev.type == SDL_JOYDEVICEADDED || ev.type == SDL_CONTROLLERDEVICEADDED)
                {
                    if (pads.Open(ev.jdevice.which)) say("CONTROLLER CONNECTED");
                }
                else if (ev.type == SDL_JOYDEVICEREMOVED && pads.Close(ev.jdevice.which))
                {
                    // Pause rather than leave the game running with nobody at the controls.
                    if (pads.Empty() && !menu.open) toggleMenu = true;
                    say("CONTROLLER DISCONNECTED");
                }
            }
            SDL_GetMouseState(&mouseX, &mouseY);
            SDL_GetRendererOutputSize(renderer, &outW, &outH);
            // SDL mouse positions are window points; layout, cursor and touch use output pixels.
            // Refresh each frame for resize and movement between Retina/external displays.
            int windowW = 0, windowH = 0;
            SDL_GetWindowSize(window, &windowW, &windowH);
            if (outW <= 0 || outH <= 0 || windowW <= 0 || windowH <= 0 ||
                (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED))
            {
                SDL_Delay(16);
                continue;
            }
            mouseX = int(double(mouseX) * outW / windowW);
            mouseY = int(double(mouseY) * outH / windowH);
            ps = ReadPad(pads);
            if (touchPadUsed)
            {
                touchPad.Layout(outW, outH);
                // Using a controller hides them: a connected one is not proof enough, as
                // Android reports some built-in keys as one.
                bool padUsed = fabsf(ps.lx) > 0.5f || fabsf(ps.ly) > 0.5f;
                for (int i = 0; i < PAD_COUNT; i++) padUsed |= ps.btn[i];
                if (padUsed) touchPad.visible = false;
                // A finger on the picture touches the game's touch screen, like the mouse.
                float fx, fy;
                mouseDown = touchPad.ScreenFinger(fx, fy);
                if (mouseDown)
                {
                    mouseX = (int)fx;
                    mouseY = (int)fy;
                }
                // The on-screen buttons go by their labels: the one marked A is the DS's A.
                bool held[PAD_COUNT] = {};
                touchPad.Apply(held, touchMenuHeld);
                if (!cfg.byLabel)
                {
                    std::swap(held[PAD_A], held[PAD_B]);
                    std::swap(held[PAD_X], held[PAD_Y]);
                }
                for (int i = 0; i < PAD_COUNT; i++) ps.btn[i] |= held[i];
                if (touchMenuHeld && !prevTouchMenu) toggleMenu = true;
                prevTouchMenu = touchMenuHeld;
            }

            const Uint8* kb = SDL_GetKeyboardState(nullptr);
            if (!menu.open)
            {
                keys |= KeyboardKeys();
                if (kb[SDL_SCANCODE_J]) curDX -= 1;
                if (kb[SDL_SCANCODE_L]) curDX += 1;
                if (kb[SDL_SCANCODE_I]) curDY -= 1;
                if (kb[SDL_SCANCODE_K]) curDY += 1;
                if (kb[SDL_SCANCODE_SPACE]) curPress = true;
                if (kb[SDL_SCANCODE_LSHIFT]) kbFast = true;
            }
        }

        const bool mashing = opt.battleMash && appRunning("Battle");
        if (mashing && (frame / 12) % 2 == 0)
        {
            // A fights; if that wandered into the in-battle party or bag screen (e.g. by answering
            // Yes to "switch Pokemon?"), B backs out and declines the question that led there.
            // If B does not leave it (the lead fainted), move to the next Pokemon and send it out.
            static uint32_t submenuSeen = 0, submenuSince = 0;
            if (sectionActive("OVY_8"))
            {
                if (!submenuSince || frame - submenuSeen > 24) submenuSince = frame;
                submenuSeen = frame;
            }
            if (submenuSeen == frame && frame - submenuSince > 600)
            {
                static const int cycle[4] = { KEY_B, KEY_RIGHT, KEY_A, KEY_A };
                keys |= 1 << cycle[(frame / 24) % 4];
            }
            else if (submenuSeen && frame - submenuSeen < 240)
                keys |= 1 << KEY_B;
            else if ((frame / 24) % 2 == 0)
                keys |= 1 << KEY_A;
            else
            {
                // Every other press is a touch on one of the left-hand moves (FIGHT in the command
                // menu), so that the first move running out of PP does not stall the battle.
                touching = true;
                touchX = 60;
                touchY = (frame / 48) % 2 ? 115 : 55;
            }
        }
        bool navigating = false;
        if (navState.active && opt.navStopOnBattle && navState.started && appRunning("Battle"))
        {
            printf("nav: battle started at frame %u, stopping\n", frame);
            navState.active = false;
        }
        if (navState.active && !mashing && scriptClock >= opt.navAfter)
        {
            int map, x, z;
            if (playerPos(map, x, z))
            {
                navigating = true;
                if (!navState.started) navState.started = frame;
                if (navState.map != -1 && map != navState.map)
                {
                    // Went through a door: that waypoint is done and the grid is a new one.
                    printf("nav: map %d -> %d at frame %u\n", navState.map, map, frame);
                    if (navState.idx < navState.way.size() &&
                        std::abs(navState.way[navState.idx].first - navState.fromX) +
                        std::abs(navState.way[navState.idx].second - navState.fromZ) <= 2)
                        navState.idx++;
                    navState.blocked.clear();
                    navState.dir = -1;
                    navState.pushDir = -1;
                }
                navState.map = map;
                navState.outdoor = x >= 96 || z >= 96; // interiors have their own small grids
                while (navState.idx < navState.way.size() && navState.way[navState.idx] == std::make_pair(x, z))
                {
                    printf("nav: reached %d,%d at frame %u\n", x, z, frame);
                    navState.pushDir = navState.push[navState.idx];
                    navState.idx++;
                    navState.dir = -1;
                }
                if (navState.pushDir >= 0)
                {
                    static const int pushKeys[4] = { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT };
                    keys |= 1 << pushKeys[navState.pushDir];
                }
                else if (navState.idx >= navState.way.size() || frame - navState.started > 150000)
                {
                    printf("nav: %s at frame %u, map %d x %d z %d\n",
                        navState.idx >= navState.way.size() ? "done" : "gave up", frame, map, x, z);
                    navState.active = false;
                    navigating = false;
                }
                else
                {
                    if (navState.dir >= 0 && (x != navState.fromX || z != navState.fromZ))
                        navState.dir = -1; // moved
                    else if (navState.dir >= 0 && frame - navState.since > 60)
                    {
                        navState.blocked.insert({ x, z, navState.dir });
                        navState.dir = -1;
                    }
                    if (navState.dir < 0)
                    {
                        auto [tx, tz] = navState.way[navState.idx];
                        navState.dir = navStep(x, z, tx, tz);
                        if (navState.dir < 0)
                        {
                            navState.blocked.clear();
                            navState.dir = navStep(x, z, tx, tz);
                        }
                        navState.fromX = x;
                        navState.fromZ = z;
                        navState.since = frame;
                    }
                    static const int navKeys[4] = { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT };
                    if (navState.dir >= 0)
                    {
                        keys |= 1 << navKeys[navState.dir];
                        navState.lastDir = navState.dir;
                    }
                    else if (navState.lastDir >= 0)
                        keys |= 1 << navKeys[navState.lastDir]; // side doors open by walking on into them
                    // With no route (e.g. mid-warp, the target is on the next map) just wait.
                    // Dismiss anything that stops the walk (signs, people talking).
                    if (frame - navState.since > 24 && (frame / 6) % 2 == 0) keys |= 1 << KEY_B;
                }
            }
        }
        const bool holdScript = mashing || navigating;
        for (const ScriptEvent& ev : script)
        {
            if (holdScript) break;
            if (scriptClock < ev.start || scriptClock > ev.end) continue;
            keys |= ev.keys;
            if (ev.touch)
            {
                touching = true;
                touchX = ev.tx;
                touchY = ev.ty;
            }
            for (int i = 0; i < PAD_COUNT; i++)
                if (ev.padButtons & (1u << i)) ps.btn[i] = true;
            if (ev.lx != 0) ps.lx = ev.lx;
            if (ev.ly != 0) ps.ly = ev.ly;
            if (ev.rx != 0) ps.rx = ev.rx;
            if (ev.ry != 0) ps.ry = ev.ry;
            if (ev.lt != 0) ps.lt = ev.lt;
            if (ev.rt != 0) ps.rt = ev.rt;
        }

        // Controller: the pause menu opens with Guide, or with Start and Select together.
        auto pressed = [&](int b) { return ps.btn[b] && !prevPad.btn[b]; };
        bool comboMenu = ps.btn[PAD_START] && ps.btn[PAD_BACK];
        if (pressed(PAD_GUIDE) || (comboMenu && !prevComboMenu)) toggleMenu = true;
        prevComboMenu = comboMenu;

        MenuInput heldNow;
        heldNow.up = ps.btn[PAD_UP] || ps.ly < -0.5f;
        heldNow.down = ps.btn[PAD_DOWN] || ps.ly > 0.5f;
        heldNow.left = ps.btn[PAD_LEFT] || ps.lx < -0.5f;
        heldNow.right = ps.btn[PAD_RIGHT] || ps.lx > 0.5f;
        if (menu.open)
        {
            nav.up |= heldNow.up && !heldNav.up;
            nav.down |= heldNow.down && !heldNav.down;
            nav.left |= heldNow.left && !heldNav.left;
            nav.right |= heldNow.right && !heldNav.right;
            nav.accept |= pressed(cfg.byLabel ? PAD_A : PAD_B);
            nav.back |= pressed(cfg.byLabel ? PAD_B : PAD_A);
        }
        heldNav = heldNow;

        if (toggleMenu)
        {
            menu.open = !menu.open;
            if (menu.open)
            {
                menu.selected = 0;
                buildMenu();
            }
            else swallowMenuButtons = true;
        }
        else if (menu.open)
        {
            if (nav.back)
            {
                menu.open = false;
                swallowMenuButtons = true;
            }
            int dir = 0;
            int hit = menu.Update(nav, dir);
            switch (hit)
            {
            case M_RESUME:
                menu.open = false;
                swallowMenuButtons = true;
                break;
            case M_LAYOUT:
                layout.mode = (LayoutMode)(((int)layout.mode + (dir < 0 ? 5 : 1)) % 6);
                break;
            case M_LARGE:
                focus.mode = (focus.mode + (dir < 0 ? 2 : 1)) % 3;
                focus.manualFlip = false;
                break;
            case M_FPS60:
                cfg.fps60 = !cfg.fps60;
                nds->GPU.GPU3D.InterpolationEnabled = cfg.fps60;
                autoDetail = false; // the player's choice from now on
                break;
            case M_FULLSCREEN:
                cfg.fullscreen = !cfg.fullscreen;
                if (window)
                {
                    SDL_SetWindowFullscreen(window, cfg.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                    SDL_ShowCursor(cfg.fullscreen ? SDL_DISABLE : SDL_ENABLE);
                }
                break;
            case M_SMOOTH:
                cfg.smooth = !cfg.smooth;
                applySmooth();
                break;
            case M_WIDE:
                cfg.wide = !cfg.wide;
                break;
            case M_WIDELINES:
                cfg.wideLines = detailLines[(detailIndex() + (dir < 0 ? detailCount - 1 : 1)) % detailCount];
                autoDetail = false;
                break;
            case M_SHOWFPS:
                cfg.showFps = !cfg.showFps;
                break;
            case M_BUTTONS:
                cfg.byLabel = !cfg.byLabel;
                break;
            case M_PADTOUCH:
                cfg.padTouch = !cfg.padTouch;
                break;
            case M_FAST:
                fastLock = !fastLock;
                break;
            case M_SAVESTATE:
                say(WriteSnapshot(*nds, quickState, frame) ? "STATE SAVED" : "SAVE STATE FAILED");
                menu.open = false;
                swallowMenuButtons = true;
                break;
            case M_LOADSTATE:
                say(ReadSnapshot(*nds, quickState, nullptr) ? "STATE LOADED" : "NO STATE TO LOAD");
                SetClock(*nds, false);
                menu.open = false;
                swallowMenuButtons = true;
                break;
            case M_RESET:
                nds->Reset();
                nds->SetupDirectBoot("game.nds");
                nds->Start();
                rc_reset();
                say("GAME RESET");
                menu.open = false;
                swallowMenuButtons = true;
                break;
            case M_QUIT:
                running = false;
                break;
            default:
                break;
            }
            if (hit >= 0) buildMenu();
        }

        bool anyHeld = false;
        for (int i = 0; i < PAD_COUNT; i++) anyHeld |= ps.btn[i];
        if (swallowMenuButtons && !anyHeld) swallowMenuButtons = false;

        bool paused = menu.open;
        int speed = 1;
        if (!paused && !swallowMenuButtons)
        {
            keys |= PadKeys(ps, cfg.byLabel);
            // Only presses made while the command strip is up count; anything done in the bag or
            // party screens must not move it afterwards, and its next showing starts from the ROM.
            const bool stripUp = singleScreenFlag && (singleScreenFlag[1] == 1 || singleScreenFlag[1] == 2);
            if (!stripUp) { stripNav.clear(); stripPubSel = -1; }
            if (cfg.padTouch)
            {
                if (pressed(PAD_RS)) focus.manualFlip = !focus.manualFlip;
                if (pressed(PAD_LS)) layout = LayoutNext(layout);
                if (fabsf(ps.rx) > 0.2f) curDX += ps.rx;
                if (fabsf(ps.ry) > 0.2f) curDY += ps.ry;
                if (ps.rt > 0.3f) curPress = true;
            }
        }
        if (!paused && (fastLock || kbFast || ps.lt > 0.5f)) speed = 4;
        prevPad = ps;

        // A decompilation build with SINGLE_SCREEN puts everything on the engine of the touch
        // screen. It carries a tag, searched for in main memory once the game has unpacked itself,
        // and a flag next to it that the game sets while a scene needs both screens (the intro).
        if (!singleScreenRom && !opt.layoutGiven && frame % 30 == 15 && frame < 1800)
        {
            static const char tag[] = "SSRECOMP-SINGLE-SCREEN-1";
            const uint8_t* ram = nds->MainRAM;
            const uint8_t* end = ram + nds->MainRAMMask + 1;
            const uint8_t* hit = std::search(ram, end, tag, tag + sizeof(tag) - 1);
            if (hit != end)
            {
                singleScreenRom = true;
                singleScreenFlag = hit + 32; // the build info struct: 32 bytes of tag, then the flag
                preSingleLayout = layout.mode;
                fprintf(stderr, "single-screen build detected\n");
            }
        }
        // The ROM says per scene whether it wants both screens (the intro movie).
        if (singleScreenRom && !opt.layoutGiven)
            layout.mode = *singleScreenFlag ? preSingleLayout : LayoutMode::Merged;
        SDL_Rect rects[2];
        LayoutRects(layout, outW, outH, rects);
        const bool merged = layout.mode == LayoutMode::Merged;
        const MergedView view = MergedMakeView(outW, outH);
        // A single-screen build's visible screen is the touch screen: touches land on it directly.
        auto touchAt = [&](float bx, float by, int& tx, int& ty) {
            if (!singleView()) return MergedTouch(scene, bx, by, tx, ty);
            if (bx < 0 || bx >= 256 || by < 0 || by >= 192) return false;
            tx = (int)bx;
            ty = (int)by;
            return true;
        };
        // Widescreen 3D: on while the overworld is running in the merged view on a wide
        // output. It is drawn at the size of the picture, up to 1440 lines.
        static int wideFrames = 0;
        {
            int ww = opt.headless && opt.composeW ? opt.composeW : outW, wh = opt.headless && opt.composeH ? opt.composeH : outH;
            // Drawing it costs time in proportion to its size; cfg.wideLines caps its height
            // and the result is enlarged to the picture.
            if (wh > cfg.wideLines)
            {
                ww = ww * cfg.wideLines / wh;
                wh = cfg.wideLines;
            }
            const bool wantWide = merged && cfg.wide && !opt.noWide && !opt.lockstep && view.width >= 300 &&
                (fieldActive() || menuReturn > 0 || scene.edgeFill || (singleView() && singleFieldFrame && rc_current_frame() - singleFieldFrame < 20));
            melonDS::Wide.Owner = wantWide ? &nds->GPU.GPU3D : nullptr;
            melonDS::Wide.ViewWidth = view.width;
            melonDS::Wide.Width = ww;
            melonDS::Wide.Height = wh;
            wideFrames = wantWide ? wideFrames + 1 : 0;
        }
        // Filled in after the frame has run: the renderer hands over its picture then.
        WideImage wideImage;
        auto takeWideImage = [&]() {
            wideImage = WideImage();
            // A battle on a wide view: the screen widened (battlewide.h) instead of the bars.
            if (scene.edgeFill && cfg.wide && !opt.noWide)
            {
                WideImage world;
                if (wideFrames > 3 && !melonDS::Wide.Front.empty())
                {
                    world.pixels = melonDS::Wide.Front.data();
                    world.w = melonDS::Wide.FrontW;
                    world.h = melonDS::Wide.FrontH;
                }
                const float vw = MergedMakeView(outW, outH).width;
                const int tu = topUnitOf();
                const ScreenImage topImage = { nds->GPU.Framebuffer[nds->GPU.FrontBuffer][singleView() ? 1 : 0].get(),
                    static_cast<GPU2D::SoftRenderer&>(nds->GPU.GetRenderer2D()).LayerFlags[tu] };
                if (vw >= 300 && battleWide.Build(*nds, topImage, (int)std::ceil((vw - 256) / 2), &world, vw, wideImage, tu))
                {
                    scene.wide = true;
                    return;
                }
            }
            else
                battleWide.Reset();
            if (wideFrames > 3 && !melonDS::Wide.Front.empty())
            {
                wideImage.pixels = melonDS::Wide.Front.data();
                wideImage.w = melonDS::Wide.FrontW;
                wideImage.h = melonDS::Wide.FrontH;
                // Fades: the game darkens or whitens its screen with the master brightness
                // register, or with a brightness effect on the 3D layer.
                int topUnit = topUnitOf();
                const auto& unit = topUnit == 0 ? nds->GPU.GPU2D_A : nds->GPU.GPU2D_B;
                int mode = (unit.MasterBrightness >> 14) & 3, f = std::min(16, unit.MasterBrightness & 0x1F);
                if ((mode != 1 && mode != 2) || !f)
                {
                    int effect = (unit.BlendCnt >> 6) & 3;
                    if ((unit.BlendCnt & 1) && effect >= 2)
                    {
                        mode = effect == 2 ? 1 : 2;
                        f = std::min<int>(16, unit.EVY);
                    }
                    else
                        mode = 0;
                }
                wideImage.fadeMode = mode;
                wideImage.fade = f;
            }
            if (opt.logSections)
            {
                // Where the widescreen picture comes and goes, and why.
                static int last = -1;
                int now = (wideImage.pixels ? 1 : 0) | (scene.wide ? 2 : 0) | (melonDS::Wide.Owner ? 4 : 0);
                if (now != last)
                    fprintf(stderr, "[wide] frame %u: picture %d, scene %s wide %d, renderer on %d, fade %d/%d\n", frame,
                        now & 1, scene.name, (now >> 1) & 1, (now >> 2) & 1, wideImage.fadeMode, wideImage.fade);
                last = now;
            }
        };
        if (!paused)
        {
            if (!opt.headless && mouseDown)
            {
                if (merged)
                {
                    float bx = (mouseX - view.originX) / view.unit, by = (mouseY - view.originY) / view.unit;
                    if (touchAt(bx, by, touchX, touchY)) touching = true;
                }
                else if (LayoutTouch(rects[1], mouseX, mouseY, touchX, touchY)) touching = true;
            }
            if (curDX != 0 || curDY != 0 || curPress)
            {
                // Faster with a fuller stick deflection, for both precision and reach.
                float mag = sqrtf(curDX * curDX + curDY * curDY);
                float step = 1.0f + 3.5f * mag;
                cursorX += curDX * step;
                cursorY += curDY * step;
                // In the merged view the cursor can also reach pieces drawn beside the 4:3 area.
                float margin = merged ? (view.width - 256) / 2 : 0;
                if (cursorX < -margin) cursorX = -margin;
                if (cursorX > 255 + margin) cursorX = 255 + margin;
                if (cursorY < 0) cursorY = 0;
                if (cursorY > 191) cursorY = 191;
                cursorShownUntil = frame + 180;
            }
            if (curPress && !touching)
            {
                if (!merged)
                {
                    touching = true;
                    touchX = (int)cursorX;
                    touchY = (int)cursorY;
                }
                else if (touchAt(cursorX, cursorY, touchX, touchY)) touching = true;
            }
        }

        // Battle strips: left and right walk along the buttons in the order they are shown,
        // and A presses the chosen one by touching it on the touch screen. The game's own
        // cursor moves in the touch screen's arrangement (FIGHT above the other three),
        // which does not match a row, so the arrows and A do not reach it here.
        {
            static std::string stripKind;
            static uint32_t prevStripKeys = 0;
            static int stripTouch = 0, stripTouchX = 0, stripTouchY = 0;
            const bool cmd = merged && !strcmp(scene.name, "battle-cmd"), moves = merged && !strcmp(scene.name, "battle-moves");
            if (!paused && (cmd || moves))
            {
                if (stripKind != scene.name)
                {
                    stripKind = scene.name;
                    battleSel = 0;
                    prevStripKeys = keys; // a key still held from before does not count
                }
                const uint32_t edge = keys & ~prevStripKeys;
                prevStripKeys = keys;
                static const int cmdX[4] = { 128, 39, 216, 128 }, cmdY[4] = { 84, 168, 168, 172 };
                static const int movX[4] = { 64, 192, 64, 192 }, movY[4] = { 52, 52, 116, 116 };
                // Empty move slots are skipped: theirs is a plain grey where a move's name is white.
                const u32* screenImagesPrev = nds->GPU.Framebuffer[nds->GPU.FrontBuffer][1].get();
                auto usable = [&](int i) {
                    if (cmd) return true;
                    uint32_t c = screenImagesPrev[(movY[i] - 14) * 256 + movX[i]];
                    return ((c >> 16) & 0xFF) > 200 && ((c >> 8) & 0xFF) > 200 && (c & 0xFF) > 200;
                };
                for (int dir : { (edge & (1 << KEY_RIGHT)) ? 1 : 0, (edge & (1 << KEY_LEFT)) ? -1 : 0 })
                    if (dir)
                        for (int i = battleSel + dir; i >= 0 && i < 4; i += dir)
                            if (usable(i)) { battleSel = i; break; }
                if (cmd)
                {
                    if (edge & (1 << KEY_LEFT)) stripNav += 'L';
                    if (edge & (1 << KEY_RIGHT)) stripNav += 'R';
                    if (edge & (1 << KEY_UP)) stripNav += 'U';
                    if (edge & (1 << KEY_DOWN)) stripNav += 'D';
                }
                if (opt.logSections && edge) fprintf(stderr, "[strip] frame %u: keys %03x, chosen %d\n", frame, edge, battleSel);
                if (edge & (1 << KEY_A))
                {
                    stripTouch = 4;
                    // On the command row the chosen button is the one the border shows (stripSel).
                    const int pick = cmd ? stripSel : battleSel;
                    stripTouchX = cmd ? cmdX[pick] : movX[pick];
                    stripTouchY = cmd ? cmdY[pick] : movY[pick];
                }
                // B in the move list touches CANCEL. Given to the game as a key, the first
                // press only brought up its own cursor. On the command row it does nothing.
                if (moves && (edge & (1 << KEY_B)))
                {
                    stripTouch = 4;
                    stripTouchX = 128;
                    stripTouchY = 172;
                }
                keys &= ~((1u << KEY_LEFT) | (1u << KEY_RIGHT) | (1u << KEY_UP) | (1u << KEY_DOWN) | (1u << KEY_A) | (1u << KEY_B));
            }
            else
                stripKind.clear();
            if (!paused && stripTouch > 0)
            {
                touching = true;
                touchX = stripTouchX;
                touchY = stripTouchY;
                stripTouch--;
            }
        }

        if (ref && !paused)
        {
            TraceLog.clear();
            TracePos = 0;
            TraceFrame = frame;
            ref->SetKeyMask(~keys & 0xFFF);
            if (touching) ref->TouchScreen((u16)touchX, (u16)touchY);
            else ref->ReleaseScreen();
            ref->RunFrame();
            TracePrevR15 = nds->ARM9.R[15];
            TracePrevCPSR = nds->ARM9.CPSR;
        }

        perfMark(0);
        int got = 0;
        if (!paused)
        {
            nds->SetKeyMask(~keys & 0xFFF);
            if (touching) nds->TouchScreen((u16)touchX, (u16)touchY);
            else nds->ReleaseScreen();

            for (int i = 0; i < speed; i++)
            {
                rc_frame++;
                nds->RunFrame();
                int avail = nds->SPU.GetOutputSize();
                if (avail > (int)audioBuf.size() / 2) avail = (int)audioBuf.size() / 2;
                int n = nds->SPU.ReadOutput(audioBuf.data(), avail);
                if (i == 0) got = n; // when fast-forwarding only the first frame's audio is played
            }
        }
        perfMark(1);
        frame++;
        if (!holdScript) scriptClock++;

        if (opt.traceFuncsAt.count(frame))
        {
            char name[64];
            snprintf(name, sizeof(name), "/funcs_%06u.txt", frame);
            std::ofstream out((opt.shotDir.empty() ? std::string(".") : opt.shotDir) + name);
            unsigned now = rc_current_frame();
            for (auto& tp : traceProbes)
                if (rc_probe_last_frame(tp.first) + 1 >= now) out << tp.second << "\n";
        }

        if (opt.logSections)
        {
            static std::string lastActive;
            std::string active;
            for (u32 i = 0; i < rc_num_sections; i++)
                if (rc_section_last_frame[i] == rc_frame && strcmp(rc_sections[i].name, "main") && strcmp(rc_sections[i].name, "ITCM") &&
                    strcmp(rc_sections[i].name, "bios9"))
                    active += std::string(" ") + rc_sections[i].name;
            if (active != lastActive)
            {
                fprintf(stderr, "[sections] frame %u:%s\n", frame, active.c_str());
                lastActive = active;
            }
        }

        if (ref && !paused)
        {
            if (!TraceFailed && TracePos != TraceLog.size())
            {
                fprintf(stderr, "LOCKSTEP MISMATCH frame %u: recomp ran %zu instructions, interpreter %zu\n",
                    frame - 1, TracePos, TraceLog.size());
                TraceFailed = true;
            }
            if (TraceFailed) break;
            if (frame % 300 == 0)
                fprintf(stderr, "[lockstep] %u frames identical (%zu instructions in the last one)\n", frame, TraceLog.size());
        }

        const u32* top = nds->GPU.Framebuffer[nds->GPU.FrontBuffer][singleView() ? 1 : 0].get();
        const u32* bot = nds->GPU.Framebuffer[nds->GPU.FrontBuffer][singleView() ? 0 : 1].get();
        // The two screens with their per-pixel layer flags, for the merged view.
        auto& renderer2d = static_cast<GPU2D::SoftRenderer&>(nds->GPU.GetRenderer2D());
        const int topUnit = topUnitOf();
        const ScreenImage screenImages[2] = {
            { top, renderer2d.LayerFlags[topUnit] },
            { bot, renderer2d.LayerFlags[1 - topUnit] },
        };
        const bool fieldNow = rc_probe_last_frame(probeFieldMap) + 1 >= rc_current_frame() && rc_probe_last_frame(probeFieldMap);
        for (u32 sec = 0; sec < rc_num_sections; sec++)
        {
            // (A section that misses a frame or two still counts as running; only a gap of
            // more than three frames starts the count again.)
            const unsigned last = rc_section_last_frame[sec];
            if (!last || last + 3 < rc_current_frame()) sectionStreak[sec] = 0;
            else if (last == rc_current_frame()) sectionStreak[sec]++;
        }
        bool appBottom = false;
        for (const RcApp* app : focus.bottomApps)
            if (appActive(app->name)) appBottom = true;
        appBottomNow = appBottom;
        if (top && bot && !paused) perfTimed(0, [&] { focus.Update(top, bot, fieldNow, appBottom); });
        layout.swapped = focus.Swapped();
        // While the touch cursor is in use the bottom screen has to be big enough to aim at.
        if (!merged && focus.mode == 0 && frame < cursorShownUntil) layout.swapped = true;
        LayoutRects(layout, outW, outH, rects);
        if (!paused)
        {
            // A new layout has to be asked for a few frames in a row before it replaces the
            // one on screen, so that the frames of a transition in which the game is between
            // two states do not flash a third layout. The battle panel stays longer: the
            // game stops polling it while its buttons animate between menus.
            const bool writing = saveWriting();
            fieldHeldBySave = writing && fieldActive();
            Scene picked;
            perfTimed(1, [&] { picked = pickScene(frame < cursorShownUntil, view.width, screenImages); });
            // An application's own layout stays while it fades out: the game is still ending it
            // (and its picture is still what is on the screens) when only a general layout
            // would be chosen, until the overworld runs again or a moment has passed.
            {
                static Scene lastApp;
                static uint32_t lastAppFrame = 0;
                static const char* const appScenes[] = { "bag", "summary", "phone", "pc-box", "map", "pokedex",
                    "pokedex-list", "pokedex-page", "mart", "naming" };
                const bool isApp = std::any_of(std::begin(appScenes), std::end(appScenes),
                    [&](const char* n) { return !strcmp(picked.name, n); });
                const bool generic = !strcmp(picked.name, "top") || !strcmp(picked.name, "bottom") || !strcmp(picked.name, "spread");
                if (isApp)
                {
                    lastApp = picked;
                    lastAppFrame = frame;
                }
                else if (generic && lastAppFrame && frame - lastAppFrame < 90 && !fieldNow && !appBottomNow)
                    // (Not when another application has started, e.g. the party after a summary.)
                    picked = lastApp;
                else if (!generic)
                    lastAppFrame = 0;
            }
            // With the X menu open the game covers the bottom of the overworld with a strip
            // that points at the other screen. It means nothing here, so the layers it is
            // drawn on are left out and the overworld shows through.
            {
                int topUnit = topUnitOf();
                // The strip of the X menu on the top screen: 2D across the full width of its
                // lower rows, with the 3D world above it.
                const uint8_t* tf = screenImages[0].flags;
                auto is2D = [&](int x, int y) { uint8_t c = LayerClass(tf[y * 256 + x]); return c != LAYER_3D && c != LAYER_BACKDROP; };
                // Only while the overworld's code is what is running: a battle's opening has
                // the same shape on screen.
                const bool strip = tf && sectionActive("field") && !sectionActive("OVY_12") &&
                    is2D(0, 152) && is2D(255, 152) && is2D(0, 189) && is2D(255, 189) && LayerClass(tf[170 * 256 + 128]) == LAYER_OBJ &&
                    LayerClass(tf[60 * 256 + 128]) == LAYER_3D && LayerClass(tf[100 * 256 + 20]) == LAYER_3D;
                if (probeActive(probeStartMenu) && fieldActive()) menuReturn = 0;
                else if (strip && merged) menuReturn = 30;
                else if (menuReturn > 0) menuReturn--;
                bool menuOpen = merged && ((probeActive(probeStartMenu) && fieldActive()) || menuReturn > 0);
                rc_hide_layers[topUnit] = menuOpen ? 0x18 : 0;
                rc_hide_layers[1 - topUnit] = 0;
            }
            // The start menu, the layouts of the applications opened from it, and the plain
            // bottom screen (the party).
            auto menuScene = [](const char* n) {
                static const char* const names[] = { "start-menu", "bottom", "bag", "summary", "phone", "pc-box", "map",
                    "pokedex", "pokedex-list", "pokedex-page", "mart", "naming" };
                return std::any_of(std::begin(names), std::end(names), [&](const char* m) { return !strcmp(n, m); });
            };
            static std::string shownName, pendingName;
            static int pending = 0;
            if (writing && !shownName.empty())
                // The layout on screen stays until the save is written (see probeSaveWrite).
                pending = 0;
            else if (shownName.empty() || shownName == picked.name ||
                // Between menus, with both screens black nothing on screen changes, so the new
                // layout can take over at once and is in place when the picture fades in.
                // (Not around battles: there the timing of the layouts decides which buttons
                // are on screen.)
                (AutoFocus::Blank(top) && AutoFocus::Blank(bot) && !sectionActive("OVY_12") && !sectionActive("OVY_8") &&
                 (menuScene(picked.name) || menuScene(shownName.c_str()))))
            {
                scene = picked;
                shownName = picked.name;
                pending = 0;
            }
            else
            {
                if (pendingName != picked.name) pending = 0;
                pendingName = picked.name;
                int need = shownName == "bottom" && fieldNow ? 1 :
                    shownName == "battle-menu" && (pendingName == "top" || pendingName == "battle") ? 40 :
                    (shownName == "starter" || shownName == "intro") && pendingName == "top" ? 16 : 4;
                if (++pending >= need)
                {
                    scene = picked;
                    shownName = picked.name;
                    pending = 0;
                }
            }
        }
        if (opt.logSections)
        {
            static std::string lastScene;
            if (lastScene != scene.name)
            {
                lastScene = scene.name;
                fprintf(stderr, "[scene] frame %u: %s\n", frame, lastScene.c_str());
                g_sceneShots.insert(frame);
                g_sceneShots.insert(frame + 2);
                g_sceneShots.insert(frame + 20);
            }
        }

        if (audio && got > 0)
        {
            // Drop audio rather than let latency grow if the queue backs up.
            perfTimed(2, [&] {
                if (SDL_GetQueuedAudioSize(audio) < 48000 * 4 / 4)
                    SDL_QueueAudio(audio, audioBuf.data(), got * 4);
            });
        }
        if (opt.audioStats && got > 0)
        {
            for (int i = 0; i < got * 2; i++)
            {
                int v = abs(audioBuf[i]);
                if (v > AudioPeak) AudioPeak = v;
                AudioSumSq += (double)audioBuf[i] * audioBuf[i];
            }
            AudioSamples += (uint64_t)got * 2;
        }

        if (opt.countChanges && top)
        {
            static uint64_t lastTop = 0;
            uint64_t h = Fnv1a(top, 256 * 192 * 4);
            if (h != lastTop) TopChanges++;
            lastTop = h;
        }

        if (opt.posEvery && frame % opt.posEvery == 0)
        {
            // sFieldSysPtr -> location (map id) and playerAvatar -> mapObject -> current tile.
            auto rd = [&](uint32_t a) -> uint32_t {
                if (a < 0x02000000 || a >= 0x02400000) return 0;
                uint32_t v;
                memcpy(&v, nds->MainRAM + (a & nds->MainRAMMask), 4);
                return v;
            };
            uint32_t fs = rd(0x021D4158), loc = rd(fs + 0x20), obj = rd(rd(fs + 0x40) + 0x30);
            printf("pos %u map %u x %u z %u\n", frame, rd(loc), rd(obj + 0x64), rd(obj + 0x6C));
        }
        if (opt.hashEvery && frame % opt.hashEvery == 0)
        {
            uint64_t fb = Fnv1a(top, 256 * 192 * 4);
            fb = Fnv1a(bot, 256 * 192 * 4, fb);
            uint64_t ram = Fnv1a(nds->MainRAM, nds->MainRAMMask + 1);
            printf("frame %u fb %016llx ram %016llx pc9 %08x pc7 %08x\n", frame,
                (unsigned long long)fb, (unsigned long long)ram, nds->ARM9.R[15], nds->ARM7.R[15]);
        }

        // Overlay: touch cursor, status message, pause menu.
        bool overlayUsed = false;
        canvas.Resize(outW, outH);
        canvas.Clear();
        const SDL_Rect cursorRect = merged
            ? SDL_Rect { (int)view.originX, (int)view.originY, (int)(256 * view.unit), (int)(192 * view.unit) }
            : rects[1];
        if (frame < cursorShownUntil && cursorRect.w > 0 && !paused)
        {
            int cx = cursorRect.x + (int)(cursorX * cursorRect.w / 256);
            int cy = cursorRect.y + (int)(cursorY * cursorRect.h / 192);
            int arm = cursorRect.w / 40 + 3;
            int t = outH / 360 + 1;
            canvas.Fill(cx - arm - 1, cy - t - 1, arm * 2 + 3, t * 2 + 3, 0xFF000000);
            canvas.Fill(cx - t - 1, cy - arm - 1, t * 2 + 3, arm * 2 + 3, 0xFF000000);
            canvas.Fill(cx - arm, cy - t, arm * 2 + 1, t * 2 + 1, curPress ? 0xFFFFD75E : 0xFFFFFFFF);
            canvas.Fill(cx - t, cy - arm, t * 2 + 1, arm * 2 + 1, curPress ? 0xFFFFD75E : 0xFFFFFFFF);
            overlayUsed = true;
        }
        // The top right corner, below the on-screen R button when those controls are shown
        // (their top row ends at 12% of the height).
        int cornerY = touchPadUsed && touchPad.visible ? outH * 13 / 100 : 0;
        if (cfg.showFps && !opt.headless)
        {
            int sc = outH / 240 < 1 ? 1 : outH / 240;
            const std::string text = std::to_string(shownFps) + " FPS";
            const int tw = Canvas::TextWidth(text, sc) + 8 * sc;
            canvas.Fill(outW - tw, cornerY, tw, 13 * sc, 0xC0000000);
            canvas.Text(outW - tw + 4 * sc, cornerY + 3 * sc, text, sc, 0xFFFFFFFF);
            cornerY += 13 * sc;
            overlayUsed = true;
        }
        if (speed > 1)
        {
            int sc = outH / 240 < 1 ? 1 : outH / 240;
            canvas.Text(outW - Canvas::TextWidth(">>", sc) - 4 * sc, cornerY + 4 * sc, ">>", sc, 0xFFFFFFFF);
            overlayUsed = true;
        }
#if defined(__ANDROID__) && !defined(SSR_LAUNCHER)
        if (!updateSaid && UpdateCheck::g_done)
        {
            updateSaid = true;
            if (!UpdateCheck::g_newer.empty())
            {
                say("VERSION " + UpdateCheck::g_newer + " IS OUT - REBUILD THE APP ON YOUR PC");
                toastUntil = frame + 600;
            }
        }
#endif
        if (frame < toastUntil)
        {
            int sc = outH / 240 < 1 ? 1 : outH / 240;
            const int tw = Canvas::TextWidth(toast, sc) + 8 * sc;
            // With on-screen controls, centred below their top row (L, MENU and R end at
            // 12% of the height) instead of under the L button.
            const bool controls = touchPadUsed && touchPad.visible;
            const int tx = controls ? (outW - tw) / 2 : 0, ty = controls ? outH * 13 / 100 : 0;
            canvas.Fill(tx, ty, tw, 13 * sc, 0xC0000000);
            canvas.Text(tx + 4 * sc, ty + 3 * sc, toast, sc, 0xFFFFFFFF);
            overlayUsed = true;
        }
        if (menu.open)
        {
            menu.Draw(canvas, "POKEMON SOULSILVER", cfg.byLabel ? "A: SELECT   B: BACK" : "RIGHT: SELECT   BOTTOM: BACK");
            overlayUsed = true;
        }

        bool wantShot = !opt.shotDir.empty() &&
            (opt.shotAt.count(frame) || g_sceneShots.count(frame) || (opt.shotEvery && frame % opt.shotEvery == 0));
        if (wantShot && opt.shotLayers)
        {
            // BG0 red, BG1 green, BG2 blue, BG3 yellow, backdrop black, 3D magenta, sprites white.
            auto& r2d = static_cast<GPU2D::SoftRenderer&>(nds->GPU.GetRenderer2D());
            int topUnit = topUnitOf();
            std::vector<uint32_t> img(256 * 384);
            for (int scr = 0; scr < 2; scr++)
            {
                const u8* flags = r2d.LayerFlags[scr == 0 ? topUnit : 1 - topUnit];
                for (int i = 0; i < 256 * 192; i++)
                {
                    u8 f = flags[i];
                    uint32_t c = 0x000000;
                    switch (LayerClass(f))
                    {
                    case LAYER_OBJ: c = 0xFFFFFF; break;
                    case LAYER_3D: c = 0xFF00FF; break;
                    case LAYER_BG0: c = 0xFF0000; break;
                    case LAYER_BG1: c = 0x00C000; break;
                    case LAYER_BG2: c = 0x0000FF; break;
                    case LAYER_BG3: c = 0xFFFF00; break;
                    default: break;
                    }
                    img[scr * 256 * 192 + i] = c;
                }
            }
            for (int scr = 0; scr < 2; scr++)
            {
                const u8* flags = r2d.LayerFlags[scr == 0 ? topUnit : 1 - topUnit];
                int hist[256] = {};
                for (int i = 0; i < 256 * 192; i++) hist[flags[i]]++;
                fprintf(stderr, "[layers] frame %u %s:", frame, scr == 0 ? "top" : "bottom");
                for (int v = 0; v < 256; v++)
                    if (hist[v]) fprintf(stderr, " %02x=%d", v, hist[v]);
                fprintf(stderr, "\n");
            }
            char lname[64];
            snprintf(lname, sizeof(lname), "/layers_%06u.png", frame);
            WritePNG(opt.shotDir + lname, img.data(), 256, 384);
        }
        if (wantShot && top && bot)
        {
            char name[64];
            snprintf(name, sizeof(name), "/frame_%06u.png", frame);
            if (opt.composeW > 0 && opt.composeH > 0)
            {
                if (merged) { takeWideImage(); MergedCompose(scene, screenImages, opt.composeW, opt.composeH, shot, cfg.smooth, &wideImage); }
                else Compose(layout, opt.composeW, opt.composeH, top, bot, shot);
                BlendCanvas(canvas, shot);
                WritePNG(opt.shotDir + name, shot.data(), opt.composeW, opt.composeH);
            }
            else
            {
                shot.resize(256 * 384);
                memcpy(shot.data(), top, 256 * 192 * 4);
                memcpy(shot.data() + 256 * 192, bot, 256 * 192 * 4);
                WritePNG(opt.shotDir + name, shot.data(), 256, 384);
            }
        }

        perfMark(2);
        if (opt.headless && opt.benchCompose && merged && top && bot && opt.composeW)
        {
            takeWideImage();
            MergedCompose(scene, screenImages, opt.composeW, opt.composeH, mergedImage, cfg.smooth, &wideImage);
            perfMark(3);
        }

        if (!opt.headless && top && bot)
        {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            if (merged)
            {
                // The graphics card puts the pieces together (gpucompose.h). SSR_CPU_COMPOSE=1
                // uses the compositor of the headless tests instead, for comparing the two.
                takeWideImage();
                if (!cpuCompose)
                {
                    if (!gpuCompose) gpuCompose = new GpuCompositor(renderer);
                    gpuCompose->Draw(scene, screenImages, outW, outH, cfg.smooth, &wideImage);
                    perfMark(3);
                }
                else
                {
                    int cw = outW, ch = outH;
                    if (cfg.smooth && outH > 768)
                    {
                        cw = outW * 768 / outH;
                        ch = 768;
                    }
                    MergedCompose(scene, screenImages, cw, ch, mergedImage, cfg.smooth, &wideImage);
                    perfMark(3);
                    if (!mergedTex || mergedTexW != cw || mergedTexH != ch)
                    {
                        if (mergedTex) SDL_DestroyTexture(mergedTex);
                        mergedTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, cw, ch);
                        mergedTexW = cw;
                        mergedTexH = ch;
                    }
                    SDL_SetTextureScaleMode(mergedTex, cfg.smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
                    SDL_UpdateTexture(mergedTex, nullptr, mergedImage.data(), cw * 4);
                    SDL_RenderCopy(renderer, mergedTex, nullptr, nullptr);
                }
            }
            else
            {
                SDL_UpdateTexture(tex[0], nullptr, top, 256 * 4);
                SDL_UpdateTexture(tex[1], nullptr, bot, 256 * 4);
                int first = LayoutDrawFirst(layout);
                for (int n = 0; n < 2; n++)
                {
                    int i = n == 0 ? first : 1 - first;
                    if (rects[i].w > 0) SDL_RenderCopy(renderer, tex[i], nullptr, &rects[i]);
                }
            }
            if (overlayUsed)
            {
                if (!overlayTex || overlayTexW != outW || overlayTexH != outH)
                {
                    if (overlayTex) SDL_DestroyTexture(overlayTex);
                    overlayTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, outW, outH);
                    SDL_SetTextureBlendMode(overlayTex, SDL_BLENDMODE_BLEND);
                    overlayTexW = outW;
                    overlayTexH = outH;
                }
                if (canvas.Dirty())
                {
                    const SDL_Rect dr = { canvas.x0d, canvas.y0d, canvas.x1d - canvas.x0d, canvas.y1d - canvas.y0d };
                    SDL_UpdateTexture(overlayTex, &dr, canvas.px.data() + (size_t)dr.y * outW + dr.x, outW * 4);
                    SDL_RenderCopy(renderer, overlayTex, &dr, &dr);
                }
            }
            // In a window, screenshots are read back from the renderer: exactly what is shown.
            if (wantShot)
            {
                std::vector<uint32_t> back((size_t)outW * outH);
                if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, back.data(), outW * 4) == 0)
                {
                    char name[64];
                    snprintf(name, sizeof(name), "/window_%06u.png", frame);
                    WritePNG(opt.shotDir + name, back.data(), outW, outH);
                }
            }
            if (touchPadUsed && touchPad.visible)
            {
                // Lit by what the fingers hold (by label, as drawn).
                bool held[PAD_COUNT] = {};
                bool menuHeld = false;
                touchPad.Apply(held, menuHeld);
                touchPad.Draw(renderer, held, menuHeld);
            }
            perfTimed(3, [&] { SDL_RenderPresent(renderer); });
            perfMark(4);
            // Frames shown in the last whole second, for the FPS counter.
            presented++;
            const uint32_t ms = SDL_GetTicks();
            if (ms - fpsSince >= 1000)
            {
                shownFps = (int)(presented * 1000 / (ms - fpsSince));
                presented = 0;
                fpsSince = ms;
            }

            nextTick += (uint64_t)(frameTime * perfFreq);
            uint64_t now = SDL_GetPerformanceCounter();
            if (autoDetail && speed == 1 && !menu.open && frame > 600)
            {
                // What a frame's work took (not whether it started late: after one slow
                // frame, such as a loading hitch, the next ones catch up and would all count).
                const double work = (double)(now - frameWorkStart) / perfFreq;
                slowFrames += work > frameTime * 0.95;
                workSum += work;
                if (++pacedFrames == 240)
                {
                    const double share = workSum / pacedFrames / frameTime; // of a frame, on average
                    std::string change;
                    if (slowFrames > 48)
                    {
                        // More than a fifth of the frames too slow: first the in-between frames go
                        // (they draw the 3D a second time), then the widescreen 3D gets fewer lines.
                        if (raised) raiseUndone = true;
                        if (cfg.fps60)
                        {
                            cfg.fps60 = false;
                            nds->GPU.GPU3D.InterpolationEnabled = false;
                            change = "FOR SPEED: 60 FPS FRAMES OFF";
                        }
                        else if (detailIndex() > 1)
                        {
                            cfg.wideLines = detailLines[std::min(detailIndex() - 1, 3)];
                            change = "FOR SPEED: WORLD DETAIL " + std::to_string(cfg.wideLines);
                        }
                    }
                    else if (!slowFrames && share < 0.6 && !raiseUndone)
                    {
                        // Plenty of time to spare: back up, the other way round, to the defaults
                        // (720 lines, then the in-between frames). If that is too much, the step
                        // down undoes it and the setting stays there.
                        if (cfg.wideLines < 720)
                        {
                            cfg.wideLines = detailLines[std::min(detailIndex() + 1, 4)];
                            change = "WORLD DETAIL " + std::to_string(cfg.wideLines);
                        }
                        else if (!cfg.fps60)
                        {
                            cfg.fps60 = true;
                            nds->GPU.GPU3D.InterpolationEnabled = true;
                            change = "60 FPS FRAMES ON";
                        }
                        if (!change.empty()) raised = true;
                    }
                    if (!change.empty())
                    {
                        say(change);
                        fprintf(stderr, "[auto detail] %d of 240 frames slow, work %.0f%% of a frame: fps60 %d, wide lines %d\n",
                            slowFrames, share * 100, cfg.fps60, cfg.wideLines);
                    }
                    slowFrames = pacedFrames = 0;
                    workSum = 0;
                }
            }
            if (now < nextTick)
            {
                // Sleep for most of the wait and spin for the last part of a millisecond:
                // frames then start within a few microseconds of an even 16.7 ms apart.
                double left = (double)(nextTick - now) / perfFreq;
                if (left > 0.0008) std::this_thread::sleep_for(std::chrono::duration<double>(left - 0.0006));
                while (SDL_GetPerformanceCounter() < nextTick) {}
            }
            else if (now - nextTick > perfFreq / 4) nextTick = now;
            perfMark(5);
        }

        if (!opt.stateSave.empty() && scriptClock == opt.stateSaveAt && !holdScript)
        {
            if (!WriteSnapshot(*nds, opt.stateSave, scriptClock)) fprintf(stderr, "snapshot failed\n");
        }

        if (!opt.noSave && frame % 120 == 0) FlushSave(opt.save);
        if (opt.frames && scriptClock >= opt.frames) running = false;
        if (opt.stopAt && frame >= opt.stopAt)
        {
            if (!opt.stateSave.empty() && !WriteSnapshot(*nds, opt.stateSave, frame)) fprintf(stderr, "snapshot failed\n");
            running = false;
        }
    }

    Sampler::Stop();
    // The 3D render thread must be finished before anything it draws from is torn down.
    static_cast<melonDS::SoftRenderer&>(nds->GPU.GetRenderer3D()).SetThreaded(false, nds->GPU);
    melonDS::Wide.Owner = nullptr;
    if (!opt.headless)
    {
        if (!singleScreenRom) cfg.layout = LayoutName(layout.mode); // the automatic choice is not the player's setting
        cfg.focusMode = focus.mode;
        cfg.Save(opt.config);
    }
    if (nds->GPU.GPU3D.InterpolationEnabled)
        fprintf(stderr, "[60fps] %u in-between frames drawn in %u frames\n", nds->GPU.GPU3D.InterpolatedFrames, frame);
    if (opt.countChanges) fprintf(stderr, "[changes] top screen changed on %u of %u frames\n", TopChanges, frame);
    if (opt.audioStats && AudioSamples)
        fprintf(stderr, "[audio] %llu samples, peak %d, rms %.1f\n", (unsigned long long)AudioSamples, AudioPeak,
            sqrt(AudioSumSq / AudioSamples));
    if (!opt.noSave) FlushSave(opt.save);
    if (!opt.headless) SDL_Quit();
    if (nds->ARM9.RecompEnable) rc_print_stats();
    if (opt.lockstep)
    {
        if (TraceFailed) return 1;
        fprintf(stderr, "[lockstep] PASS: %u frames, every ARM9 instruction matched the interpreter\n", frame);
    }
    return 0;
}

#ifdef SSR_UWP
// A UWP program starts in CoreApplication, and SDL runs main (renamed SDL_main by
// SDL_main.h) inside it. SDL's own entry point for this, SDL_winrt_main_NonXAML.cpp, is
// C++/CX, which clang-cl cannot compile; this does the same.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    if (FAILED(RoInitialize(RO_INIT_MULTITHREADED))) return 1;
    return SDL_WinRTRunApp(SDL_main, nullptr);
}
#endif
