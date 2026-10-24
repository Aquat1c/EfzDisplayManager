#include "config.h"
#include "config_defaults.h"
#include "logging.h"

namespace dm {

namespace {

bool equal(const wchar_t* a, const wchar_t* b) { return lstrcmpiW(a, b) == 0; }

bool validSetting(const wchar_t* key, const wchar_t* value) {
    if (!value[0]) return false;
    if (equal(key, L"Mode"))
        return equal(value, L"FitToScreen") || equal(value, L"IntegerScaling") || equal(value, L"CustomResolution");
    if (equal(key, L"Filter"))
        return equal(value, L"Linear") || equal(value, L"Point") || equal(value, L"Sharp") || equal(value, L"xBR");
    if (equal(key, L"FullscreenMode"))
        return equal(value, L"Exclusive") || equal(value, L"Borderless") || equal(value, L"0") || equal(value, L"1");
    if (equal(key, L"BackgroundColor")) {
        unsigned count = 0;
        for (const wchar_t* p = value; *p; ++p, ++count)
            if (!((*p >= L'0' && *p <= L'9') || (*p >= L'a' && *p <= L'f') || (*p >= L'A' && *p <= L'F'))) return false;
        return count == 6;
    }
    if ((equal(key, L"DynamicUpload") || equal(key, L"VSync")) && equal(value, L"Auto")) return true;
    if (equal(key, L"VSync") && equal(value, L"-1")) return true;
    const wchar_t* p = value;
    if (*p == L'+') ++p;
    unsigned base = 10;
    if (p[0] == L'0' && (p[1] == L'x' || p[1] == L'X')) { base = 16; p += 2; }
    if (!*p) return false;
    unsigned long number = 0;
    for (; *p; ++p) {
        unsigned digit;
        if (*p >= L'0' && *p <= L'9') digit = *p - L'0';
        else if (*p >= L'a' && *p <= L'f') digit = *p - L'a' + 10;
        else if (*p >= L'A' && *p <= L'F') digit = *p - L'A' + 10;
        else return false;
        if (digit >= base || number > (0x7fffffffUL - digit) / base) return false;
        number = number * base + digit;
    }
    if (equal(key, L"Sharpness")) return number >= 10 && number <= 40;
    if (equal(key, L"TraceFrames")) return number <= 32768;
    if (equal(key, L"CustomWidth") || equal(key, L"CustomHeight")) return number > 0;
    if (equal(key, L"IntegerScale") || equal(key, L"LogFirst") || equal(key, L"LogEvery") ||
        equal(key, L"FullscreenRefresh") || equal(key, L"FullscreenDelayMs")) return true;
    return number <= 1;
}

void readSetting(const wchar_t* ini, const wchar_t* key, const wchar_t* fallback,
                 wchar_t* text, DWORD capacity) {
    const DWORD count = GetPrivateProfileStringW(L"Display", key, L"", text, capacity, ini);
    if (count == capacity - 1 || !validSetting(key, text)) lstrcpyW(text, fallback);
}

void repairConfig(const wchar_t* ini) {
    const DWORD attributes = GetFileAttributesW(ini);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND) {
            DM_LOG("config: settings path unavailable (%lu); using readable values/defaults", GetLastError());
            return;
        }
        // CREATE_NEW cannot overwrite a file created by another startup.
        HANDLE file = CreateFileW(ini, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            const bool ok = WriteFile(file, kDefaultIni, sizeof(kDefaultIni) - 1, &written, nullptr) &&
                written == sizeof(kDefaultIni) - 1;
            const DWORD error = ok ? 0 : GetLastError();
            CloseHandle(file);
            DM_LOG("config: %s DisplayManager.ini (error=%lu)", ok ? "created" : "could not finish creating", error);
            return;
        }
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) {
            DM_LOG("config: cannot create DisplayManager.ini (%lu); using defaults", GetLastError());
            return;
        }
    }
    unsigned repaired = 0;
    for (const auto& item : kIniDefaults) {
        wchar_t value[64];
        const DWORD count = GetPrivateProfileStringW(L"Display", item.key, L"", value, 64, ini);
        if (count < 63 && validSetting(item.key, value)) continue;
        if (!WritePrivateProfileStringW(L"Display", item.key, item.value, ini)) {
            DM_LOG("config: settings update failed (%lu); existing choices retained", GetLastError());
            return;
        }
        ++repaired;
    }
    if (repaired) DM_LOG("config: added/repaired %u settings; existing choices retained", repaired);
}

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

Config loadConfigFile(const wchar_t* ini, bool repair) {
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
    c.borderlessFullscreen = 0;
    c.fullscreenRefresh = 0;
    c.compatLayer = 1;
    c.fullscreenDelayMs = 0;
    c.fullscreen = 0;
    c.fullscreenHotkey = 1;
    c.hideCursor = 1;
    c.vsync = 0;
    c.lowLatencyWindow = 0;
    c.dynamicUpload = 1;
    c.traceFrames = 0;
    c.resizeBackBuffer = 1;
    c.windowAdjustStyle = 1;
    c.windowDragResize = 1;

    if (repair && ini) repairConfig(ini);
    if (!ini) return c;

    auto readInt = [&](const wchar_t* key, int fallback) {
        wchar_t text[64];
        const DWORD count = GetPrivateProfileStringW(L"Display", key, L"", text, 64, ini);
        if (count == 63 || !validSetting(key, text) || equal(text, L"Auto")) return fallback;
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
    readSetting(ini, L"Mode", L"FitToScreen", text, 32);
    if (lstrcmpiW(text, L"IntegerScaling") == 0) {
        c.mode = ScaleMode::Integer;
    } else if (lstrcmpiW(text, L"CustomResolution") == 0) {
        c.mode = ScaleMode::Custom;
    }

    readSetting(ini, L"Filter", L"Linear", text, 32);
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
    readSetting(ini, L"FullscreenMode", L"Exclusive", text, 32);
    c.borderlessFullscreen = lstrcmpiW(text, L"Exclusive") != 0 && lstrcmpW(text, L"0") != 0;
    const int refresh = readInt(L"FullscreenRefresh", 0);
    c.fullscreenRefresh = refresh > 0 ? static_cast<unsigned long>(refresh) : 0;
    c.fullscreen = readInt(L"Fullscreen", c.fullscreen);
    c.fullscreenHotkey = readInt(L"FullscreenHotkey", c.fullscreenHotkey);
    c.hideCursor = readInt(L"HideCursor", c.hideCursor);
    c.vsync = readInt(L"VSync", 0) == 1 ? 1 : 0;  // old -1 (keep) maps to 0
    c.dynamicUpload = readInt(L"DynamicUpload", 1) != 0;
    c.traceFrames = readInt(L"TraceFrames", 0);
    if (c.traceFrames < 0) c.traceFrames = 0;
    if (c.traceFrames > 32768) c.traceFrames = 32768;
    c.lowLatencyWindow = readInt(L"LowLatencyWindow", c.lowLatencyWindow) != 0;
    c.resizeBackBuffer = readInt(L"ResizeBackBuffer", c.resizeBackBuffer);
    c.windowAdjustStyle = readInt(L"WindowAdjustStyle", c.windowAdjustStyle);
    c.windowDragResize = readInt(L"WindowDragResize", c.windowDragResize);

    readSetting(ini, L"BackgroundColor", L"000000", text, 32);
    c.backgroundColor = parseHexColor(text, c.backgroundColor);
    return c;
}

Config loadConfig(HMODULE module, bool repair) {
    wchar_t ini[MAX_PATH];
    const DWORD length = GetModuleFileNameW(module, ini, MAX_PATH);
    // A truncated path is not NUL-terminated on XP.
    if (!length || length >= MAX_PATH) return loadConfigFile(nullptr, false);
    wchar_t* slash = nullptr;
    for (wchar_t* p = ini; *p; ++p) if (*p == L'\\') slash = p;
    static const wchar_t name[] = L"DisplayManager.ini";
    if (!slash || static_cast<size_t>(slash - ini) + 1 + sizeof(name)/sizeof(name[0]) > MAX_PATH)
        return loadConfigFile(nullptr, false);
    lstrcpyW(slash + 1, name);
    return loadConfigFile(ini, repair);
}

}  // namespace dm
