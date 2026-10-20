#pragma once

#include <windows.h>

namespace dm {

struct Config {
    int enabled;    // [Display] Enabled (default 1): 0 leaves the game untouched
    int logFirst;   // [Display] LogFirst hook calls (default 8)
    int logEvery;   // [Display] LogEvery Nth hook call (default 600)
};

// Reads DisplayManager.ini next to the given module; missing file means
// defaults. INI access uses GetPrivateProfileIntA only (XP-safe).
Config loadConfig(HMODULE module);

}  // namespace dm
