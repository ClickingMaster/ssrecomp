// HOME Menu entries for the games of the all-games program on the Switch: one per game, installed
// by the program itself on the player's own console (no PC, no keys file: the one key needed is
// made by the console). Each starts the all-games program straight into its game
// ("--game IREO"). frontend/switch_forwarder.cpp.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace SwitchForwarder
{
struct Entry
{
    std::string name;            // "Pokemon Black 2"; the HOME Menu spells Pokémon (switch_forwarder.cpp)
    std::string args;            // after the program's path, e.g. "--game IREO"
    std::vector<uint8_t> icon;   // a 256x256 JPEG
};

// Installs the entries (again: an entry made before is replaced), each starting nroPath
// ("sdmc:/switch/pokemon-ds-wide/pokemon-ds-wide.nro") with its arguments. Returns how many were
// installed; error says what failed, if anything did.
int Install(const std::string& nroPath, const std::vector<Entry>& entries, std::string& error);

// For checking: the NCAs made are also written to this folder (empty: not).
void SetDebugFolder(const std::string& folder);
}
