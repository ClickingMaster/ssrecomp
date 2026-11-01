// The updater (frontend/updater.cpp) without the internet: checksums, GitHub's list of releases,
// SHA256SUMS.txt, and a whole check and install of the Linux download into a folder with a save
// in it, from releases made up here. On Linux:
//   g++ -std=c++17 -O1 -pthread -Ifrontend tests/local_updater.cpp frontend/updater.cpp -o /tmp/local_updater && /tmp/local_updater
#include "updater.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

// The internet, made up: address -> what it gives.
static std::map<std::string, std::string> g_web;
namespace Achievements
{
bool HttpPost(const std::string& url, const std::string&, const std::string&, const std::string&, int& status,
              std::string& body, std::string& error)
{
    auto it = g_web.find(url);
    if (it == g_web.end()) { status = 404; body.clear(); return true; }
    status = 200;
    body = it->second;
    (void)error;
    return true;
}
bool HttpDownload(const std::string& url, const std::string& ua, int& status, std::string& body, std::string& error)
{
    return HttpPost(url, "", "", ua, status, body, error);
}
}

static std::string Read(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}
static void Write(const fs::path& p, const std::string& data)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << data;
}

// What the check or install started by act came to.
template <typename F>
static Updater::Status After(F act)
{
    const unsigned before = Updater::Get().serial;
    act();
    for (int i = 0; i < 500; i++)
    {
        const Updater::Status s = Updater::Get();
        if (s.serial != before && s.state != Updater::State::Checking && s.state != Updater::State::Downloading) return s;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(!"the updater did not finish");
    return {};
}

static std::string ReleasesJson(const std::string& tarUrl, const std::string& sumsUrl, const std::string& time)
{
    return R"([
  {"tag_name":"pc-v3","html_url":"https://fake/pc-v3","draft":true,"prerelease":false,
   "assets":[{"name":"pokemon-ds-wide-pc-linux-x86_64.tar.gz","size":5,"updated_at":"2099-01-01T00:00:00Z",
              "browser_download_url":"http://fake/download/draft.tar.gz"}]},
  {"tag_name":"pc-v2","html_url":"https://fake/pc-v2","draft":false,"prerelease":false,"body":"Notes \"quoted\" \u00e9 \ud83d\ude00",
   "assets":[{"name":"pokemon-ds-wide-pc-linux-x86_64.tar.gz","size":5,"updated_at":")" + time + R"(",
              "browser_download_url":")" + tarUrl + R"("},
             {"name":"SHA256SUMS.txt","size":5,"updated_at":")" + time + R"(","browser_download_url":")" + sumsUrl + R"("}]},
  {"tag_name":"pc-v1","html_url":"https://fake/pc-v1","draft":false,"prerelease":false,
   "assets":[{"name":"pokemon-ds-wide-pc-linux-x86_64.tar.gz","size":5,"updated_at":"2020-01-01T00:00:00Z",
              "browser_download_url":"http://fake/download/old.tar.gz"},
             {"name":"pokemon-ds-wide-pc-linux-x86_64.tar.gz","size":5,"updated_at":"2099-01-01T00:00:00Z",
              "browser_download_url":"https://elsewhere.example/evil.tar.gz"}]}
])";
}

int main()
{
    // Binary build IDs prevent quick hotfixes being hidden, and the original upload
    // being mistaken for a different version of the exact same binary.
    const auto builds=Updater::ParseBuilds("# fulldex-build test.zip 100\n# fulldex-build bad.zip -1\n# fulldex-build overflow.zip 9999999999999999999999999\n");
    assert(builds.size()==1 && builds.at("test.zip")==100);
    assert(Updater::IsNewerBuild(101,100,"new","old",101,100));
    assert(!Updater::IsNewerBuild(100,100,"same","",150,0));
    assert(!Updater::IsNewerBuild(99,100,"older","newer",200,100));
    assert(Updater::IsNewerBuild(0,100,"new","",101,0));
    assert(!Updater::IsNewerBuild(0,100,"same","same",150,100));
    assert(Updater::BuildLabel(0)=="UNKNOWN");
    // Checksums (FIPS 180-2's examples).
    assert(Updater::Sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    assert(Updater::Sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(Updater::Sha256Hex(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    // SHA256SUMS.txt, as sha256sum writes it (and with its binary mark, Windows line ends, junk).
    const std::string h1(64, 'A'), h2(64, 'b');
    auto sums = Updater::ParseSums(h1 + "  pokemon-ds-wide-pc-windows-x86_64.zip\r\n" + h2 + " *pokemon-ds-wide-android.apk\nnot a line\n" +
                                   std::string(63, 'c') + "  short.zip\n");
    assert(sums.size() == 2);
    assert(sums["pokemon-ds-wide-pc-windows-x86_64.zip"] == std::string(64, 'a'));
    assert(sums["pokemon-ds-wide-android.apk"] == h2);

    assert(Updater::ParseTime("1970-01-02T00:00:00Z") == 86400);
    assert(Updater::ParseTime("2026-11-01T16:07:00Z") == 1793549220LL);
    assert(Updater::ParseTime("yesterday") == 0);

    // The list: the newest real release with the asset from the right place, with its sums.
    {
        Updater::Release r;
        std::string error;
        const std::string json = ReleasesJson("http://fake/download/new.tar.gz", "http://fake/download/SHA256SUMS.txt", "2026-11-01T16:07:00Z");
        assert(Updater::FindRelease(json, "pokemon-ds-wide-pc-linux-x86_64.tar.gz", "http://fake/", r, error));
        assert(r.tag == "pc-v2" && r.url == "http://fake/download/new.tar.gz" && r.sumsUrl == "http://fake/download/SHA256SUMS.txt");
        assert(r.updated == 1793549220LL && r.page == "https://fake/pc-v2");
        assert(!Updater::FindRelease(json, "pokemon-ds-wide-ios.ipa", "http://fake/", r, error));
        assert(!Updater::FindRelease("{\"message\":\"rate limited\"}", "x", "http://fake/", r, error));
        assert(!Updater::FindRelease("[{\"assets\":[", "x", "http://fake/", r, error));
    }

    const fs::path root = fs::temp_directory_path() / "fulldex-updater-test";
    fs::remove_all(root);

    // SwapIn: the program's files replaced and kept, the player's left alone.
    {
        const fs::path dir = root / "swap/pokemon-ds-wide", fresh = root / "swap/new/pokemon-ds-wide", backup = root / "swap/previous";
        Write(dir / "pokemon-ds-wide", "old program");
        Write(dir / "data/funcs/IREO.txt", "old table");
        Write(dir / "roms/black2.nds", "rom");
        Write(dir / "saves/black2.sav", "save");
        Write(dir / "settings.cfg", "volume=5\n");
        Write(fresh / "pokemon-ds-wide", "new program");
        Write(fresh / "data/funcs/IREO.txt", "new table");
        Write(fresh / "README.txt", "new notes");
        fs::create_directories(fresh / "roms");
        Write(fresh / "saves/black2.sav", "a save in a download must never land");
        std::string error;
        assert(Updater::SwapIn(dir.u8string(), fresh.u8string(), backup.u8string(), error));
        assert(Read(dir / "pokemon-ds-wide") == "new program" && Read(dir / "data/funcs/IREO.txt") == "new table");
        assert(Read(dir / "README.txt") == "new notes");
        assert(Read(backup / "pokemon-ds-wide") == "old program" && Read(backup / "data/funcs/IREO.txt") == "old table");
        assert(Read(dir / "roms/black2.nds") == "rom" && Read(dir / "saves/black2.sav") == "save");
        assert(Read(dir / "settings.cfg") == "volume=5\n");
        // (A backup that cannot be made: nothing changed.)
        Write(root / "swap/blocker", "a file where the backup folder would go");
        Write(fresh / "pokemon-ds-wide", "newer program");
        assert(!Updater::SwapIn(dir.u8string(), fresh.u8string(), (root / "swap/blocker/previous").u8string(), error));
        assert(Read(dir / "pokemon-ds-wide") == "new program");
    }

    // The whole thing, as the Linux download: check, install, then up to date; a damaged download.
    {
        const fs::path prog = root / "install/pokemon-ds-wide";
        Write(prog / "pokemon-ds-wide", "version 1");
        Write(prog / "data/funcs/IREO.txt", "table 1");
        Write(prog / "saves/white2.sav", "my save");
        Write(prog / "roms/white2.nds", "my rom");
        const fs::path pack = root / "pack/pokemon-ds-wide";
        Write(pack / "pokemon-ds-wide", "version 2");
        Write(pack / "data/funcs/IREO.txt", "table 2");
        fs::create_directories(pack / "roms");
        const fs::path tarball = root / "pack.tar.gz";
        const std::string cmd = "tar -czf '" + tarball.string() + "' -C '" + (root / "pack").string() + "' pokemon-ds-wide";
        assert(system(cmd.c_str()) == 0);
        const std::string tar = Read(tarball);
        const std::string asset = "pokemon-ds-wide-pc-linux-x86_64.tar.gz";

        setenv("FULLDEX_UPDATE_URL", "http://fake/releases.json", 1);
        unsetenv("APPIMAGE");
        g_web["http://fake/releases.json"] = ReleasesJson("http://fake/download/new.tar.gz", "http://fake/download/SHA256SUMS.txt", "2026-11-01T16:07:00Z");
        g_web["http://fake/download/new.tar.gz"] = tar;
        g_web["http://fake/download/SHA256SUMS.txt"] = Updater::Sha256Hex(tar) + "  " + asset + "\n";

        Updater::Init(prog.u8string(), prog.u8string());
        Updater::Status s = After([] { assert(Updater::Choose().empty()); }); // (the first choice checks)
        assert(s.state == Updater::State::Available && s.canInstall && s.text == "NEW VERSION: INSTALL");
        s = After([] { assert(Updater::Choose().empty()); }); // (the second installs)
        assert(s.state == Updater::State::Installed && Updater::RestartPending());
        assert(Read(prog / "pokemon-ds-wide") == "version 2" && Read(prog / "data/funcs/IREO.txt") == "table 2");
        assert(Read(prog / "update/previous/pokemon-ds-wide") == "version 1");
        assert(Read(prog / "update/previous/data/funcs/IREO.txt") == "table 1");
        assert(Read(prog / "saves/white2.sav") == "my save" && Read(prog / "roms/white2.nds") == "my rom");
        assert(Read(prog / "update/installed.txt").rfind(Updater::Sha256Hex(tar) + " ", 0) == 0);
        assert(!fs::exists(prog / "update/new") && !fs::exists(prog / ("update/" + asset)));

        // The same release again: up to date.
        Updater::Init(prog.u8string(), prog.u8string());
        s = After([] { Updater::Check(false); });
        assert(s.state == Updater::State::UpToDate);

        // A newer release whose download does not match its checksum: nothing changes.
        g_web["http://fake/releases.json"] = ReleasesJson("http://fake/download/v3.tar.gz", "http://fake/download/SHA256SUMS.txt", "2026-12-01T00:00:00Z");
        g_web["http://fake/download/v3.tar.gz"] = tar + "tampered";
        g_web["http://fake/download/SHA256SUMS.txt"] = std::string(64, 'f') + "  " + asset + "\n";
        s = After([] { Updater::Check(false); });
        assert(s.state == Updater::State::Available && s.canInstall);
        s = After([] { Updater::Choose(); });
        assert(s.state == Updater::State::Failed && s.text == "DOWNLOAD DAMAGED: NOTHING CHANGED");
        assert(Read(prog / "pokemon-ds-wide") == "version 2" && Read(prog / "saves/white2.sav") == "my save");

        // A release without SHA256SUMS.txt: said, not installed.
        g_web["http://fake/releases.json"] = ReleasesJson("http://fake/download/v3.tar.gz", "http://fake/download/missing.txt", "2026-12-01T00:00:00Z");
        s = After([] { Updater::Check(false); });
        assert(s.state == Updater::State::Available && !s.canInstall && s.text == "NEW VERSION: OPEN PAGE");
        assert(Updater::Choose() == "https://fake/pc-v2");

        // Not an unpacked download (a build folder): said, never installed there.
        const fs::path build = root / "build";
        Write(build / "ssrecomp", "a build");
        Updater::Init(build.u8string(), build.u8string());
        g_web["http://fake/download/SHA256SUMS.txt"] = Updater::Sha256Hex(tar) + "  " + asset + "\n";
        g_web["http://fake/releases.json"] = ReleasesJson("http://fake/download/new.tar.gz", "http://fake/download/SHA256SUMS.txt", "2026-11-01T16:07:00Z");
        s = After([] { Updater::Check(false); });
        assert(s.state == Updater::State::Available && !s.canInstall);
    }
    fs::remove_all(root);
    std::cout << "updater: all checks passed\n";
    return 0;
}
