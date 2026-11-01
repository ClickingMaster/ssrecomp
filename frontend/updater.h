// Updates for the all-games program on a PC, a Mac, an iPhone (pc_launcher.cpp) and the Switch
// (switch_platform.cpp): the newest release of its own download on github.com/mamesj/pokemon-fulldex,
// checked against that release's SHA256SUMS.txt before anything is replaced. Windows, Linux (the
// unpacked download and the AppImage) and the Switch put the new version in place, keeping the
// old one; a Mac and an iPhone only say that there is one and open its page. (The Android app
// has its own, tools/android/allgames/java/.../Updates.java.)
#pragma once

#include <map>
#include <string>

namespace Updater
{
// The parts that do no downloading, for tests (tests/local_updater.cpp).
struct Release
{
    std::string asset, url, sumsUrl, page, tag;
    long long updated = 0; // the asset's upload time (seconds since 1970, UTC)
};
std::string Sha256Hex(const std::string& data);
// SHA256SUMS.txt's lines ("<sha256>  <file>", sha256sum's own form): file -> lowercase hash.
std::map<std::string, std::string> ParseSums(const std::string& text);
// Build times belong to the downloaded binary, independent of when GitHub uploaded it.
std::map<std::string, long long> ParseBuilds(const std::string& text);
bool IsNewerBuild(long long remoteBuild, long long currentBuild, const std::string& digest,
                  const std::string& installedDigest, long long uploaded, long long installedUpload);
std::string BuildLabel(long long build);
// "2026-11-01T16:07:00Z" -> seconds since 1970 (0 when it is not one).
long long ParseTime(const std::string& iso);
// The newest release (not a draft or a pre-release) of GitHub's list of releases with this
// asset, downloaded from an address starting with urlPrefix; with its SHA256SUMS.txt when the
// release has one. False when none has it.
bool FindRelease(const std::string& json, const std::string& asset, const std::string& urlPrefix, Release& out,
                 std::string& error);
// Puts the new download's files (fresh: its top folder, unpacked) in dir, each one already
// there moved into backup first (emptied before), and puts everything back when a step fails.
// The player's folders and files (roms, saves, settings, logs) are never touched.
bool SwapIn(const std::string& dir, const std::string& fresh, const std::string& backup, std::string& error);

// The check and the install, each on a thread of its own; Get says how it is going.
enum class State { Idle, Checking, UpToDate, Available, Downloading, Installed, Failed };
struct Status
{
    State state = State::Idle;
    std::string text;  // for the settings page's CHECK FOR UPDATES (the list's font: capitals)
    std::string page;  // the release's page
    bool canInstall = false;
    long long currentBuild = 0, availableBuild = 0;
    unsigned serial = 0; // changes with every new state
};
// programDir: where the program is; dataDir: where its settings are (the update folder, with
// the old version and what is installed, goes there); program: the program's own file, where
// its name is not always the same (the Switch's .nro).
void Init(const std::string& programDir, const std::string& dataDir, const std::string& program = "");
// quiet: the list's own check at launch (a failed connection is not shown as an error).
void Check(bool quiet);
// CHECK FOR UPDATES chosen: checks, installs what was found, or gives the page to open ("": none).
std::string Choose();
Status Get();
// The launch popup (the list's font: capitals). canInstall: INSTALL, otherwise OPEN DOWNLOAD.
inline const char* PopupTitle() { return "NEW VERSION"; }
inline const char* PopupInstall() { return "INSTALL"; }
inline const char* PopupOpen() { return "OPEN DOWNLOAD"; }
inline const char* PopupLater() { return "LATER"; }
inline std::string PopupNote(bool canInstall)
{
    const Status s=Get();
    const std::string versions="CURRENT "+BuildLabel(s.currentBuild)+". AVAILABLE "+BuildLabel(s.availableBuild)+". ";
    return versions + (canInstall
        ? "DOWNLOAD VERIFIED. YOUR ROMS, SAVES AND SETTINGS STAY. PREVIOUS PROGRAM KEPT."
        : "OPEN THE RELEASE PAGE FOR DETAILS AND THE DOWNLOAD.");
}
// After an install: the new program, started when the list closes (Restart).
bool RestartPending();
void Restart();
// The list closed. Switch work is synchronous, so there is no updater worker to wait for.
void Stop();
}
