#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "launcher.h"
#include <cassert>
#include <fstream>
int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string path = std::string(argv[1]) + "/settings.cfg";
    { std::ofstream f(path); f << "wide_lines=1080\nbusy_detail=1\nlist_lang=ja\n"; }
    Launcher::SettingsPage page;
    page.Open(path, true);
    page.Enter(2);
    for (size_t i = 0; i < page.items.size(); ++i)
        if (page.items[i].key && !strcmp(page.items[i].key, "wide_lines")) page.selected = (int)i;
    assert(!strcmp(page.items[page.selected].key, "wide_lines"));
    page.Change(-1);
    assert(Launcher::ConfigValue(path, "wide_lines") == "720");
    assert(Launcher::ConfigValue(path, "busy_detail") == "0");
    assert(Launcher::ConfigValue(path, "list_lang") == "ja");
    // It stays manual on a second change, including an existing settings file.
    page.Change(-1);
    assert(Launcher::ConfigValue(path, "wide_lines") == "540");
    assert(Launcher::ConfigValue(path, "busy_detail") == "0");
    puts("Launcher manual resolution: value saved, automatic lowering disabled");
}
