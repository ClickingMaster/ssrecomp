#define STB_IMAGE_IMPLEMENTATION
#include "shortcuts.h"
#include <iostream>

int main(int argc, char** argv)
{
    if (argc > 1)
    {
        for (int i = 1; i < argc; ++i) std::cout << argv[i] << "\n";
        return 0;
    }
    Launcher::Game game;
    game.code = "IREO";
    game.known = true;
    game.hasIcon = true;
    std::fill(std::begin(game.icon), std::end(game.icon), 0xff4080c0);
    Launcher::Game unknown;
    std::string where;
    const int made = Shortcuts::Make(".", {game, game, unknown}, where);
    std::cout << made << " " << where << "\n";
    return made == 1 ? 0 : 1;
}
