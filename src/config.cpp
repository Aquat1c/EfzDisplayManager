#include "config.h"

namespace dm {

Config loadConfig(HMODULE module) {
    Config c;
    c.enabled = 1;
    c.logFirst = 8;
    c.logEvery = 600;

    char path[MAX_PATH];
    if (GetModuleFileNameA(module, path, MAX_PATH) == 0) return c;
    char* lastSlash = nullptr;
    for (char* p = path; *p; ++p) {
        if (*p == '\\') lastSlash = p;
    }
    if (!lastSlash) return c;
    *lastSlash = '\0';
    char ini[MAX_PATH];
    if (wsprintfA(ini, "%s\\DisplayManager.ini", path) <= 0) return c;

    c.enabled = GetPrivateProfileIntA("Display", "Enabled", c.enabled, ini);
    c.logFirst = GetPrivateProfileIntA("Display", "LogFirst", c.logFirst, ini);
    c.logEvery = GetPrivateProfileIntA("Display", "LogEvery", c.logEvery, ini);
    return c;
}

}  // namespace dm
