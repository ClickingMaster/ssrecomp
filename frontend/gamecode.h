#pragma once
// Another language's version of a game (Pokemon White in Spanish, IRAS) plays with the English
// version's layouts (IRAO): its game code differs only in the last letter, the language. Its own
// function table (tools/versions, tools/port_version.py) or shifts (tools/soulsilver/*.json) say
// where its functions are; what it is, for everything else, is the English version's code.
#include <cstring>
#include <string>

inline std::string BaseGameCode(const std::string& code)
{
    static const char* const english[] = { "IRBO", "IRAO", "IREO", "IRDO", "IPKE", "IPGE", "CPUE", "ADAE", "APAE" };
    if (code.size() == 4)
        for (const char* e : english)
            if (!code.compare(0, 3, e, 3)) return e;
    return code;
}
inline bool IsGame(const std::string& code, const char* english) { return BaseGameCode(code) == english; }
