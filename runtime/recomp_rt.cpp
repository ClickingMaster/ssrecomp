// Dispatcher for recompiled chunks.
//
// Several overlays are linked at the same addresses, so an address alone does not
// identify code. A chunk is only entered after its bytes have been compared with
// what is in emulated memory, and every recompiled instruction re-checks its own
// opcode, so a stale chunk can never execute the wrong instruction.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include "NDS.h"
#include "ARM.h"
#include "ARMInterpreter.h"
#include "recomp_rt.h"
#include "recomp_api.h"
#include "ndsrom.h"

using namespace melonDS;

bool rc_single = false;
RcStats rc_stats;
u32 rc_frame = 1;
u32 rc_section_last_frame[512];

namespace
{
const u32 kMainBase = 0x02000000, kMainEnd = 0x02400000;
const u32 kItcmBase = 0x01FF8000, kItcmEnd = 0x02000000;
const u32 kBiosBase = 0xFFFF0000, kBiosEnd = 0xFFFF1000;

// Chunk index + 1 for every halfword of executable address space; 0 means nothing stamped.
std::vector<u32> viewMain((kMainEnd - kMainBase) >> 1);
std::vector<u32> viewItcm((kItcmEnd - kItcmBase) >> 1);
std::vector<u32> viewBios((kBiosEnd - kBiosBase) >> 1);

// After a failed byte comparison, skip this many lookups before comparing again.
std::vector<u8> retryDelay;
std::vector<u32> chunkLastFrame;

struct Probe
{
    u32 addr;
    u32 section;
    u32 lastFrame;
    u32 next; // next probe at the same address (index + 1), for overlays that share addresses
};
std::vector<Probe> probes;
std::vector<u32> probeHead; // per halfword of main RAM: first probe index + 1
std::vector<u8> chunkHasProbe; // lets the dispatcher skip that table for most chunks
u64 stamps = 0, evictions = 0;

// Addresses where Miss found no chunk, so that code run there by the interpreter (a
// section changed at run time, such as the copy-protection code that decrypts itself)
// does not search every section again for each instruction. An entry lasts a few
// frames, after which the address is looked up again (code may have been loaded there).
struct NoChunk
{
    u32 addr; // address | thumb, 0 = empty
    u32 until; // rc_frame up to which it holds
};
NoChunk noChunk[4096];
const u32 kNoChunkFrames = 4;
inline NoChunk& NoChunkSlot(u32 key) { return noChunk[(key ^ (key >> 12)) & 4095]; }

inline u32* ViewSlot(u32 addr)
{
    if (addr >= kMainBase && addr < kMainEnd) return &viewMain[(addr - kMainBase) >> 1];
    if (addr >= kItcmBase && addr < kItcmEnd) return &viewItcm[(addr - kItcmBase) >> 1];
    if (addr >= kBiosBase && addr < kBiosEnd) return &viewBios[(addr - kBiosBase) >> 1];
    return nullptr;
}

const u8* GuestPtr(ARMv5* cpu, u32 addr)
{
    if (addr >= kBiosBase) return cpu->NDS.GetARM9BIOS().data() + (addr - kBiosBase);
    if (addr >= kMainBase) return cpu->NDS.MainRAM + (addr & cpu->NDS.MainRAMMask);
    return cpu->ITCM + (addr & (ITCMPhysicalSize - 1));
}

void Stamp(u32 id)
{
    const RcChunk& c = rc_chunks[id];
    for (u32 a = c.start; a < c.end; a += 2)
        if (u32* slot = ViewSlot(a)) *slot = id + 1;
    stamps++;
}

void Evict(u32 id)
{
    const RcChunk& c = rc_chunks[id];
    for (u32 a = c.start; a < c.end; a += 2)
        if (u32* slot = ViewSlot(a))
            if (*slot == id + 1) *slot = 0;
    evictions++;
}

// Finds a chunk whose code is really present at addr and stamps it. Returns chunk index + 1, or 0.
u32 Miss(ARMv5* cpu, u32 addr, bool thumb)
{
    for (u32 s = 0; s < rc_num_sections; s++)
    {
        const RcSection& sec = rc_sections[s];
        if (addr < sec.base || addr >= sec.base + sec.size || !sec.numChunks) continue;

        u32 lo = sec.firstChunk, hi = sec.firstChunk + sec.numChunks;
        while (lo < hi)
        {
            u32 mid = (lo + hi) / 2;
            if (rc_chunks[mid].end <= addr) lo = mid + 1;
            else hi = mid;
        }
        if (lo >= sec.firstChunk + sec.numChunks) continue;
        const RcChunk& c = rc_chunks[lo];
        if (addr < c.start || (bool)c.thumb != thumb) continue;

        if (retryDelay[lo])
        {
            retryDelay[lo]--;
            continue;
        }
        if (memcmp(sec.image + (c.start - sec.base), GuestPtr(cpu, c.start), c.end - c.start) != 0)
        {
            retryDelay[lo] = 255;
            continue;
        }
        Stamp(lo);
        return lo + 1;
    }
    return 0;
}

void BailCommon(u32 section, u32 addr)
{
    rc_stats.guardFails++;
    if (u32* slot = ViewSlot(addr))
        if (*slot) Evict(*slot - 1);
    (void)section;
}
}

void rc_reset()
{
    std::fill(viewMain.begin(), viewMain.end(), 0);
    std::fill(viewItcm.begin(), viewItcm.end(), 0);
    std::fill(viewBios.begin(), viewBios.end(), 0);
    retryDelay.assign(rc_num_chunks, 0);
    chunkLastFrame.assign(rc_num_chunks, 0);
    rc_stats = RcStats();
    stamps = evictions = 0;
    memset(noChunk, 0, sizeof(noChunk));
}

void rc_print_stats()
{
    u64 total = rc_stats.dispatches + rc_stats.interpreted;
    fprintf(stderr, "[recomp] chunk entries %llu, interpreter fallbacks %llu (%.2f%% of dispatches), "
        "guard failures %llu, chunks stamped %llu, evicted %llu\n",
        (unsigned long long)rc_stats.dispatches, (unsigned long long)rc_stats.interpreted,
        total ? 100.0 * rc_stats.interpreted / total : 0.0,
        (unsigned long long)rc_stats.guardFails, (unsigned long long)stamps, (unsigned long long)evictions);
}

void rc_bail_arm(ARMv5* cpu, u32 section)
{
    u32 addr = cpu->R[15] - 8;
    if (cpu->CheckCondition(cpu->CurInstr >> 28))
    {
        u32 icode = ((cpu->CurInstr >> 4) & 0xF) | ((cpu->CurInstr >> 16) & 0xFF0);
        ARMInterpreter::ARMInstrTable[icode](cpu);
    }
    else if ((cpu->CurInstr & 0xFE000000) == 0xFA000000)
        ARMInterpreter::A_BLX_IMM(cpu);
    else
        cpu->AddCycles_C();
    BailCommon(section, addr);
}

void rc_bail_thumb(ARMv5* cpu, u32 section)
{
    u32 addr = cpu->R[15] - 4;
    u32 icode = (cpu->CurInstr >> 6) & 0x3FF;
    ARMInterpreter::THUMBInstrTable[icode](cpu);
    BailCommon(section, addr);
}

namespace melonDS
{
// Called by the core's ARM9 loop in place of interpreting one instruction.
// Returns false if there is no recompiled code for the current PC.
bool RecompRun(ARMv5* cpu)
{
    if (retryDelay.empty()) rc_reset();

    bool thumb = (cpu->CPSR & 0x20) != 0;
    u32 addr = cpu->R[15] - (thumb ? 2 : 4);
    u32* slot = ViewSlot(addr);
    if (!slot)
    {
        rc_stats.interpreted++;
        return false;
    }
    u32 id = *slot;
    if (!id || (bool)rc_chunks[id - 1].thumb != thumb)
    {
        NoChunk& known = NoChunkSlot(addr | thumb);
        if (known.addr == (addr | thumb) && known.until >= rc_frame)
        {
            rc_stats.interpreted++;
            return false;
        }
        id = Miss(cpu, addr, thumb);
        if (!id)
        {
            known.addr = addr | thumb;
            known.until = rc_frame + kNoChunkFrames;
            rc_stats.interpreted++;
            return false;
        }
    }
    rc_stats.dispatches++;
    rc_section_last_frame[rc_chunks[id - 1].section] = rc_frame;
    chunkLastFrame[id - 1] = rc_frame;
    if (!chunkHasProbe.empty() && chunkHasProbe[id - 1] && addr >= kMainBase && addr < kMainEnd)
    {
        for (u32 p = probeHead[(addr - kMainBase) >> 1]; p; p = probes[p - 1].next)
            if (probes[p - 1].section == rc_chunks[id - 1].section) probes[p - 1].lastFrame = rc_frame;
    }
    rc_chunks[id - 1].fn(cpu, addr);
    return true;
}
}

namespace
{
// The watch (see recomp_rt.h): per entry of the table, the section's bytes from the ROM and
// whether memory matched them when last compared (once per frame at most); per 4 KB page of
// main RAM, the entries whose sections cover it.
const RcWatchSection* watchTable = nullptr;
u32 watchCount = 0;
std::vector<std::vector<u8>> watchImages;
std::vector<u32> watchFrame;
std::vector<u8> watchMatches;
std::vector<std::vector<u16>> watchPages;
// Per page, the entries of watchPages whose sections were in memory when the page was last
// looked at, and the frame that was: many overlays share addresses, and a jump then only
// looks at the one or two that are loaded.
std::vector<std::vector<u16>> watchLoaded;
std::vector<u32> watchPageFrame;

// Whether the section of watch entry w is what is in memory now.
bool WatchMatches(ARMv5* cpu, u32 w)
{
    if (watchFrame[w] == rc_frame) return watchMatches[w];
    watchFrame[w] = rc_frame;
    const RcWatchSection& ws = watchTable[w];
    const RcSection& sec = rc_sections[ws.section];
    const std::vector<u8>& image = watchImages[w];
    bool ok = !image.empty() && ws.numSamples;
    for (u32 i = 0; ok && i < ws.numSamples; i++)
    {
        const u32 off = ws.samples[i] - sec.base;
        ok = off + 4 <= image.size() &&
            memcmp(&image[off], cpu->NDS.MainRAM + (ws.samples[i] & cpu->NDS.MainRAMMask), 4) == 0;
    }
    watchMatches[w] = ok;
    return ok;
}
}

void rc_watch_register(const RcWatchSection* table, u32 count)
{
    watchTable = table;
    watchCount = count;
}

bool rc_watch_enable(const u8* rom, size_t size)
{
    if (!watchCount) return false;
    std::vector<u8> main;
    std::vector<std::vector<u8>> overlays;
    if (!ndsrom::ReadRomCode(rom, size, main, overlays)) return false;
    watchImages.assign(watchCount, {});
    watchFrame.assign(watchCount, ~0u);
    watchMatches.assign(watchCount, 0);
    watchPages.assign((kMainEnd - kMainBase) >> 12, {});
    watchLoaded.assign(watchPages.size(), {});
    watchPageFrame.assign(watchPages.size(), ~0u);
    for (u32 w = 0; w < watchCount; w++)
    {
        const RcWatchSection& ws = watchTable[w];
        const RcSection& sec = rc_sections[ws.section];
        if (ws.source == -1) watchImages[w] = main;
        else if (ws.source >= 0 && (size_t)ws.source < overlays.size()) watchImages[w] = overlays[ws.source];
        if (sec.base < kMainBase || sec.base >= kMainEnd) continue;
        const u32 end = std::min<u32>(sec.base + sec.size, kMainEnd);
        for (u32 page = (sec.base - kMainBase) >> 12; page <= ((end - 1 - kMainBase) >> 12); page++)
            watchPages[page].push_back((u16)w);
    }
    return true;
}

namespace melonDS
{
// Called by ARMv5::JumpTo while RecompWatch is set: the jump's target counts as code of
// every watched section there that is in memory, as a chunk entry does for recompiled code.
void RecompWatchJump(ARMv5* cpu, u32 addr)
{
    if (addr < kMainBase || addr >= kMainEnd || watchPages.empty()) return;
    const u32 page = (addr - kMainBase) >> 12;
    if (watchPageFrame[page] != rc_frame)
    {
        watchPageFrame[page] = rc_frame;
        watchLoaded[page].clear();
        for (u16 w : watchPages[page])
            if (WatchMatches(cpu, w)) watchLoaded[page].push_back(w);
    }
    for (u16 w : watchLoaded[page])
    {
        const u32 s = watchTable[w].section;
        if (addr < rc_sections[s].base || addr >= rc_sections[s].base + rc_sections[s].size) continue;
        rc_section_last_frame[s] = rc_frame;
        if (!probeHead.empty())
            for (u32 p = probeHead[(addr - kMainBase) >> 1]; p; p = probes[p - 1].next)
                if (probes[p - 1].section == s) probes[p - 1].lastFrame = rc_frame;
    }
}
}

bool rc_ran_recently(const char* section, unsigned addr, unsigned frames)
{
    if (watchCount && !watchPages.empty())
    {
        // Watched: the frontend has a probe on each function it asks about.
        for (const Probe& p : probes)
            if (p.addr == addr && !strcmp(rc_sections[p.section].name, section))
                return p.lastFrame && p.lastFrame + frames >= rc_frame;
        return false;
    }
    if (chunkLastFrame.empty()) return false;
    for (u32 s = 0; s < rc_num_sections; s++)
    {
        const RcSection& sec = rc_sections[s];
        if (strcmp(sec.name, section) != 0) continue;
        for (u32 c = sec.firstChunk; c < sec.firstChunk + sec.numChunks; c++)
            if (addr >= rc_chunks[c].start && addr < rc_chunks[c].end)
                return chunkLastFrame[c] && chunkLastFrame[c] + frames >= rc_frame;
    }
    return false;
}

int rc_add_probe(const char* section, unsigned addr)
{
    if (addr < kMainBase || addr >= kMainEnd) return -1;
    u32 sec = ~0u;
    for (u32 s = 0; s < rc_num_sections; s++)
        if (!strcmp(rc_sections[s].name, section)) sec = s;
    if (sec == ~0u) return -1;
    if (probeHead.empty()) probeHead.assign((kMainEnd - kMainBase) >> 1, 0);
    u32& head = probeHead[(addr - kMainBase) >> 1];
    probes.push_back({ addr, sec, 0, head });
    if (chunkHasProbe.empty()) chunkHasProbe.assign(rc_num_chunks, 0);
    for (u32 c = rc_sections[sec].firstChunk; c < rc_sections[sec].firstChunk + rc_sections[sec].numChunks; c++)
        if (addr >= rc_chunks[c].start && addr < rc_chunks[c].end) chunkHasProbe[c] = 1;
    head = (u32)probes.size();
    return (int)probes.size() - 1;
}

unsigned rc_probe_last_frame(int probe)
{
    return probe >= 0 && probe < (int)probes.size() ? probes[probe].lastFrame : 0;
}

unsigned rc_current_frame()
{
    return rc_frame;
}
