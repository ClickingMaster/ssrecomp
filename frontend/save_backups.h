#pragma once
// Browsing and restoring DS cartridge-save backups. No emulator state is changed here.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#if defined(__linux__)
#include <unistd.h>
#endif
namespace SaveBackups
{
namespace fs = std::filesystem;
struct Entry { std::string path, label; };
inline std::vector<unsigned char> Read(const std::string& path)
{
    std::error_code ec;
    const auto n = fs::file_size(fs::u8path(path), ec);
    if (ec || n == 0 || n > 32u * 1024 * 1024) return {};
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};
    std::vector<unsigned char> data((size_t)n);
    const bool ok = fread(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok ? data : std::vector<unsigned char>();
}
inline std::vector<Entry> List(const std::string& save)
{
    const fs::path p = fs::u8path(save), dir = p.parent_path() / "backups";
    const std::string stem = p.stem().u8string();
    std::vector<Entry> entries;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        const auto& f = it->path();
        const std::string n = f.filename().u8string();
        if (fs::is_symlink(it->symlink_status(ec)) || !it->is_regular_file(ec)) continue;
        if (n.size() != stem.size() + 20 || n.compare(0, stem.size()+1, stem+"-") || f.extension() != ".sav") continue;
        const std::string when = n.substr(stem.size()+1, 15);
        bool valid = when[8] == '-';
        for (size_t i=0; i<when.size(); i++) if (i != 8 && (when[i] < '0' || when[i] > '9')) valid = false;
        if (!valid) continue;
        entries.push_back({f.u8string(), when.substr(0,4)+"-"+when.substr(4,2)+"-"+when.substr(6,2)+" "+when.substr(9,2)+":"+when.substr(11,2)+":"+when.substr(13,2)});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a,const Entry& b){return a.path > b.path;});
    return entries;
}
// Keep the old destination until the new file has been written and closed successfully.
inline bool Replace(const std::string& target, const std::vector<unsigned char>& data, std::string& error)
{
    const std::string temp=target+".restore-tmp", old=target+".restore-old";
    std::error_code ec;
    if (fs::exists(fs::u8path(temp),ec) || fs::exists(fs::u8path(old),ec))
    { error="PREVIOUS RESTORE FILES EXIST - CHECK THE SAVE FOLDER"; return false; }
    FILE* f=fopen(temp.c_str(),"wbx");
    if (!f) { error="CANNOT WRITE IN THE SAVE FOLDER";return false; }
    bool ok=fwrite(data.data(),1,data.size(),f)==data.size() && fflush(f)==0;
#if defined(__linux__)
    if(ok) ok=fsync(fileno(f))==0;
#endif
    ok=fclose(f)==0 && ok;
    if(!ok) { remove(temp.c_str());error="CANNOT FINISH WRITING SAVE";return false; }
    const bool had=fs::exists(fs::u8path(target),ec);
    if(had) fs::rename(fs::u8path(target),fs::u8path(old),ec);
    if(ec) {remove(temp.c_str());error="CANNOT KEEP THE PREVIOUS SAVE";return false;}
    fs::rename(fs::u8path(temp),fs::u8path(target),ec);
    if(ec)
    {
        std::error_code undo;
        if(had) fs::rename(fs::u8path(old),fs::u8path(target),undo);
        error=undo ? "RESTORE FAILED - KEEP THE RESTORE-OLD FILE" : "RESTORE FAILED - PREVIOUS SAVE KEPT";
        remove(temp.c_str());return false;
    }
    if(had) fs::remove(fs::u8path(old),ec);
    return true;
}
inline bool Restore(const std::string& save, const std::string& backup, size_t expected,
                    std::vector<unsigned char>& restored, std::string& error)
{
    restored.clear();
    std::error_code linked;
    if(fs::is_symlink(fs::symlink_status(fs::u8path(save),linked)))
    {error="LINKED SAVE - RESTORE FROM ITS ORIGINAL FOLDER";return false;}
    const auto entries=List(save);
    if(std::none_of(entries.begin(),entries.end(),[&](const Entry& e){return e.path==backup;}))
    {error="BACKUP IS NO LONGER AVAILABLE";return false;}
    auto data=Read(backup);
    if(data.empty() || data.size()!=expected) {error="BACKUP SIZE DOES NOT MATCH THIS GAME";return false;}
    const auto current=Read(save);
    std::error_code ec;
    if(fs::exists(fs::u8path(save),ec) && current.empty()) {error="CANNOT READ THE CURRENT SAVE";return false;}
    // This safety copy is outside the rotating backups, so rotation cannot remove it.
    if(!current.empty() && !Replace(save+".before-restore.sav",current,error))return false;
    if(!Replace(save,data,error))return false;
    restored=std::move(data);return true;
}
}
