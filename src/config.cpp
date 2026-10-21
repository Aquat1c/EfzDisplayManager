#include "config.h"

namespace dm {

namespace {

unsigned long parseHexColor(const wchar_t* text, unsigned long fallback) {
    if (!text || !text[0]) return fallback;
    unsigned long value = 0;
    int digits = 0;
    for (const wchar_t* p = text; *p && digits < 6; ++p) {
        const wchar_t c = *p;
        unsigned long d;
        if (c >= L'0' && c <= L'9') {
            d = static_cast<unsigned long>(c - L'0');
        } else if (c >= L'a' && c <= L'f') {
            d = static_cast<unsigned long>(c - L'a' + 10);
        } else if (c >= L'A' && c <= L'F') {
            d = static_cast<unsigned long>(c - L'A' + 10);
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
    c.logFirst = 6;
    c.logEvery = 0;
    c.mode = ScaleMode::Fit;
    c.integerScale = 0;
    c.customWidth = 640;
    c.customHeight = 480;
    c.filter = 0;
    c.sharpness = 20;
    c.backgroundColor = 0xFF000000UL;
    c.borderlessFullscreen = 1;
    c.fullscreenRefresh = 0;
    c.compatLayer = 1;
    c.fullscreenDelayMs = 0;
    c.fullscreen = 0;
    c.fullscreenHotkey = 1;
    c.hideCursor = 1;
    c.vsync = 0;
    c.lowLatencyWindow = 0;
    c.resizeBackBuffer = 1;
    c.windowAdjustStyle = 1;
    c.windowDragResize = 1;

    wchar_t ini[MAX_PATH];
    const DWORD length = GetModuleFileNameW(module, ini, MAX_PATH);
    // A truncated path is not NUL-terminated on XP.
    if (length == 0 || length >= MAX_PATH) return c;
    wchar_t* lastSlash = nullptr;
    for (wchar_t* p = ini; *p; ++p) {
        if (*p == L'\\') lastSlash = p;
    }
    static const wchar_t kName[] = L"DisplayManager.ini";
    if (!lastSlash ||
        static_cast<size_t>(lastSlash - ini) + 1 + sizeof(kName) / sizeof(kName[0]) > MAX_PATH) return c;
    lstrcpyW(lastSlash + 1, kName);

    auto readInt = [&](const wchar_t* key, int fallback) {
        return static_cast<int>(GetPrivateProfileIntW(L"Display", key, fallback, ini));
    };
    c.enabled = readInt(L"Enabled", c.enabled);
    c.logFirst = readInt(L"LogFirst", c.logFirst);
    c.logEvery = readInt(L"LogEvery", c.logEvery);
    if (c.logFirst < 0) c.logFirst = 0;
    if (c.logEvery < 0) c.logEvery = 0;
    c.integerScale = readInt(L"IntegerScale", c.integerScale);
    c.customWidth = readInt(L"CustomWidth", c.customWidth);
    c.customHeight = readInt(L"CustomHeight", c.customHeight);

    wchar_t text[32];
    GetPrivateProfileStringW(L"Display", L"Mode", L"FitToScreen", text, 32, ini);
    if (lstrcmpiW(text, L"IntegerScaling") == 0) {
        c.mode = ScaleMode::Integer;
    } else if (lstrcmpiW(text, L"CustomResolution") == 0) {
        c.mode = ScaleMode::Custom;
    }

    GetPrivateProfileStringW(L"Display", L"Filter", L"Linear", text, 32, ini);
    if (lstrcmpiW(text, L"Point") == 0) {
        c.filter = 1;  // D3DTEXF_POINT
    } else if (lstrcmpiW(text, L"Sharp") == 0) {
        c.filter = 100;  // shader pipeline: sharp-bilinear
    } else if (lstrcmpiW(text, L"xBR") == 0) {
        c.filter = 101;  // shader pipeline: xBR-lv2
    }
    c.sharpness = readInt(L"Sharpness", c.sharpness);
    if (c.sharpness < 10) c.sharpness = 10;
    if (c.sharpness > 40) c.sharpness = 40;

    c.fullscreenDelayMs = readInt(L"FullscreenDelayMs", c.fullscreenDelayMs);
    c.compatLayer = readInt(L"CompatLayer", c.compatLayer);
    GetPrivateProfileStringW(L"Display", L"FullscreenMode", L"Borderless", text, 32, ini);
    c.borderlessFullscreen = lstrcmpiW(text, L"Exclusive") != 0 && lstrcmpW(text, L"0") != 0;
    const int refresh = readInt(L"FullscreenRefresh", 0);
    c.fullscreenRefresh = refresh > 0 ? static_cast<unsigned long>(refresh) : 0;
    c.fullscreen = readInt(L"Fullscreen", c.fullscreen);
    c.fullscreenHotkey = readInt(L"FullscreenHotkey", c.fullscreenHotkey);
    c.hideCursor = readInt(L"HideCursor", c.hideCursor);
    c.vsync = readInt(L"VSync", 0) == 1 ? 1 : 0;  // old -1 (keep) maps to 0
    c.lowLatencyWindow = readInt(L"LowLatencyWindow", c.lowLatencyWindow) != 0;
    c.resizeBackBuffer = readInt(L"ResizeBackBuffer", c.resizeBackBuffer);
    c.windowAdjustStyle = readInt(L"WindowAdjustStyle", c.windowAdjustStyle);
    c.windowDragResize = readInt(L"WindowDragResize", c.windowDragResize);

    GetPrivateProfileStringW(L"Display", L"BackgroundColor", L"000000", text, 32, ini);
    c.backgroundColor = parseHexColor(text, c.backgroundColor);
    return c;
}

}  // namespace dm
