// Reading the ARM9 code of a DS ROM (the main program and its overlays, decompressed), as
// tools/ndsrom.py does it, for the interpreter watch in recomp_rt.cpp.
#pragma once
#include <algorithm>
#include <cstring>
#include <vector>
#include "types.h"

namespace ndsrom
{
using melonDS::u8;
using melonDS::u32;
using melonDS::u64;

// Nintendo's "backwards LZ" compression, as tools/ndsrom.py undoes it (see there).
inline std::vector<u8> BlzDecompress(const u8* data, size_t len)
{
    std::vector<u8> out(data, data + len);
    if (len < 8) return out;
    u32 bounds, extra;
    memcpy(&bounds, data + len - 8, 4);
    memcpy(&extra, data + len - 4, 4);
    if (!extra) return out;
    const size_t headerLen = bounds >> 24, packedLen = bounds & 0xFFFFFF;
    if (headerLen > len || packedLen > len) return {};
    out.resize(len + extra);
    size_t src = len - headerLen, dst = len + extra;
    const size_t stop = len - packedLen;
    while (src > stop)
    {
        u8 flags = data[--src];
        for (int i = 0; i < 8 && src > stop; i++, flags <<= 1)
        {
            if (flags & 0x80)
            {
                if (src < 2) return {};
                src -= 2;
                const u32 info = data[src] | (data[src + 1] << 8);
                const u32 length = (info >> 12) + 3, disp = (info & 0xFFF) + 3;
                for (u32 n = 0; n < length; n++)
                {
                    if (!dst || dst + disp > out.size()) return {};
                    dst--;
                    out[dst] = out[dst + disp];
                }
            }
            else
            {
                if (!dst) return {};
                out[--dst] = data[--src];
            }
        }
    }
    return out;
}

inline u32 Read32(const u8* p) { u32 v; memcpy(&v, p, 4); return v; }

// The main program and overlays of a DS ROM, decompressed, as tools/ndsrom.py reads them.
inline bool ReadRomCode(const u8* rom, size_t size, std::vector<u8>& main, std::vector<std::vector<u8>>& overlays)
{
    if (size < 0x60) return false;
    const u32 arm9Off = Read32(rom + 0x20), arm9Ram = Read32(rom + 0x28), arm9Size = Read32(rom + 0x2C);
    const u32 fatOff = Read32(rom + 0x48), ovtOff = Read32(rom + 0x50), ovtSize = Read32(rom + 0x54);
    if ((u64)arm9Off + arm9Size > size || (u64)ovtOff + ovtSize > size) return false;
    main.assign(rom + arm9Off, rom + arm9Off + arm9Size);
    // The module parameters (found through the "nitrocode" marker) say where the compressed
    // part of the main program ends.
    const u32 kNitrocode = 0xDEC00621;
    for (size_t pos = 0x1C; pos + 4 <= main.size(); pos += 4)
    {
        if (Read32(&main[pos]) != kNitrocode) continue;
        const u32 end = Read32(&main[pos - 0x1C + 0x14]);
        if (end > arm9Ram && end <= arm9Ram + main.size())
        {
            // The decompressed part, then the rest of the program as it is.
            std::vector<u8> unpacked = BlzDecompress(main.data(), end - arm9Ram);
            if (unpacked.empty()) return false;
            const size_t head = unpacked.size(), tail = main.size() - (end - arm9Ram);
            unpacked.resize(head + tail);
            if (tail) memcpy(&unpacked[head], &main[end - arm9Ram], tail);
            main.swap(unpacked);
        }
        break;
    }
    overlays.clear();
    for (u32 i = 0; i + 32 <= ovtSize; i += 32)
    {
        const u8* e = rom + ovtOff + i;
        const u32 id = Read32(e), fileId = Read32(e + 24), comp = Read32(e + 28);
        if ((u64)fatOff + (u64)fileId * 8 + 8 > size) return false;
        const u32 start = Read32(rom + fatOff + fileId * 8), end = Read32(rom + fatOff + fileId * 8 + 4);
        if (start > end || end > size) return false;
        if (id >= overlays.size()) overlays.resize(id + 1);
        if (comp & (1u << 24))
            overlays[id] = BlzDecompress(rom + start, std::min<size_t>(comp & 0xFFFFFF, end - start));
        else
            overlays[id].assign(rom + start, rom + end);
    }
    return true;
}
} // namespace ndsrom
