// Shared state between the frontend and the Platform implementation.
#pragma once
#include <cstdint>

#include <string>

namespace Host
{
extern bool StopRequested;
extern int LogLevel;

// Called by the core whenever cartridge save memory changes.
void SaveDirty(const uint8_t* data, uint32_t len);
// The DS firmware's file (firmware.bin beside the saves), or empty: not kept (tests). See MakeConsole.
extern std::string FirmwarePath;
}
