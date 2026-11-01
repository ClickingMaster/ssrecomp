#pragma once
// iPhone and iPad (tools/ios/build_all.sh). The game list is upright and a game is
// sideways, on a phone or a tablet. SDL reads its orientation hint when a window is
// made, and from iOS 16 a window already on screen does not turn unless asked.

// The list. Call before its window is made, and again once that window exists.
void IosOrientLauncher();
// A DS or GBA game. Call before its window is made. A window made later (the GBA
// game's own) is turned when it is shown.
void IosOrientGame();
