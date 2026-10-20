#pragma once

#include <cstdarg>
#include <cstddef>

namespace dm {

// Minimal vsnprintf replacement for the CRT-free DLL build. Supports
// %%, %c, %s, %d/%i, %u, %x/%X, %p and their 'l'-prefixed variants, with an
// optional zero-pad flag and field width ("%08x", "%2u"). Writes at most
// cap-1 bytes plus a NUL and returns the untruncated length, like C99
// vsnprintf. Everything the module logs goes through here.
//
// There is deliberately exactly one variadic entry point and one separately
// named va_list entry point. An overload PAIR (va_list + ellipsis) is
// dangerous on MSVC x86: va_list is char* there, so any call passing a
// char* as the first variadic argument would resolve to the va_list
// overload and treat that string pointer as the argument cursor.

int formatString(char* out, size_t cap, const char* fmt, ...);
int formatStringV(char* out, size_t cap, const char* fmt, va_list args);

}  // namespace dm
