#include <cassert>
#include "launcher.h"
int main()
{
    UiLang::Set("en"); assert(UiLang::Tr("RESUME") == "RESUME");
    UiLang::Set("es"); assert(UiLang::Tr("RESUME") != "RESUME");
    UiLang::Set("it"); assert(UiLang::Tr("RESUME") != "RESUME");
    assert(Canvas::TextWidth("ÀÈÉÌÒÙ", 1) == 36);
    Canvas canvas; canvas.Resize(200, 40); canvas.Text(1, 10, "ÀÈÉÌÒÙ'", 1, 0xffffffff);
    int pixels = 0; for (auto p : canvas.px) pixels += p != 0; assert(pixels > 40);
    UiLang::SetSystem("es"); UiLang::Set("auto"); assert(UiLang::lang == 1);
    UiLang::Set("it"); UiLang::SetSystem("en"); assert(UiLang::lang == 2);
    UiLang::Set("auto"); assert(UiLang::lang == 0);
    const char* codes[] = {"IRBF","IRAF","IREF","IRDF","IPGF","IPKF","CPUF","IRAS","CPUS","IPGS","IPKS","ADAS","IPKI","APAF","APAS","APAD","ADAD","ADAI"};
    for (auto code : codes) { const auto* game = Launcher::Known(code); assert(game); assert(std::string(game->code) == code); }
    for (const auto& profile : RegionalProfiles::profiles) {
        const auto* game = Launcher::Known(profile.code); assert(game);
        assert(std::string(game->code) == profile.code);
        assert(BaseGameCode(profile.code) == profile.base);
    }
    assert(!Launcher::Known("IPKZ")); assert(BaseGameCode("IPKZ") == "IPKZ");
    assert(std::string(Launcher::Known("IPKF")->save) != Launcher::Known("IPKE")->save);
    assert(std::string(Launcher::Known("IPKS")->save) != Launcher::Known("IPKI")->save);
}
