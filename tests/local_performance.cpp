#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "launcher.h"
#include <chrono>
#include <iostream>
int main() {
    std::vector<Launcher::Game> games(200);
    for (auto& g:games) { g.name="POKEMON TEST"; g.art.assign(Launcher::Art*Launcher::Art,0xFF223344); }
    for (int round=0;round<3;round++) for(auto size:{std::pair<int,int>{1080,2400},{576,1280}}) {
        Canvas canvas; canvas.Resize(size.first,size.second); int scroll=0;
        auto start=std::chrono::steady_clock::now();
        for(int i=0;i<60;i++) Launcher::Draw(canvas,games,0,0,scroll,"");
        double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/60;
        std::cout<<size.first<<"x"<<size.second<<": "<<ms<<" ms/frame\n";
    }
}