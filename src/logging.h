#pragma once

#include <windows.h>

namespace dm {
namespace log {

// Opens (appends to) DisplayManager.log next to the loaded module. Safe to
// call multiple times; returns false when the log file cannot be created, in
// which case DM_LOG calls become no-ops.
bool init(HMODULE module);

// Flushes and closes the log file, if open.
void shutdown();

// Appends one line with a timestamp. Used only for startup/teardown and
// admission diagnostics; never call this from a per-frame path.
void write(const char* format, ...);

}  // namespace log
}  // namespace dm

#define DM_LOG(...) ::dm::log::write(__VA_ARGS__)
