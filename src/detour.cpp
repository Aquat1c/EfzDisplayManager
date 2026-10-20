#include "detour.h"

#include <windows.h>
#include <tlhelp32.h>

#include <cstdint>

#include "logging.h"

namespace dm {

namespace {

struct PatchState {
    unsigned char* site = nullptr;
    unsigned char original[16] = {};
    size_t len = 0;
};
PatchState g_patch;

struct SuspendedThreads {
    HANDLE handles[MAXIMUM_WAIT_OBJECTS];
    int count = 0;
};

// Suspends every thread of the current process except the calling one.
// No thread may run while a multi-byte instruction at the flip call site is
// being replaced.
bool suspendAllOtherThreads(SuspendedThreads* out) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return false;

    DWORD me = GetCurrentThreadId();
    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    BOOL ok = Thread32First(snap, &te);
    while (ok) {
        if (te.th32OwnerProcessID == GetCurrentProcessId() &&
            te.th32ThreadID != me && out->count < MAXIMUM_WAIT_OBJECTS) {
            HANDLE h = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
            if (h) {
                if (SuspendThread(h) != (DWORD)-1) {
                    out->handles[out->count++] = h;
                } else {
                    CloseHandle(h);
                }
            }
        }
        te.dwSize = sizeof(te);
        ok = Thread32Next(snap, &te);
    }
    CloseHandle(snap);
    return true;
}

void resumeThreads(SuspendedThreads* st) {
    for (int i = 0; i < st->count; ++i) {
        ResumeThread(st->handles[i]);
        CloseHandle(st->handles[i]);
    }
    st->count = 0;
}

}  // namespace

bool installDetour(unsigned char* site, const unsigned char* expectedBytes,
                   size_t byteCount, const void* target, bool useCall) {
    if (byteCount < 5 || byteCount > 16) return false;
    if (g_patch.site != nullptr) {
        DM_LOG("detour: only one installed site is supported");
        return false;
    }

    for (size_t i = 0; i < byteCount; ++i) {
        if (site[i] != expectedBytes[i]) {
            DM_LOG("detour: site bytes mismatch at +%u (found %02x, expected %02x); "
                   "not patching",
                   unsigned(i), site[i], expectedBytes[i]);
            return false;
        }
    }

    ptrdiff_t rel = reinterpret_cast<const unsigned char*>(target) - (site + 5);
    if (rel < -0x80000000LL || rel > 0x7FFFFFFFLL) {
        DM_LOG("detour: target out of rel32 range");
        return false;
    }

    unsigned char patch[16];
    patch[0] = useCall ? 0xE8 : 0xE9;
    *reinterpret_cast<int32_t*>(patch + 1) = static_cast<int32_t>(rel);
    for (size_t i = 5; i < byteCount; ++i) patch[i] = 0x90;

    SuspendedThreads st;
    if (!suspendAllOtherThreads(&st)) {
        DM_LOG("detour: thread snapshot failed; not patching");
        return false;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(site, byteCount, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        DM_LOG("detour: VirtualProtect failed (%u)", unsigned(GetLastError()));
        resumeThreads(&st);
        return false;
    }
    for (size_t i = 0; i < byteCount; ++i) g_patch.original[i] = site[i];
    for (size_t i = 0; i < byteCount; ++i) site[i] = patch[i];
    FlushInstructionCache(GetCurrentProcess(), site, byteCount);
    VirtualProtect(site, byteCount, oldProtect, &oldProtect);

    g_patch.site = site;
    g_patch.len = byteCount;

    resumeThreads(&st);
    DM_LOG("detour: installed %u-byte hook at %p (target %p)",
           unsigned(byteCount), site, target);
    return true;
}

bool removeDetour(unsigned char* site) {
    if (g_patch.site != site || g_patch.site == nullptr) return false;

    SuspendedThreads st;
    if (!suspendAllOtherThreads(&st)) {
        DM_LOG("detour: thread snapshot failed during removal; leaving hook in place");
        return false;
    }

    DWORD oldProtect = 0;
    if (VirtualProtect(site, g_patch.len, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        for (size_t i = 0; i < g_patch.len; ++i) site[i] = g_patch.original[i];
        FlushInstructionCache(GetCurrentProcess(), site, g_patch.len);
        VirtualProtect(site, g_patch.len, oldProtect, &oldProtect);
        g_patch.site = nullptr;
        g_patch.len = 0;
    }
    resumeThreads(&st);
    return g_patch.site == nullptr;
}

}  // namespace dm
