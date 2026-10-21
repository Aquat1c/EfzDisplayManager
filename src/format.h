#pragma once

#include <cstdarg>
#include <cstddef>

namespace dm {

// Minimal vsnprintf replacement for the CRT-free DLL build. Supports
// %%, %c, %s, %d/%i, %u, %x/%X, %p and their 'l'-prefixed variants, with an
// optional zero-pad flag and field width ("%08x", "%2u"). Writes at most
// cap-1 bytes plus a NUL and returns the untruncated length, like C99
// vsnprintf.
//
// Never overload these two: on MSVC x86 va_list is char*, so a call whose
// first variadic argument is a char* would pick the va_list overload.

int formatString(char* out, size_t cap, const char* fmt, ...);
int formatStringV(char* out, size_t cap, const char* fmt, va_list args);

}  // namespace dm
