#include "logging.h"

#include <cstdarg>

#include "format.h"

namespace dm {
namespace log {

namespace {

CRITICAL_SECTION g_lock = {};
bool g_lockReady = false;
HANDLE g_file = INVALID_HANDLE_VALUE;

void lock() {
    if (g_lockReady) EnterCriticalSection(&g_lock);
}

void unlock() {
    if (g_lockReady) LeaveCriticalSection(&g_lock);
}

}  // namespace

bool init(HMODULE module) {
    if (!g_lockReady) {
        InitializeCriticalSection(&g_lock);
        g_lockReady = true;
    }
    if (g_file != INVALID_HANDLE_VALUE) return true;

    wchar_t modulePath[MAX_PATH];
    if (GetModuleFileNameW(module, modulePath, MAX_PATH) == 0) return false;

    wchar_t* lastSlash = nullptr;
    for (wchar_t* p = modulePath; *p; ++p) {
        if (*p == L'\\') lastSlash = p;
    }
    if (!lastSlash) return false;
    *lastSlash = L'\0';

    wchar_t logPath[MAX_PATH];
    int n = wsprintfW(logPath, L"%s\\DisplayManager.log", modulePath);
    if (n <= 0 || n >= MAX_PATH) return false;

    g_file = CreateFileW(logPath, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    return g_file != INVALID_HANDLE_VALUE;
}

void shutdown() {
    lock();
    if (g_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
    unlock();
}

bool enabled() {
    return g_file != INVALID_HANDLE_VALUE;
}

void write(const char* format, ...) {
    char line[512];
    SYSTEMTIME st;
    GetLocalTime(&st);
    int prefix = dm::formatString(line, sizeof(line) - 2,
                                  "[%02u:%02u:%02u.%03u] ", st.wHour,
                                  st.wMinute, st.wSecond, st.wMilliseconds);
    if (prefix < 0 || prefix >= int(sizeof(line)) - 4) return;

    va_list args;
    va_start(args, format);
    int body = dm::formatStringV(line + prefix, sizeof(line) - prefix - 2,
                                format, args);
    va_end(args);
    if (body < 0) return;

    int total = prefix + body;
    if (total >= int(sizeof(line)) - 2) total = sizeof(line) - 2 - 1;
    line[total] = '\r';
    line[total + 1] = '\n';

    lock();
    if (g_file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(g_file, line, total + 2, &written, nullptr);
    }
    unlock();
}

}  // namespace log
}  // namespace dm
