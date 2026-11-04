// Host implementation of melonDS's Platform interface for the ssrecomp frontend.
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "Platform.h"
#include "SPI_Firmware.h"
#include "SsrThreads.h"
#include "host.h"
#ifdef __ANDROID__
#include <dlfcn.h>
#endif

namespace Host { std::string FirmwarePath; }

namespace melonDS::Platform
{

void SignalStop(StopReason reason, void*)
{
    fprintf(stderr, "[core] stop requested (reason %d)\n", (int)reason);
    Host::StopRequested = true;
}

std::string GetLocalFilePath(const std::string& filename) { return filename; }

static const char* ModeString(FileMode mode, bool exists)
{
    if (mode & FileMode::Append) return (mode & FileMode::Text) ? "a" : "ab";
    if (!(mode & FileMode::Write)) return (mode & FileMode::Text) ? "r" : "rb";
    if (mode & FileMode::Preserve)
    {
        if (exists) return (mode & FileMode::Text) ? "r+" : "rb+";
        if (mode & FileMode::NoCreate) return nullptr;
        return (mode & FileMode::Text) ? "w+" : "wb+";
    }
    if (mode & FileMode::Read) return (mode & FileMode::Text) ? "w+" : "wb+";
    return (mode & FileMode::Text) ? "w" : "wb";
}

bool FileExists(const std::string& name)
{
    FILE* f = fopen(name.c_str(), "rb");
    if (!f) return false;
    fclose(f);
    return true;
}
bool LocalFileExists(const std::string& name) { return FileExists(name); }

FileHandle* OpenFile(const std::string& path, FileMode mode)
{
    const char* m = ModeString(mode, FileExists(path));
    if (!m) return nullptr;
    return (FileHandle*)fopen(path.c_str(), m);
}
FileHandle* OpenLocalFile(const std::string& path, FileMode mode) { return OpenFile(path, mode); }

bool CheckFileWritable(const std::string& filepath)
{
    FILE* f = fopen(filepath.c_str(), "ab");
    if (!f) return false;
    fclose(f);
    return true;
}
bool CheckLocalFileWritable(const std::string& filepath) { return CheckFileWritable(filepath); }

bool CloseFile(FileHandle* file) { return fclose((FILE*)file) == 0; }
bool IsEndOfFile(FileHandle* file) { return feof((FILE*)file) != 0; }
bool FileReadLine(char* str, int count, FileHandle* file) { return fgets(str, count, (FILE*)file) != nullptr; }
u64 FilePosition(FileHandle* file) { return (u64)ftell((FILE*)file); }
bool FileSeek(FileHandle* file, s64 offset, FileSeekOrigin origin)
{
    int whence = origin == FileSeekOrigin::Start ? SEEK_SET : origin == FileSeekOrigin::Current ? SEEK_CUR : SEEK_END;
    return fseek((FILE*)file, (long)offset, whence) == 0;
}
void FileRewind(FileHandle* file) { rewind((FILE*)file); }
u64 FileRead(void* data, u64 size, u64 count, FileHandle* file) { return fread(data, size, count, (FILE*)file); }
bool FileFlush(FileHandle* file) { return fflush((FILE*)file) == 0; }
u64 FileWrite(const void* data, u64 size, u64 count, FileHandle* file) { return fwrite(data, size, count, (FILE*)file); }
u64 FileWriteFormatted(FileHandle* file, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int ret = vfprintf((FILE*)file, fmt, args);
    va_end(args);
    return ret < 0 ? 0 : (u64)ret;
}
u64 FileLength(FileHandle* file)
{
    FILE* f = (FILE*)file;
    long pos = ftell(f);
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, pos, SEEK_SET);
    return (u64)len;
}

void Log(LogLevel level, const char* fmt, ...)
{
    if ((int)level < Host::LogLevel) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

struct Thread { std::thread t; };
Thread* Thread_Create(std::function<void()> func)
{
    return new Thread { std::thread([func = std::move(func)] { SsrHelperThreadStart(0); func(); }) };
}
void Thread_Free(Thread* thread)
{
    if (thread->t.joinable()) thread->t.join();
    delete thread;
}
void Thread_Wait(Thread* thread)
{
    if (thread->t.joinable()) thread->t.join();
}

struct Semaphore
{
    std::mutex m;
    std::condition_variable cv;
    int count = 0;
};
Semaphore* Semaphore_Create() { return new Semaphore; }
void Semaphore_Free(Semaphore* sema) { delete sema; }
void Semaphore_Reset(Semaphore* sema)
{
    std::lock_guard<std::mutex> lk(sema->m);
    sema->count = 0;
}
void Semaphore_Wait(Semaphore* sema)
{
    std::unique_lock<std::mutex> lk(sema->m);
    sema->cv.wait(lk, [&] { return sema->count > 0; });
    sema->count--;
}
bool Semaphore_TryWait(Semaphore* sema, int timeout_ms)
{
    std::unique_lock<std::mutex> lk(sema->m);
    if (!sema->cv.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&] { return sema->count > 0; }))
        return false;
    sema->count--;
    return true;
}
void Semaphore_Post(Semaphore* sema, int count)
{
    {
        std::lock_guard<std::mutex> lk(sema->m);
        sema->count += count;
    }
    sema->cv.notify_all();
}

struct Mutex { std::mutex m; };
Mutex* Mutex_Create() { return new Mutex; }
void Mutex_Free(Mutex* mutex) { delete mutex; }
void Mutex_Lock(Mutex* mutex) { mutex->m.lock(); }
void Mutex_Unlock(Mutex* mutex) { mutex->m.unlock(); }
bool Mutex_TryLock(Mutex* mutex) { return mutex->m.try_lock(); }

void Sleep(u64 usecs) { std::this_thread::sleep_for(std::chrono::microseconds(usecs)); }
u64 GetUSCount()
{
    using namespace std::chrono;
    return (u64)duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}
u64 GetMSCount() { return GetUSCount() / 1000; }

void WriteNDSSave(const u8* savedata, u32 savelen, u32, u32, void*)
{
    Host::SaveDirty(savedata, savelen);
}
void WriteGBASave(const u8*, u32, u32, u32, void*) {}
// The firmware the game changed (the Nintendo Wi-Fi Connection settings, DNS among them): kept
// in Host::FirmwarePath, written whole to a .tmp beside it and then put in its place.
void WriteFirmware(const Firmware& fw, u32, u32, void*)
{
    if (Host::FirmwarePath.empty() || !fw.Buffer() || !fw.Length()) return;
    const std::string tmp = Host::FirmwarePath + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return;
    const bool ok = fwrite(fw.Buffer(), 1, fw.Length(), f) == fw.Length();
    if (fclose(f) != 0 || !ok) { remove(tmp.c_str()); return; }
    if (rename(tmp.c_str(), Host::FirmwarePath.c_str()) != 0)
    {
        remove(Host::FirmwarePath.c_str()); // (Windows: rename does not replace a file)
        rename(tmp.c_str(), Host::FirmwarePath.c_str());
    }
}
void WriteDateTime(int, int, int, int, int, int, void*) {}

void MP_Begin(void*) {}
void MP_End(void*) {}
int MP_SendPacket(u8*, int, u64, void*) { return 0; }
int MP_RecvPacket(u8*, u64*, void*) { return 0; }
int MP_SendCmd(u8*, int, u64, void*) { return 0; }
int MP_SendReply(u8*, int, u64, u16, void*) { return 0; }
int MP_SendAck(u8*, int, u64, void*) { return 0; }
int MP_RecvHostPacket(u8*, u64*, void*) { return 0; }
u16 MP_RecvReplies(u8*, u64, u16, void*) { return 0; }
int Net_SendPacket(u8*, int, void*) { return 0; }
int Net_RecvPacket(u8*, void*) { return 0; }

void Camera_Start(int, void*) {}
void Camera_Stop(int, void*) {}
void Camera_CaptureFrame(int, u32*, int, int, bool, void*) {}

void Mic_Start(void*) {}
void Mic_Stop(void*) {}
int Mic_ReadInput(s16*, int, void*) { return 0; }

AACDecoder* AAC_Init() { return nullptr; }
void AAC_DeInit(AACDecoder*) {}
bool AAC_Configure(AACDecoder*, int, int) { return false; }
bool AAC_DecodeFrame(AACDecoder*, const void*, int, void*, int) { return false; }

bool Addon_KeyDown(KeyType, void*) { return false; }
void Addon_RumbleStart(u32, void*) {}
void Addon_RumbleStop(void*) {}
float Addon_MotionQuery(MotionQueryType, void*) { return 0.0f; }

#ifdef __ANDROID__
// The JIT's fast memory needs ASharedMemory_create from libandroid.so: without it melonDS
// falls back to opening /dev/ashmem, which apps targeting Android 10 or later may not do (the
// app was ended there).
DynamicLibrary* DynamicLibrary_Load(const char* name) { return (DynamicLibrary*)dlopen(name, RTLD_NOW | RTLD_LOCAL); }
void DynamicLibrary_Unload(DynamicLibrary* lib) { if (lib) dlclose(lib); }
void* DynamicLibrary_LoadFunction(DynamicLibrary* lib, const char* name) { return lib ? dlsym(lib, name) : nullptr; }
#else
DynamicLibrary* DynamicLibrary_Load(const char*) { return nullptr; }
void DynamicLibrary_Unload(DynamicLibrary*) {}
void* DynamicLibrary_LoadFunction(DynamicLibrary*, const char*) { return nullptr; }
#endif

}
