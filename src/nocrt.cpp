// CRT-free runtime support. The DLL links with /NODEFAULTLIB to stay XP-clean
// (the v141 static CRT imports Vista+ FlsAlloc/InitializeCriticalSectionEx).
// The compiler still emits calls to these five functions, so provide byte
// loops; noinline keeps the compiler from turning a loop into a call to itself.

#include <cstddef>

// Float code references __fltused; _dm_fltused is aliased to it via the
// linker. Keep float->int casts out of the DLL: /arch:IA32 turns them into
// calls to the CRT's __ftol2_sse/__ftol2 (argument on the x87 stack), which a
// CRT-free link cannot resolve.
extern "C" int _dm_fltused = 1;

// Not intrinsics, so the definitions below are legal.
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
