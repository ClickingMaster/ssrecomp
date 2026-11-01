#include <cstdio>
#include <vector>
#include <string>
#include <filesystem>
#include <unistd.h>
#include <fcntl.h>
#include <cassert>
#include <fstream>
namespace Host { bool SaveIsDirty=false; std::vector<unsigned char> SaveData; }
static bool FlushSave(const std::string& path)
{
    if (!Host::SaveIsDirty) return true;
    if (path.empty()) return false;
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) { fprintf(stderr, "cannot open save temporary file %s\n", tmp.c_str()); return false; }
    bool ok = fwrite(Host::SaveData.data(), 1, Host::SaveData.size(), f) == Host::SaveData.size();
    // A completed stdio write alone does not ask Linux to persist the data to storage.
#if defined(__linux__)
    if (ok) ok = fflush(f) == 0;
    if (ok) ok = fsync(fileno(f)) == 0;
#endif
    ok = fclose(f) == 0 && ok;
    if (!ok) { fprintf(stderr, "cannot finish writing save %s\n", tmp.c_str()); return false; }
#ifdef __SWITCH__
    // The Switch's file system will not rename onto an existing file, so the old save steps
    // aside first, and comes back if the new one cannot take its place.
    const std::string old = path + ".old";
    remove(old.c_str());
    const bool hadOld = rename(path.c_str(), old.c_str()) == 0;
    if (rename(tmp.c_str(), path.c_str()) != 0)
    {
        if (hadOld) rename(old.c_str(), path.c_str());
        fprintf(stderr, "cannot write save %s\n", path.c_str());
        return false;
    }
    if (hadOld) remove(old.c_str());
#else
    // std::filesystem::rename replaces an existing file on Windows too (plain rename()
    // fails there when the target exists, which kept every save after the first).
    std::error_code err;
    std::filesystem::rename(tmp, path, err);
    if (err)
    {
        fprintf(stderr, "cannot write save %s: %s\n", path.c_str(), err.message().c_str());
        return false;
    }
#endif
#if defined(__linux__)
    // Persist the directory entry after replacing the file. Leave the save dirty on error
    // so the next periodic flush retries instead of silently accepting an uncertain write.
    const std::string parent = std::filesystem::path(path).parent_path().empty()
        ? "." : std::filesystem::path(path).parent_path().string();
    const int dir = open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    bool synced = dir >= 0;
    if (synced) { synced = fsync(dir) == 0; if (close(dir) != 0) synced = false; }
    if (!synced) { fprintf(stderr, "cannot sync save directory %s\n", parent.c_str()); return false; }
#endif
    fprintf(stderr, "[save] persisted %zu bytes to %s\n", Host::SaveData.size(), path.c_str());
    fflush(stderr);
    Host::SaveIsDirty = false;
    return true;
}

int main() {
 char folder[]="/tmp/fulldex-save-test-XXXXXX"; assert(mkdtemp(folder));
 std::string path=std::string(folder)+"/test.sav";
 for (int i=1;i<=3;i++) {
  Host::SaveData.assign(524288,(unsigned char)i); Host::SaveIsDirty=true;
  assert(FlushSave(path)); assert(!Host::SaveIsDirty);
  std::ifstream in(path,std::ios::binary); std::vector<unsigned char> actual((std::istreambuf_iterator<char>(in)),{});
  assert(actual==Host::SaveData);
 }
 Host::SaveData.assign(524288,9); Host::SaveIsDirty=true;
 std::filesystem::create_directory(path+".tmp");
 assert(!FlushSave(path)); assert(Host::SaveIsDirty);
 std::ifstream in(path,std::ios::binary); assert(in.get()==3); in.close();
 std::filesystem::remove(path+".tmp"); assert(FlushSave(path));
 assert(!std::filesystem::exists(path+".tmp"));
 std::filesystem::remove_all(folder);
 puts("PASS: repeated save replacement, failure preserves existing save, dirty retry, full byte readback");
}
