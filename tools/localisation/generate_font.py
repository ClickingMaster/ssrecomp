#!/usr/bin/env python3
"""Bake SIL-OFL Noto CJK glyphs into portable monochrome UI data (Pillow required)."""
from pathlib import Path
import argparse,hashlib,json
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--japanese',type=Path,required=True);p.add_argument('--korean',type=Path,required=True);a=p.parse_args()
expected=['68a3fc98800b2a27b371f2fb79991daf3633bd89309d4ffaa6946fd587f375b5','6bcb2a0703aa137e874fc2dffa85f6c21ba9a67fa329e81b8c801663af7e992a']
for f,sha in zip([a.japanese,a.korean],expected):
 if hashlib.sha256(f.read_bytes()).hexdigest()!=sha:raise ValueError('Unexpected font source: '+str(f))
jp=ImageFont.truetype(str(a.japanese),12);ko=ImageFont.truetype(str(a.korean),12);latin=ImageFont.truetype(str(a.japanese),9)
chars=set(range(33,127))|set(range(0xa0,0x250))|set(range(0x3000,0x3100))|set(range(0x1100,0x1200))|set(range(0x3130,0x3190))|set(range(0x4e00,0xa000))|set(range(0xac00,0xd7a4))
d=json.loads((ROOT/'localisation/translations.json').read_text(encoding='utf-8'))
for table in d['tables'].values():
 for text in table.values():chars.update(ord(c) for c in text if ord(c)>=128)
lines=['// Generated from Noto Sans CJK JP/KR. SIL Open Font License 1.1; see LICENSE-Noto.txt.','// Font source SHA-256 values pinned in tools/localisation/generate_font.py.','#pragma once','#include <cstdint>','#include <cstddef>','namespace UiFont {','struct Glyph { uint32_t code; uint8_t advance; uint16_t rows[14]; };','inline constexpr Glyph glyphs[] = {']
for i,code in enumerate(sorted(chars)):
 wide=code>=0x1100;hangul=0x1100<=code<=0x11ff or 0x3130<=code<=0x318f or 0xac00<=code<=0xd7a3
 font=ko if hangul else jp if wide else latin
 image=Image.new('L',(12,14));draw=ImageDraw.Draw(image);letter=chr(code);upper=letter.upper();letter=upper if not wide and len(upper)==1 else letter;draw.text((0,11 if wide else 10),letter,font=font,fill=255,anchor='ls')
 advance=12 if wide else max(6,min(8,round(font.getlength(chr(code)))))
 rows=[sum((1<<(11-x)) for x in range(12) if image.getpixel((x,y))>=96) for y in range(14)]
 lines.append('    {0x%x, %d, {%s}},'%(code,advance,','.join('0x%x'%v for v in rows)))
 if i%5000==0:print('Baked',i,'/',len(chars),flush=True)
lines+=['};','inline const Glyph* Find(uint32_t code) {','    size_t lo=0,hi=sizeof(glyphs)/sizeof(glyphs[0]);','    while(lo<hi) { size_t mid=lo+(hi-lo)/2; if(glyphs[mid].code<code)lo=mid+1;else hi=mid; }','    return lo<sizeof(glyphs)/sizeof(glyphs[0]) && glyphs[lo].code==code ? &glyphs[lo] : nullptr;','}','} // namespace UiFont','']
(ROOT/'frontend/ui_font.h').write_text('\n'.join(lines),encoding='utf-8');print('Baked glyphs',len(chars),flush=True)
