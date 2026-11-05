// ROM-free regression: a click on the top screen's inset (Hybrid, bottom screen large, the inset
// over its corner) must not touch the bottom screen under it.
#include <cassert>
#include <cstdio>
#include "../frontend/layout.h"

int main()
{
    Layout l;
    l.mode = LayoutMode::Hybrid;
    l.swapped = true;
    SDL_Rect r[2];
    LayoutRects(l, 800, 600, r); // 4:3: no strip, the inset is over the lower right
    assert(r[0].w > 0 && r[0].x + r[0].w == r[1].x + r[1].w);
    int tx = -1, ty = -1;
    assert(!LayoutTouch(l, r, r[0].x + r[0].w / 2, r[0].y + r[0].h / 2, tx, ty));
    assert(LayoutTouch(l, r, r[1].x + 10, r[1].y + 10, tx, ty) && tx < 5 && ty < 5);
    l.swapped = false; // the bottom screen is the small one: all of it touches
    LayoutRects(l, 800, 600, r);
    assert(LayoutTouch(l, r, r[1].x + r[1].w / 2, r[1].y + r[1].h / 2, tx, ty) && tx == 128 && ty == 96);
    puts("layout touch passed");
}
