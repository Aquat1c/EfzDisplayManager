#include "logging.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace dm {
namespace log {

namespace {

CRITICAL_SECTION g_lock = {};
bool g_lock_ready = false;
FILE* g_file = nullptr;

void lock() {
    if (g_lock_ready) EnterCriticalSection(&g_lock);
}

void unlock() {
    if (g_lock_ready) LeaveCriticalSection(&g_lock);
}

}  // namespace

bool init(HMODULE module) {
    if (!g_lock_ready) {
        InitializeCriticalSection(&g_lock);
        g_lock_ready = true;
    }

    if (g_file) return true;

    wchar_t module_path[MAX_PATH];
    if (GetModuleFileNameW(module, module_path, MAX_PATH) == 0) return false;

    wchar_t* last_slash = wcsrchr(module_path, L'\\');
    if (!last_slash) return false;
    *last_slash = L'\0';

    wchar_t log_path[MAX_PATH];
    if (swprintf_s(log_path, L"%ls\\DisplayManager.log", module_path) <= 0) return false;

    if (_wfopen_s(&g_file, log_path, L"a") != 0) {
        g_file = nullptr;
        return false;
    }
    return true;
}

void shutdown() {
    lock();
    if (g_file) {
        fclose(g_file);
        g_file = nullptr;
    }
    unlock();
}

void write(const char* format, ...) {
    lock();

    if (g_file) {
        SYSTEMTIME st;
        GetLocalTime(&st);

        va_list args;
        va_start(args, format);
        std::fprintf(g_file, "[%02u:%02u:%02u.%03u] ",
                     st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        std::vfprintf(g_file, format, args);
        std::fprintf(g_file, "\n");
        std::fflush(g_file);
        va_end(args);
    }

    unlock();
}

}  // namespace log
}  // namespace dm
