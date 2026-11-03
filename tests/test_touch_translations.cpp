#define SDL_MAIN_HANDLED
#include "ui.h"
#include "touchlayout.h"
#include <cassert>
#include <iostream>
int main() {
 for(const auto* language:UiLang::Codes) {
  UiLang::Set(language);
  for(int width:{320,360,720,1280}) {
   const char* actions[]={"PREV","NEXT","-","+","RESET","CANCEL","SAVE"};
   for(int i=0;i<7;++i) {
    Canvas c;c.Resize(width,240);int left=i*width/7,right=(i+1)*width/7;
    TouchLayout::DrawAction(c,left,right,194,44,actions[i],width>=800?2:1,0xffffffff);
    int pixels=0;
    for(int y=0;y<c.h;++y)for(int x=0;x<c.w;++x)if(c.px[y*c.w+x]) {++pixels;if(!(x>=left+2&&x<right-2&&y>=194&&y<238)) {std::cerr<<language<<" width="<<width<<" action="<<actions[i]<<" pixel="<<x<<","<<y<<" bounds="<<left<<","<<right<<"\n";return 1;}}
    assert(pixels);
   }
  }
 }
 std::cout<<"Translated touch actions fit all seven languages at 320/360/720/1280 pixels\n";
}
