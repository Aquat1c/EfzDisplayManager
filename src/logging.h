#pragma once

#include <windows.h>

namespace dm {
namespace log {

// Opens (appends to) DisplayManager.log next to the loaded module. Safe to
// call repeatedly; on failure write() is a no-op. Win32 only, no CRT file I/O.
bool init(HMODULE module);

// Closes the log file, if open.
void shutdown();

// Appends one timestamped line. Keep hook-path logging throttled.
void write(const char* format, ...);

// True when init() succeeded (gates verbose observation logging).
bool enabled();

}  // namespace log
}  // namespace dm

#define DM_LOG(...) ::dm::log::write(__VA_ARGS__)
