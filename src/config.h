#pragma once

#include <windows.h>

#include "geometry.h"

namespace dm {

struct Config {
    int enabled;               // [Display] Enabled (default 1)
    int logFirst;              // [Display] LogFirst (default 8)
    int logEvery;              // [Display] LogEvery (default 600)
    ScaleMode mode;            // [Display] Mode: FitToScreen | IntegerScaling |
                               // CustomResolution (default FitToScreen)
    int integerScale;          // [Display] IntegerScale: 0/auto or explicit
                               // multiple (default 0 = largest that fits)
    int customWidth;           // [Display] CustomWidth (default 640)
    int customHeight;          // [Display] CustomHeight (default 480)
    int sharpness;               // [Display] Sharpness 10-40 (=1.0-4.0,
                                  // default 20); Sharp filter only
    unsigned long filter;      // [Display] Filter: Linear (keep the wrapper's
                               // value) | Point | Sharp | xBR (default
                               // Linear)
    unsigned long backgroundColor;  // [Display] BackgroundColor RRGGBB hex
                                    // (default 000000)
    int borderlessFullscreen;      // [Display] FullscreenMode: 0 = the
                                  // Alt+Enter/F11 target is EXCLUSIVE
                                  // fullscreen (default), 1 = BORDERLESS
    int fullscreenDelayMs;        // [Display] test/automation: delay before
                                  // the startup exclusive request (ms)
    int fullscreen;               // [Display] Fullscreen (default 0): start
                                  // in exclusive fullscreen at the desktop
                                  // mode (F11 toggles)
    int fullscreenHotkey;         // [Display] FullscreenHotkey (default 1):
                                  // F11 toggles exclusive fullscreen
    long vsync;                   // [Display] VSync: -1 keep the wrapper's
                                  // presentation interval, 0 immediate,
                                  // 1 vsync (default -1)
    int compatLayer;              // [Display] CompatLayer (default 1):
                                  // use owner callbacks for known overlays;
                                  // disabled + loaded overlay blocks resets
    int resizeBackBuffer;           // [Display] ResizeBackBuffer (default 1):
                                    // make the backbuffer follow window
                                    // maximize/restore/resize; Revival's
                                    // EfzRevival.ini [Global] sizes stay
                                    // authoritative at the original size
    int windowAdjustStyle;          // [Display] WindowAdjustStyle (default 1):
                                    // enable the maximize box on the
                                    // wrapper's fixed-size window
    int windowDragResize;           // [Display] WindowDragResize (default 1):
                                    // add a sizing border so the window can
                                    // be dragged; NOTE live drag-resize runs
                                    // Win32's modal size loop and pauses the
                                    // game until the drag ends - maximize/
                                    // restore/snap do not pause
};

// Reads DisplayManager.ini next to the given module; missing file means
// defaults. INI access uses GetPrivateProfile* only (XP-safe).
Config loadConfig(HMODULE module);

}  // namespace dm
