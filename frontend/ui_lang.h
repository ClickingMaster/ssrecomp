// The interface's language (LANGUAGE in the settings, list_lang): the game list, the settings page
// and the pause menu are written in English; Canvas::Text (ui.h) shows each text through Tr,
// which applies the selected interface translation. A text not in the table stays in English.
// shortcut: whole texts and a few prefixes only (a text built with a number or a name in the
// middle stays English), upgrade to keyed strings if more languages come.
#pragma once
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <algorithm>
#include <cstdint>
#include "ui_translations.h"
#include "ui_font.h"

namespace UiLang
{
inline int lang = 0; // English, Spanish, Italian, French, German, Japanese, Korean
inline int system = 0;
inline std::string choice = "auto";
inline const char* Codes[] = {"en","es","it","fr","de","ja","ko"};
inline std::string Normalise(std::string code) {
    for(char& c:code) if(c>='A'&&c<='Z') c+=32;
    const auto end=code.find_first_of("_-"); if(end!=std::string::npos)code.resize(end);
    return code;
}
inline int Language(const std::string& code) {
    const auto normal=Normalise(code);
    for(int i=0;i<7;++i)if(normal==Codes[i])return i;
    return 0;
}
inline const std::vector<std::pair<std::string,std::string>>& Choices() {
    static const std::vector<std::pair<std::string,std::string>> values={{"auto","AUTO"},{"en","ENGLISH"},{"es","ESPAÑOL"},{"it","ITALIANO"},{"fr","FRANÇAIS"},{"de","DEUTSCH"},{"ja","日本語"},{"ko","한국어"}};return values;
}
inline std::string ChoiceName() { for(const auto& value:Choices())if(value.first==choice)return value.second;return "AUTO"; }
inline std::string NextChoice(int direction) {
    const auto& values=Choices();size_t index=0;for(size_t i=0;i<values.size();++i)if(values[i].first==choice)index=i;
    return values[(index+values.size()+(direction<0?-1:1))%values.size()].first;
}
inline bool Tall() { return lang==5 || lang==6; }
inline int LineUnits() { return Tall()?14:9; }
inline void Set(const std::string& code) {
    const auto normal=Normalise(code);choice="auto";
    for(const auto* known:Codes)if(normal==known)choice=known;
    lang=choice=="auto"?system:Language(choice);
}
inline void SetSystem(const std::string& code) {
    system=Language(code);if(choice=="auto")lang=system;
}
// Invalid UTF-8 consumes one byte, so filenames and error messages cannot stall drawing.
inline uint32_t Next(const std::string& text,size_t& at) {
    if(at>=text.size())return 0;
    const auto first=(unsigned char)text[at++];if(first<128)return first;
    const int count=first>=0xf0&&first<=0xf4?3:first>=0xe0&&first<=0xef?2:first>=0xc2&&first<=0xdf?1:0;
    if(!count || at+count>text.size())return 0xfffd;
    uint32_t code=first&((1u<<(6-count))-1);
    for(int i=0;i<count;++i) {const auto c=(unsigned char)text[at+i];if((c&0xc0)!=0x80)return 0xfffd;code=(code<<6)|(c&63);}
    if(code<(count==1?0x80u:count==2?0x800u:0x10000u) || code>0x10ffff || (code>=0xd800&&code<=0xdfff))return 0xfffd;
    at+=count;return code;
}
inline bool LegacyAccent(uint32_t c) {
    const uint32_t codes[]={0xc0,0xc1,0xc8,0xc9,0xcc,0xcd,0xd1,0xd2,0xd3,0xd9,0xda,0xe0,0xe1,0xe8,0xe9,0xec,0xed,0xf1,0xf2,0xf3,0xf9,0xfa};
    for(auto code:codes)if(code==c)return true;return false;
}
inline int Advance(uint32_t code) {
    if(code<128 || LegacyAccent(code))return 6;
    const auto* glyph=UiFont::Find(code);return glyph?glyph->advance:6;
}
inline int Units(const std::string& text) {
    int units=0;for(size_t at=0;at<text.size();)units+=Advance(Next(text,at));return units;
}
inline std::vector<std::string> Wrap(const std::string& text,int width) {
    std::vector<std::string> lines;std::string line;int units=0;size_t space=std::string::npos;
    width=std::max(width,12);
    for(size_t at=0;at<text.size();) {
        const size_t start=at;const auto code=Next(text,at);const auto bytes=text.substr(start,at-start);
        if(code=='\n') {lines.push_back(line);line.clear();units=0;space=std::string::npos;continue;}
        const int advance=Advance(code);
        while(!line.empty() && units+advance>width) {
            if(space!=std::string::npos) {lines.push_back(line.substr(0,space));line=line.substr(space+1);}
            else {lines.push_back(line);line.clear();}
            units=Units(line);space=line.find_last_of(' ');
        }
        if(line.empty()&&code==' ')continue;
        if(code==' ')space=line.size();line+=bytes;units+=advance;
    }
    if(!line.empty())lines.push_back(line);return lines;
}

inline std::string Elide(const std::string& text,int width) {
    if(Units(text)<=width)return text;
    std::string out;int units=0;
    for(size_t at=0;at<text.size();) {size_t start=at;int next=Advance(Next(text,at));if(units+next+18>width)break;out+=text.substr(start,at-start);units+=next;}
    return out+"...";
}
inline int Characters(const std::string& text)
{
    int n = 0;
    for (unsigned char c : text) n += (c & 0xc0) != 0x80;
    return n;
}

// Compatibility accessors use the canonical generated catalogue as well.
inline const std::unordered_map<std::string,std::string>& Spanish() { return UiTranslations::Table(1); }
inline const std::unordered_map<std::string,std::string>& Italian() { return UiTranslations::Table(2); }
inline std::vector<std::pair<std::string,std::string>> Prefixes(int language) {
    std::vector<std::pair<std::string,std::string>> out;const auto& table=UiTranslations::Table(language);
    for(const char* key:{"RETROACHIEVEMENTS: LOGGED IN AS ","RETROACHIEVEMENTS: LOGIN FAILED: ","RETROACHIEVEMENTS: COULD NOT LOAD: ","USRCHEAT.DAT HAS NO ENTRY FOR THIS GAME CODE: ","ACHIEVEMENT: ","SETTINGS: ","FOLDER: ","LOADED: ","FAILED: ","UNLOCKED ","SO FAR ","VERSION ","FIND: ","SAVE IN ","NEW IN "}) {
        auto value=table.find(key);if(value!=table.end())out.push_back({key,value->second});
    }
    return out;
}
inline const std::vector<std::pair<std::string,std::string>>& SpanishPrefixes() { static const auto values=Prefixes(1);return values; }
inline const std::vector<std::pair<std::string,std::string>>& ItalianPrefixes() { static const auto values=Prefixes(2);return values; }

// Translate whole messages first, then known dynamic prefixes and controller hint columns.
inline std::string Tr(const std::string& s) {
    if(!lang||s.empty())return s;
    const auto& table=UiTranslations::Table(lang);
    auto direct=[&](const std::string& key)->const std::string* {auto it=table.find(key);return it==table.end()?nullptr:&it->second;};
    const auto actionColon=s.find(": ");
    if(actionColon!=std::string::npos && actionColon<48) {
        const auto action=s.substr(actionColon+2);
        for(const char* known:{"PLAY","BACK","QUIT","OPEN","CHOOSE","SELECT","CHANGE","TABS","SETTINGS","ON / OFF"})
            if(action==known)if(const auto* value=direct(action=="SELECT"?"CHOOSE":action))return Tr(s.substr(0,actionColon))+": "+*value;
    }
    const bool columns=s.find("  ")!=std::string::npos && s.find(":")!=std::string::npos;
    if(!columns)if(const auto* value=direct(s))if(*value!=s)return *value;
    if(columns) {
        std::string out;size_t start=0;
        while(start<s.size()) {
            size_t end=s.find("  ",start);out+=Tr(s.substr(start,end==std::string::npos?end:end-start));
            if(end==std::string::npos)break;
            size_t next=end;while(next<s.size()&&s[next]==' ')++next;out+=s.substr(end,next-end);start=next;
        }
        return out;
    }
    for(const char* prefix:{"RETROACHIEVEMENTS: LOGGED IN AS ","RETROACHIEVEMENTS: LOGIN FAILED: ","RETROACHIEVEMENTS: COULD NOT LOAD: ","USRCHEAT.DAT HAS NO ENTRY FOR THIS GAME CODE: ","ACHIEVEMENT: ","SETTINGS: ","FOLDER: ","LOADED: ","FAILED: ","UNLOCKED ","SO FAR ","VERSION ","FIND: ","SAVE IN ","NEW IN "}) {
        const std::string key=prefix;if(s.size()>key.size()&&!s.compare(0,key.size(),key))if(const auto* value=direct(key))return *value+((key=="UNLOCKED "||key=="SO FAR ")?Tr(s.substr(key.size())):s.substr(key.size()));
    }
    const auto colon=s.find(": ");
    if(colon!=std::string::npos && colon<48)if(const auto* value=direct(s.substr(colon+2)=="SELECT"?"CHOOSE":s.substr(colon+2))) {
        const auto left=s.substr(0,colon);const auto* translated=direct(left);return (translated?*translated:left)+": "+*value;
    }
    if(s.find(" / ")!=std::string::npos && (s.rfind("LEFT / ",0)==0 || s.rfind("UP / ",0)==0)) {
        std::string out;size_t start=0;
        while(start<s.size()) {size_t end=s.find(" / ",start);out+=Tr(s.substr(start,end==std::string::npos?end:end-start));if(end==std::string::npos)break;out+=" / ";start=end+3;}
        return out;
    }
    size_t digits=0;while(digits<s.size()&&s[digits]>='0'&&s[digits]<='9')++digits;
    if(digits&&digits<s.size()) {
        const auto rest=s.substr(digits);if(const auto* value=direct(rest))return s.substr(0,digits)+*value;
        if(rest.rfind(" OF ",0)==0)if(const auto* value=direct(" OF "))return s.substr(0,digits)+*value+rest.substr(4);
    }
    return s;
}
} // namespace UiLang
