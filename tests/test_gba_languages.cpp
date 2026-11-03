#include <cassert>
#include <fstream>
#include <iostream>
#include "ui.h"
int main(int argc,char** argv) {
    for(int language=0;language<7;++language) {
        UiLang::Set(UiLang::Codes[language]);gbaw::Canvas canvas;canvas.Resize(800,450);
        gbaw::Menu menu;menu.items={{"RESUME",""},{"VOLUME","100%"},{"SAVE STATE",""},{"LOAD STATE",""},{"CHEATS","OFF"},{"BACK TO GAME LIST",""}};
        menu.Draw(canvas,"SETTINGS","A: CHOOSE    B: BACK",true);
        assert(menu.box.x>=0&&menu.box.x+menu.box.w<=canvas.w);
        assert(menu.rows.size()==menu.items.size());
        for(size_t i=1;i<menu.rows.size();++i)assert(menu.rows[i].y>=menu.rows[i-1].y+menu.rows[i-1].h);
        if(argc>1) {std::ofstream f(std::string(argv[1])+"/gba-"+UiLang::Codes[language]+".ppm",std::ios::binary);f<<"P6\n800 450\n255\n";for(auto p:canvas.px){char rgb[]={char(p>>16),char(p>>8),char(p)};f.write(rgb,3);}}
    }
    std::cout<<"Embedded GBA menus rendered in all seven languages\n";
}
