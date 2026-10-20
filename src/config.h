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
    unsigned long filter;      // [Display] Filter: Linear (keep the wrapper's
                               // value) | Point (override) (default Linear)
    unsigned long backgroundColor;  // [Display] BackgroundColor RRGGBB hex
                                    // (default 000000)
};

// Reads DisplayManager.ini next to the given module; missing file means
// defaults. INI access uses GetPrivateProfile* only (XP-safe).
Config loadConfig(HMODULE module);

}  // namespace dm
