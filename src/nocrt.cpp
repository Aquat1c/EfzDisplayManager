// CRT-free runtime support for the shipped DLL. The module links with
// /NODEFAULTLIB so the import table contains only the Win32 APIs it calls
// (XP-clean; the stock v141 static CRT would import Vista+ FlsAlloc/
// InitializeCriticalSectionEx). The compiler still emits references to these
// four functions for struct copies and inits, so provide byte-loop
// implementations. No recursion: these loops compile to plain loads/stores
// (they are marked noinline so the compiler cannot fuse a call to itself).

#include <cstddef>

// The float-using translation units (shaders, geometry) reference this CRT
// startup symbol; in a CRT-free link it must be satisfied manually. It is
// defined with a distinct name and aliased via the linker.
extern "C" int _dm_fltused = 1;

// Force the compiler to treat these as plain functions, not intrinsics, so
// the definitions below are legal.
#pragma function(memcpy, memset, memmove, strlen)

extern "C" __declspec(noinline) void* memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = static_cast<unsigned char*>(dst);
    const unsigned char* s = static_cast<const unsigned char*>(src);
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

extern "C" __declspec(noinline) void* memset(void* dst, int c, size_t n) {
    unsigned char* d = static_cast<unsigned char*>(dst);
    for (size_t i = 0; i < n; ++i) d[i] = unsigned char(c);
    return dst;
}

extern "C" __declspec(noinline) void* memmove(void* dst, const void* src, size_t n) {
    unsigned char* d = static_cast<unsigned char*>(dst);
    const unsigned char* s = static_cast<const unsigned char*>(src);
    if (d < s) {
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; --i) d[i - 1] = s[i - 1];
    }
    return dst;
}

extern "C" __declspec(noinline) size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}
