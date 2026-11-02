#pragma once
#include <SDL.h>
#include "ui_lang.h"
namespace UiLang
{
inline void DetectSystem()
{
    if (SDL_Locale* locales = SDL_GetPreferredLocales())
    {
        SetSystem(locales[0].language ? locales[0].language : "en");
        SDL_free(locales);
    }
}
}
