#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "launcher.h"
#include <filesystem>
#include <cassert>
#include <fstream>
#include <iostream>
std::string PrepareLaunch(const Launcher::Game& game)
{
    FILE* rom = fopen(game.rom.c_str(), "rb");
    if (!rom) return "ROM IS MISSING OR CANNOT BE READ";
    fclose(rom);
    if (game.saveName.empty()) return "THIS GAME HAS NO SAVE FILE NAME";
    std::error_code ec;
    std::filesystem::create_directories(game.folder, ec);
    if (ec) return "CANNOT CREATE THE SAVES FOLDER";
    const std::string save = game.folder + "/" + game.saveName;
    if (std::filesystem::exists(save, ec))
    {
        const std::string backup = save + ".prelaunch.bak";
        std::filesystem::copy_file(save, backup + ".tmp", std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec) std::filesystem::rename(backup + ".tmp", backup, ec);
        if (ec) return "CANNOT BACK UP SAVE: CHECK FREE SPACE AND ACCESS";
    }
    const std::string probe = save + ".write-check";
    FILE* f = fopen(probe.c_str(), "wbx");
    if (!f) return "SAVES FOLDER IS NOT WRITABLE";
    const bool ok = fclose(f) == 0;
    remove(probe.c_str());
    return ok ? "" : "CANNOT WRITE IN THE SAVES FOLDER";
}


int main() {
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / "fulldex-synthetic-tests";
    fs::create_directories(root);
    std::vector<unsigned char> rom(0x640);
    memcpy(rom.data()+12, "IPGE", 4);
    rom[0x69]=2;
    auto write=[&](const char* name, size_t n) { std::ofstream out(root/name, std::ios::binary); out.write((char*)rom.data(), n); };
    write("valid.nds",rom.size());
    Launcher::Game game;
    assert(Launcher::ReadGame((root/"valid.nds").string(), (root/"saves").string(), game));
    assert(game.hasIcon && game.known);
    write("truncated.nds",0x60);
    assert(!Launcher::ReadGame((root/"truncated.nds").string(),root.string(),game));
    rom[0x68]=rom[0x69]=rom[0x6a]=rom[0x6b]=255;
    write("bad-banner.nds",rom.size());
    assert(Launcher::ReadGame((root/"bad-banner.nds").string(),root.string(),game));
    assert(!game.hasIcon);
    rom[12]=0;
    write("bad-code.nds",rom.size());
    assert(!Launcher::ReadGame((root/"bad-code.nds").string(),root.string(),game));
    assert(!Launcher::ReadGame(root.string(),root.string(),game));
    std::vector<unsigned char> image;
    std::vector<uint32_t> art;
    assert(!Launcher::LoadArt(nullptr, 0, art));
    assert(Launcher::ReadFile("tests/fixtures/tiny.png",image));
    assert(Launcher::LoadArt(image.data(),image.size(),art));
    assert(art.size()==Launcher::Art*Launcher::Art);
    assert(Launcher::ReadFile("tests/fixtures/oversize.png",image));
    assert(!Launcher::LoadArt(image.data(),image.size(),art));
    for (auto size : {std::pair<int,int>{576,1280},{720,1600},{1280,720}}) {
        Canvas c; c.Resize(size.first,size.second);
        const auto layout=Launcher::Layout(c);
        assert(layout.gx>=0 && layout.top<layout.bottom);
        if (size.second>size.first) assert(layout.cols==2);
        std::vector<Launcher::Game> games(1); games[0].name="TEST";
        int scroll=0; Launcher::Draw(c,games,0,0,scroll,"");
    }

    const std::string config=(root/"settings.cfg").string();
    { std::ofstream out(config); out << "volume=37\nlist_colour=blue\n"; }
    Launcher::SettingsPage settings; settings.Open(config);
    settings.items = Launcher::SettingsList();
    for (size_t i=0; i<settings.items.size(); ++i) if (settings.items[i].key && std::string(settings.items[i].key)=="smooth") {
        settings.selected=(int)i; settings.Change(1); break;
    }
    assert(Launcher::ConfigValue(config,"volume")=="37");
    assert(Launcher::ConfigValue(config,"list_colour")=="blue");
    assert(!Launcher::ConfigValue(config,"smooth").empty());
    Canvas screenshot; screenshot.Resize(576,1280);
    std::vector<Launcher::Game> display(8);
    for (int i=0;i<8;i++) {
        display[i].name=std::string("POKEMON ")+Launcher::Known(i%2 ? "IREO":"IPGE")->name;
        display[i].known=true; display[i].hasIcon=true;
        for (auto& pixel:display[i].icon) pixel= i%2 ? 0xFFCC3344 : 0xFF3388CC;
    }
    int offset=0; Launcher::Draw(screenshot,display,0,0,offset,"");
    std::ofstream picture("tests/portrait.ppm",std::ios::binary);
    picture << "P6\n576 1280\n255\n";
    for (uint32_t pixel:screenshot.px) { const char rgb[]={(char)(pixel>>16),(char)(pixel>>8),(char)pixel}; picture.write(rgb,3); }
    picture.close();
    assert(Launcher::ReadGame((root/"valid.nds").string(),(root/"saves").string(),game));
    assert(PrepareLaunch(game).empty());
    std::ofstream(root/"saves"/"soulsilver.sav") << "SAVE-SENTINEL";
    assert(PrepareLaunch(game).empty());
    std::ifstream backup(root/"saves"/"soulsilver.sav.prelaunch.bak"); std::string saved; backup >> saved;
    assert(saved=="SAVE-SENTINEL");
    fs::permissions(root/"saves",fs::perms::owner_read|fs::perms::owner_exec);
    assert(!PrepareLaunch(game).empty());
    fs::permissions(root/"saves",fs::perms::owner_all);
    game.rom=(root/"missing.nds").string(); assert(!PrepareLaunch(game).empty());
    // Only this test's synthetic files are removed.
    fs::remove_all(root);
    std::cout << "PASS: ROM bounds, image limits, portrait/landscape layout, launch errors and save backup\n";
}
