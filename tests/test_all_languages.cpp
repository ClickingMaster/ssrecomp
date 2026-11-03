#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "launcher.h"
static void Save(const Canvas& c,const std::string& name) {
    std::ofstream f(name,std::ios::binary);f<<"P6\n"<<c.w<<" "<<c.h<<"\n255\n";
    for(auto p:c.px) {char rgb[]={char(p>>16),char(p>>8),char(p)};f.write(rgb,3);}
}
int main(int argc,char** argv) {
    UiLang::SetSystem("de-DE");UiLang::Set("auto");assert(UiLang::lang==4);
    UiLang::Set("ja_JP");assert(UiLang::lang==5);UiLang::SetSystem("fr");assert(UiLang::lang==5);
    UiLang::Set("auto");assert(UiLang::lang==3);UiLang::SetSystem("unsupported");assert(UiLang::lang==0);
    const auto& keys=UiTranslations::Table(1);
    for(int lang=1;lang<7;++lang) {
        const auto& table=UiTranslations::Table(lang);assert(table.size()==keys.size());
        for(const auto& entry:keys)assert(table.count(entry.first));
        for(const auto& entry:table) for(size_t at=0;at<entry.second.size();) {
            auto c=UiLang::Next(entry.second,at);assert(c!=0xfffd);
            if(c>=128&&!UiLang::LegacyAccent(c)) {const auto* g=UiFont::Find(c);assert(g);if(c!=0x3000&&c!=0xa0) {int pixels=0;for(auto row:g->rows)pixels|=row;assert(pixels);}}
        }
    }
    for(int lang=0;lang<7;++lang) {
        UiLang::Set(UiLang::Codes[lang]);assert(UiLang::lang==lang);
        assert(UiLang::Tr("NAME_NOT_A_TRANSLATION")=="NAME_NOT_A_TRANSLATION");
        const auto hint=UiLang::Tr("A: PLAY    B: BACK");
        assert(hint==std::string("A: ")+UiLang::Tr("PLAY")+"    B: "+UiLang::Tr("BACK"));
        if(lang) {const auto actions=UiLang::Tr("ENTER: SELECT    ESC: BACK");assert(actions.find(": SELECT")==std::string::npos && actions.find(": BACK")==std::string::npos);}
        if(lang)assert(UiLang::Tr("3 ON")==std::string("3 ")+UiLang::Tr("ON"));
        for(const auto& entry:UiTranslations::Table(lang)) for(int width:{72,156,288})
            for(const auto& line:UiLang::Wrap(entry.second,width))assert(UiLang::Units(line)<=width);
        const auto settings=Launcher::SettingsList();bool found=false;
        for(const auto& setting:settings)if(std::string(setting.key?setting.key:"")=="list_lang") {assert(setting.values.size()==8);found=true;}
        assert(found);
        assert(UiLang::Tr("ENTER")=="ENTER" && UiLang::Tr("RETROACHIEVEMENTS")=="RETROACHIEVEMENTS");
        UiLang::Set("auto");std::vector<std::string> choices;
        for(int step=0;step<8;++step){choices.push_back(UiLang::choice);UiLang::Set(UiLang::NextChoice(1));}
        assert(UiLang::choice=="auto");for(const auto& value:UiLang::Choices())assert(std::find(choices.begin(),choices.end(),value.first)!=choices.end());
        UiLang::Set(UiLang::Codes[lang]);
        for(auto size:{std::pair<int,int>{1280,720},{720,1280},{320,240}}) {
            Canvas c;c.Resize(size.first,size.second);Menu menu;
            menu.items={{"RESUME",""},{"SCREEN LAYOUT","MERGED"},{"WIDESCREEN WORLD","ON"},{"OTHER VERSION POKEMON","ON"},{"SAVE BACKUPS",""},{"CHEATS",""},{"LANGUAGE",UiLang::Codes[lang]}};
            menu.Draw(c,"SETTINGS","");assert(menu.drawnX>=0&&menu.drawnX+menu.drawnW<=c.w);
            for(int i=0;i<int(menu.items.size());++i) {
                const int y=menu.drawnFirstY+i*menu.drawnLineH;
                if(y<menu.drawnY+menu.drawnH)assert(menu.ItemAt(menu.drawnX+8,y+1)==i);
            }
            if(argc>1&&size.first!=320)Save(c,std::string(argv[1])+"/"+UiLang::Codes[lang]+"-pause-"+std::to_string(c.w)+".ppm");
            Launcher::SettingsPage page;
            for(int group=0;group<int(Launcher::SettingsGroups().size());++group) {
                page.Enter(group);c.Clear();page.Draw(c,"");
                for(auto row:page.rows)assert(row.second>=0&&row.second<c.h);
                if(argc>1&&size.first!=320&&group==0)Save(c,std::string(argv[1])+"/"+UiLang::Codes[lang]+"-settings-"+std::to_string(c.w)+".ppm");
            }
        }
    }
    // Malformed names cannot produce an infinite decode loop.
    std::string broken="\xf0\x28\x8c\x28\xff";size_t at=0;int count=0;while(at<broken.size()){UiLang::Next(broken,at);assert(++count<=int(broken.size()));}
    std::cout<<"Seven-language coverage, locales, glyphs, wrapping, hints and menus passed\n";
}
