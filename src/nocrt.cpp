// CRT-free runtime support for the shipped DLL. The module links with
// /NODEFAULTLIB so the import table contains only the Win32 APIs it calls
// (XP-clean; the stock v141 static CRT would import Vista+ FlsAlloc/
// InitializeCriticalSectionEx). The compiler still emits references to these
// five functions for struct copies, comparisons and inits, so provide byte-loop
// implementations. No recursion: these loops compile to plain loads/stores
// (they are marked noinline so the compiler cannot fuse a call to itself).

#include <cstddef>

// The float-using shader translation unit references these CRT conversion
// helpers; in a CRT-free link they must be satisfied manually. _dm_fltused
// is aliased to __fltused via the linker. __ftol2_sse converts the double
// in xmm0 to a 64-bit integer in edx:eax (truncation toward zero, matching
// the CRT helper the newer x86 toolchains emit for float->int64 paths).
extern "C" {
int _dm_fltused = 1;
__declspec(naked) void _ftol2_sse(void) {
    __asm {
        sub esp, 8
        movsd qword ptr [esp], xmm0
        fld qword ptr [esp]
        fistp qword ptr [esp]
        pop eax
        pop edx
        ret
    }
}
}

// Force the compiler to treat these as plain functions, not intrinsics, so
// the definitions below are legal.
#if defined(_MSC_VER) && !defined(__clang__)
#pragma function(memcpy, memset, memmove, memcmp, strlen)
#endif

extern "C" __declspec(noinline) void* memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = static_cast<unsigned char*>(dst);
    const unsigned char* s = static_cast<const unsigned char*>(src);
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

extern "C" __declspec(noinline) void* memset(void* dst, int c, size_t n) {
    unsigned char* d = static_cast<unsigned char*>(dst);
    for (size_t i = 0; i < n; ++i) d[i] = static_cast<unsigned char>(c);
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

extern "C" __declspec(noinline) int memcmp(const void* left, const void* right,
                                          size_t n) {
    const unsigned char* a = static_cast<const unsigned char*>(left);
    const unsigned char* b = static_cast<const unsigned char*>(right);
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) return static_cast<int>(a[i]) - static_cast<int>(b[i]);
    }
    return 0;
}

extern "C" __declspec(noinline) size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}
