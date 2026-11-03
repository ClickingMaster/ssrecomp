// Shared localisation and glyph drawing for the DS frontend and embedded GBA menus.
#pragma once
#include <cstring>
#include "ui_lang.h"
namespace UiText {
inline int Width(const std::string& s,int scale) { return UiLang::Units(UiLang::Tr(s))*scale; }
template<class C> void DrawRaw(C& canvas,int x,int y,const std::string& text,int scale,uint32_t argb) {

        const std::string& s = text;
        for (size_t i = 0; i < s.size(); i++)
        {
            char c = s[i];
            // Spanish's letters (UTF-8 C3 xx): the plain capital with its mark above the line.
            int mark = 0; // 1 acute, 2 tilde, 3 grave
            size_t next = i;
            const uint32_t code = UiLang::Next(s, next);
            if (code >= 128 && !UiLang::LegacyAccent(code))
            {
                if (const auto* glyph = UiFont::Find(code))
                {
                    for (int row=0;row<14;++row) for(int col=0;col<12;++col)
                        if(glyph->rows[row] & (1u << (11-col))) canvas.Fill(x+col*scale,y+(row-3)*scale,scale,scale,argb);
                }
                else
                {
                    // A visible missing-glyph box, rather than silently dropping text.
                    canvas.Fill(x,y,5*scale,scale,argb);canvas.Fill(x,y+6*scale,5*scale,scale,argb);
                    canvas.Fill(x,y,scale,7*scale,argb);canvas.Fill(x+4*scale,y,scale,7*scale,argb);
                }
                x += UiLang::Advance(code)*scale;i=next-1;continue;
            }
            if ((unsigned char)c == 0xC3 && i + 1 < s.size())
            {
                static const char from[] = "\x81\x89\x8D\x93\x9A\x91\xA1\xA9\xAD\xB3\xBA\xB1", to[] = "AEIOUNAEIOUN";
                const char* p = strchr(from, s[++i]);
                c = p ? to[p - from] : '?';
                mark = p ? (c == 'N' ? 2 : 1) : 0;
                if (!p)
                {
                    static const char grave[] = "\x80\x88\x8C\x92\x99\xA0\xA8\xAC\xB2\xB9", plain[] = "AEIOUAEIOU";
                    if (const char* g = strchr(grave, s[i])) { c = plain[g - grave]; mark = 3; }
                }
            }
            else if ((unsigned char)c >= 0x80) continue; // (another letter's other bytes)
            // \x05: E with an acute accent (the É of POKÉMON): a whole E, the accent above the
            // line, two pixels slanting up to the right with a row's gap.
            if (c == '\x05')
            {
                c = 'E';
                mark = 1;
            }
            if (mark == 1)
            {
                canvas.Fill(x + 3 * scale, y - 3 * scale, scale, scale, argb);
                canvas.Fill(x + 2 * scale, y - 2 * scale, scale, scale, argb);
            }
            else if (mark == 3)
            {
                canvas.Fill(x + scale, y - 3 * scale, scale, scale, argb);
                canvas.Fill(x + 2 * scale, y - 2 * scale, scale, scale, argb);
            }
            else if (mark == 2)
            {
                canvas.Fill(x + 1 * scale, y - 2 * scale, scale, scale, argb);
                canvas.Fill(x + 2 * scale, y - 3 * scale, scale, scale, argb);
                canvas.Fill(x + 3 * scale, y - 2 * scale, scale, scale, argb);
                canvas.Fill(x + 4 * scale, y - 3 * scale, scale, scale, argb);
            }
            if (const uint8_t* g = C::Glyph(c))
                for (int row = 0; row < 7; row++)
                    for (int col = 0; col < 5; col++)
                        if (g[row] & (0x10 >> col))
                            canvas.Fill(x + col * scale, y + row * scale, scale, scale, argb);
            if (!C::Glyph(c)) if (const auto* glyph=UiFont::Find((unsigned char)c))
                for(int row=0;row<14;++row)for(int col=0;col<12;++col)
                    if(glyph->rows[row]&(1u<<(11-col)))canvas.Fill(x+col*scale,y+(row-3)*scale,scale,scale,argb);
            x += 6 * scale;
        }

}
template<class C> void Draw(C& canvas,int x,int y,const std::string& text,int scale,uint32_t colour)
{
    DrawRaw(canvas,x,y,UiLang::Tr(text),scale,colour);
}

}
