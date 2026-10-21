#pragma once

#include <windows.h>

namespace dm {
namespace log {

// Opens (appends to) DisplayManager.log next to the loaded module. Safe to
// call multiple times; returns false when the log file cannot be created, in
// which case write() calls become no-ops. Pure Win32 - no CRT file I/O - so
// the import table stays XP-clean and toolchain-agnostic.
bool init(HMODULE module);

// Flushes and closes the log file, if open.
void shutdown();

// Appends one line with a timestamp. Used only for startup/teardown,
// admission diagnostics, and throttled hook observations; never per-frame.
void write(const char* format, ...);

// True when init() succeeded (gates verbose observation logging).
bool enabled();

}  // namespace log
}  // namespace dm

#define DM_LOG(...) ::dm::log::write(__VA_ARGS__)
