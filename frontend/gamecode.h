#pragma once
// Another language's version of a game (Pokemon White in Spanish, IRAS) plays with the English
// version's layouts (IRAO): its game code differs only in the last letter, the language. Its own
// function table (tools/versions, tools/port_version.py) or shifts (tools/soulsilver/*.json) say
// where its functions are; what it is, for everything else, is the English version's code.
#include <cstring>
#include <string>
#include "regional_profiles.h"

inline std::string BaseGameCode(const std::string& code)
{
    static const char* const aliases[][2] = {
        { "IRBS", "IRBO" }, { "IRAS", "IRAO" }, { "IRES", "IREO" }, { "IRDS", "IRDO" },
        { "CPUS", "CPUE" }, { "IPGS", "IPGE" }, { "IPKS", "IPKE" }, { "ADAS", "ADAE" },
        { "IRBF", "IRBO" }, { "IRAF", "IRAO" }, { "IREF", "IREO" }, { "IRDF", "IRDO" },
        { "IPGF", "IPGE" }, { "IPKF", "IPKE" }, { "CPUF", "CPUE" }, { "IPKI", "IPKE" },
        { "APAF", "APAE" }, { "APAS", "APAE" }, { "APAD", "APAE" }, { "ADAD", "ADAE" }, { "ADAI", "ADAE" }
    };
    for (const auto& alias : aliases) if (code == alias[0]) return alias[1];
    if (const auto* p = RegionalProfiles::Find(code.c_str())) return p->base;
    return code;
}
inline bool IsGame(const std::string& code, const char* english) { return BaseGameCode(code) == english; }
