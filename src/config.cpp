#include "config.h"

#include "format.h"

namespace dm {

namespace {

unsigned long parseHexColor(const char* text, unsigned long fallback) {
    if (!text || !text[0]) return fallback;
    unsigned long value = 0;
    int digits = 0;
    for (const char* p = text; *p && digits < 6; ++p) {
        char c = *p;
        unsigned long d;
        if (c >= '0' && c <= '9') {
            d = unsigned long(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            d = unsigned long(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            d = unsigned long(c - 'A' + 10);
        } else {
            return fallback;
        }
        value = (value << 4) | d;
        ++digits;
    }
    if (digits == 0) return fallback;
    return 0xFF000000UL | (value & 0xFFFFFFUL);
}

}  // namespace

Config loadConfig(HMODULE module) {
    Config c;
    c.enabled = 1;
    c.logFirst = 8;
    c.logEvery = 600;
    c.mode = ScaleMode::Fit;
    c.integerScale = 0;
    c.customWidth = 640;
    c.customHeight = 480;
    c.filter = 0;                  // 0 = keep the wrapper's filter
    c.backgroundColor = 0xFF000000UL;
    c.borderlessFullscreen = 1;  // borderless default per user preference
    c.compatLayer = 1;
    c.fullscreenDelayMs = 0;
    c.fullscreen = 0;
    c.fullscreenHotkey = 1;
    c.vsync = -1;
    c.resizeBackBuffer = 1;
    c.windowAdjustStyle = 1;
    c.windowDragResize = 1;

    char path[MAX_PATH];
    if (GetModuleFileNameA(module, path, MAX_PATH) == 0) return c;
    char* lastSlash = nullptr;
    for (char* p = path; *p; ++p) {
        if (*p == '\\') lastSlash = p;
    }
    if (!lastSlash) return c;
    *lastSlash = '\0';
    char ini[MAX_PATH];
    if (dm::formatString(ini, sizeof(ini), "%s\\DisplayManager.ini", path) <= 0) {
        return c;
    }

    c.enabled = GetPrivateProfileIntA("Display", "Enabled", c.enabled, ini);
    c.logFirst = GetPrivateProfileIntA("Display", "LogFirst", c.logFirst, ini);
    c.logEvery = GetPrivateProfileIntA("Display", "LogEvery", c.logEvery, ini);
    c.integerScale =
        GetPrivateProfileIntA("Display", "IntegerScale", c.integerScale, ini);
    c.customWidth =
        GetPrivateProfileIntA("Display", "CustomWidth", c.customWidth, ini);
    c.customHeight =
        GetPrivateProfileIntA("Display", "CustomHeight", c.customHeight, ini);

    char text[32];
    GetPrivateProfileStringA("Display", "Mode", "FitToScreen", text,
                             sizeof(text), ini);
    if (lstrcmpiA(text, "IntegerScaling") == 0) {
        c.mode = ScaleMode::Integer;
    } else if (lstrcmpiA(text, "CustomResolution") == 0) {
        c.mode = ScaleMode::Custom;
    } else {
        c.mode = ScaleMode::Fit;
    }

    GetPrivateProfileStringA("Display", "Filter", "Linear", text, sizeof(text),
                             ini);
    if (lstrcmpiA(text, "Point") == 0) {
        c.filter = 1;  // D3DTEXF_POINT
    } else if (lstrcmpiA(text, "Sharp") == 0) {
        c.filter = 100;  // shader pipeline: sharp-bilinear
    } else if (lstrcmpiA(text, "xBR") == 0) {
        c.filter = 101;  // shader pipeline: xBR-lv2
    }
    c.sharpness = GetPrivateProfileIntA("Display", "Sharpness", 20, ini);
    if (c.sharpness < 10) c.sharpness = 10;
    if (c.sharpness > 40) c.sharpness = 40;

    c.fullscreenDelayMs = GetPrivateProfileIntA("Display", "FullscreenDelayMs",
                                                c.fullscreenDelayMs, ini);
    c.compatLayer = GetPrivateProfileIntA("Display", "CompatLayer",
                                          c.compatLayer, ini);
    c.borderlessFullscreen = GetPrivateProfileIntA(
        "Display", "FullscreenMode", c.borderlessFullscreen, ini);
    if (c.borderlessFullscreen != 0) c.borderlessFullscreen = 1;
    c.fullscreen =
        GetPrivateProfileIntA("Display", "Fullscreen", c.fullscreen, ini);
    c.fullscreenHotkey = GetPrivateProfileIntA("Display", "FullscreenHotkey",
                                               c.fullscreenHotkey, ini);
    c.vsync = GetPrivateProfileIntA("Display", "VSync", int(c.vsync), ini);
    c.resizeBackBuffer =
        GetPrivateProfileIntA("Display", "ResizeBackBuffer", c.resizeBackBuffer, ini);
    c.windowAdjustStyle =
        GetPrivateProfileIntA("Display", "WindowAdjustStyle", c.windowAdjustStyle, ini);
    c.windowDragResize =
        GetPrivateProfileIntA("Display", "WindowDragResize", c.windowDragResize, ini);

    GetPrivateProfileStringA("Display", "BackgroundColor", "000000", text,
                             sizeof(text), ini);
    c.backgroundColor = parseHexColor(text, c.backgroundColor);
    return c;
}

}  // namespace dm
