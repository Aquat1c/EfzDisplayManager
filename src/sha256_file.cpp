#include "sha256.h"

#include <windows.h>

namespace dm {

bool sha256File(const char* path, unsigned char out[32]) {
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    Sha256 h;
    // Keep the whole frame well under one page: larger stack frames call
    // the CRT's __chkstk, which does not exist in the CRT-free DLL build.
    unsigned char buf[1 << 11];
    for (;;) {
        DWORD got = 0;
        BOOL ok = ReadFile(f, buf, sizeof(buf), &got, nullptr);
        if (!ok || got == 0) {
            CloseHandle(f);
            if (!ok) return false;
            break;
        }
        h.update(buf, got);
    }
    h.finish(out);
    return true;
}

}  // namespace dm
