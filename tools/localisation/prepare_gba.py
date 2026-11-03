"""Prepare a build-local GBA UI overlay without modifying the supplied source."""
from pathlib import Path
import argparse
p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
source=a.source.resolve();output=a.output.resolve()
if source==output or source in output.parents:raise ValueError('Use an independent build-local output')
def write(path,data):
 path.parent.mkdir(parents=True,exist_ok=True)
 if not path.exists() or path.read_bytes()!=data:path.write_bytes(data)
for name in ['CMakeLists.txt','LICENSE','frontend','tools','art','patches']:
 root=source/name
 if not root.exists():continue
 for f in ([root] if root.is_file() else root.rglob('*')):
  if not f.is_file():continue
  if f.suffix.lower() in ['.nds','.gba','.sav','.state','.st','.nef','.sbin']:raise ValueError('Unexpected game/test file in GBA source')
  write(output/f.relative_to(source),f.read_bytes())
ui=(source/'frontend/ui.h').read_text()
ui=ui.replace('#pragma once','#pragma once\n#include "ui_text.h"',1)
a0=ui.index('    static int TextWidth(');b0=ui.index('    void TextCentred(',a0)
ui=ui[:a0]+'    static int TextWidthRaw(const std::string& s,int scale) { return UiLang::Units(s)*scale; }\n    void TextRaw(int x,int y,const std::string& text,int scale,uint32_t colour) { UiText::DrawRaw(*this,x,y,text,scale,colour); }\n    static int TextWidth(const std::string& s,int scale) { return UiText::Width(s,scale); }\n    void Text(int x,int y,const std::string& text,int scale,uint32_t colour) { UiText::Draw(*this,x,y,text,scale,colour); }\n'+ui[b0:]
a0=ui.index('inline std::vector<std::string> Wrap(');b0=ui.index('// The colours of the menus.',a0)
ui=ui[:a0]+'inline std::vector<std::string> Wrap(const std::string& text,int chars) { return UiLang::Wrap(UiLang::Tr(text),chars*6); }\n\n'+ui[b0:]
ui=ui.replace('"< " + it.value + " >"','"< " + UiLang::Tr(it.value) + " >"')
ui=ui.replace('const int lineH = big ?', 'const int baseLine = UiLang::Tall() ? 16 : 12;\n        const int lineH = big ?').replace('std::max(12, std::min(30,','std::max(baseLine * s, std::min(30,').replace(' : 12;\n        auto shown', ' : baseLine;\n        auto shown')
ui=ui.replace('const int headH = 7 * s + 10, footH = 18;','const int headH = (UiLang::Tall()?12:7) * s + 10, footH = UiLang::Tall()?24:18;')
ui=ui.replace('const int ty = y + (lineH - 7 * s) / 2;','const int ty = y + (lineH - (UiLang::Tall()?10:7) * s) / 2;')
write(output/'frontend/ui.h',ui.encode())
main=(source/'frontend/main.cpp').read_text();main=main.replace('"< " + ln.value + " >"','"< " + UiLang::Tr(ln.value) + " >"').replace('const int top = 36, bottom = c.h - 22, lineH = 10;','const int top = 36, bottom = c.h - 22, lineH = UiLang::Tall()?16:10;')
main=main.replace('c.Text(x + 12, y, ln.text, 1, sel ? Colour::Text : Colour::Hint);','c.TextRaw(x + 12, y, ln.text, 1, sel ? Colour::Text : Colour::Hint);')
write(output/'frontend/main.cpp',main.encode())
print('Prepared localised GBA UI at '+str(output))

# The generic editor is shared, including localisation-aware action bounds.
host_editor=Path(__file__).resolve().parents[2]/"frontend/touchlayout.h"
write(output/"frontend/touchlayout.h",host_editor.read_bytes())

touch=(source/'frontend/touch.h').read_text()
touch=touch.replace('c.TextCentred(r.x + r.w / 2, r.y + (r.h - 7) / 2, label, 1, text);','if(UiLang::Tr(label)!=label)TouchLayout::DrawAction(c,r.x,r.x+r.w,r.y,r.h,label,1,text);else c.TextCentred(r.x + r.w / 2, r.y + (r.h - 7) / 2, label, 1, text);')
write(output/'frontend/touch.h',touch.encode())
