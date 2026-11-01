// Turns an iPhone or iPad upright for the game list and sideways for a game.
// Info.plist allows both (tools/ios/build_all.sh); this asks iOS to use one of them.
// SDL's hint alone does not turn a screen that is already up, which is how the list
// and a game share one app.
#import <UIKit/UIKit.h>
#include <SDL.h>
#include <cstdio>
#include "ios_orient.h"

static bool g_landscape = false;

static UIInterfaceOrientationMask Wanted()
{
    return g_landscape ? (UIInterfaceOrientationMaskLandscapeLeft | UIInterfaceOrientationMaskLandscapeRight)
                       : UIInterfaceOrientationMaskPortrait;
}

static bool AlreadyThere(UIInterfaceOrientation o)
{
    if (g_landscape) return UIInterfaceOrientationIsLandscape(o);
    return o == UIInterfaceOrientationPortrait;
}

// lock: the screen should stay in this orientation, so tell iOS again even when it
// already faces that way. A later resize does not, or a turn would ask forever.
static void Apply(bool lock)
{
    if (@available(iOS 13.0, *))
    {
        for (UIScene* s in UIApplication.sharedApplication.connectedScenes)
        {
            if (![s isKindOfClass:[UIWindowScene class]]) continue;
            UIWindowScene* scene = (UIWindowScene*)s;
            UIWindow* window = nil;
            for (UIWindow* w in scene.windows)
                if (w.isKeyWindow) { window = w; break; }
            if (!window && scene.windows.count) window = scene.windows.firstObject;
            if (!window) continue;
            const bool there = AlreadyThere(scene.interfaceOrientation);
            if (there && !lock) return;
            UIViewController* vc = window.rootViewController;
            if (@available(iOS 16.0, *))
            {
                // The public way to turn the screen to a chosen orientation.
                if (vc) [vc setNeedsUpdateOfSupportedInterfaceOrientations];
                if (there) return;
                UIWindowSceneGeometryPreferencesIOS* prefs =
                    [[UIWindowSceneGeometryPreferencesIOS alloc] initWithInterfaceOrientations:Wanted()];
                [scene requestGeometryUpdateWithPreferences:prefs errorHandler:^(NSError* err) {
                    fprintf(stderr, "[ios] could not turn the screen: %s\n", err.localizedDescription.UTF8String);
                }];
            }
            else
            {
                // iOS 14 and 15 have no public call that turns the screen against how the
                // device is held. This is the call those versions answer. The app is
                // sideloaded, so it is not going through App Store review.
                if (!there)
                {
                    const UIInterfaceOrientation o = g_landscape ? UIInterfaceOrientationLandscapeRight
                                                                : UIInterfaceOrientationPortrait;
                    [[UIDevice currentDevice] setValue:@(o) forKey:@"orientation"];
                }
                [UIViewController attemptRotationToDeviceOrientation];
            }
            return;
        }
    }
}

static int OnWindow(void*, SDL_Event* e)
{
    // A game's window (the GBA build makes its own) is shown after the hint is set.
    // Deferred: this runs inside SDL, and asking iOS to turn can call back in.
    if (e->type == SDL_WINDOWEVENT && e->window.event == SDL_WINDOWEVENT_SHOWN)
        dispatch_async(dispatch_get_main_queue(), ^{ Apply(false); });
    return 1;
}

static void Set(bool landscape)
{
    g_landscape = landscape;
    SDL_SetHint(SDL_HINT_ORIENTATIONS, landscape ? "LandscapeLeft LandscapeRight" : "Portrait");
    if (SDL_WasInit(SDL_INIT_VIDEO))
    {
        SDL_DelEventWatch(OnWindow, nullptr);
        SDL_AddEventWatch(OnWindow, nullptr);
        Apply(true);
    }
}

void IosOrientLauncher() { Set(false); }
void IosOrientGame() { Set(true); }
