#include "detour.h"
#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstring>
#include "logging.h"

namespace dm {
namespace {
struct SuspendedThreads { HANDLE handles[MAXIMUM_WAIT_OBJECTS]; int count; };
void resumeThreads(SuspendedThreads* threads) {
    for (int i = 0; i < threads->count; ++i) {
        ResumeThread(threads->handles[i]);
        CloseHandle(threads->handles[i]);
    }
    threads->count = 0;
}
bool suspendOtherThreads(SuspendedThreads* threads, unsigned char* site, size_t guardedLength) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    THREADENTRY32 entry = {};
    entry.dwSize = sizeof(entry);
    bool safe = true;
    for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID == GetCurrentProcessId() && entry.th32ThreadID != GetCurrentThreadId()) {
            if (threads->count == MAXIMUM_WAIT_OBJECTS) { safe = false; break; }
            HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, entry.th32ThreadID);
            if (!thread) {
                if (GetLastError() == ERROR_INVALID_PARAMETER) continue; // thread exited
                safe = false; break;
            }
            if (SuspendThread(thread) == static_cast<DWORD>(-1)) { CloseHandle(thread); safe = false; break; }
            threads->handles[threads->count++] = thread;
            CONTEXT context = {};
            context.ContextFlags = CONTEXT_CONTROL;
            if (!GetThreadContext(thread, &context)) { safe = false; break; }
            const uintptr_t ip = context.Eip;
            const uintptr_t first = reinterpret_cast<uintptr_t>(site);
            // EIP at the first byte can safely see the new branch. A stopped
            // thread inside overwritten instructions/function must run out first.
            if (ip > first && ip < first + guardedLength) { safe = false; break; }
        }
        entry.dwSize = sizeof(entry);
    }
    CloseHandle(snapshot);
    if (!safe) resumeThreads(threads);
    return safe;
}
}

bool installDetour(unsigned char* site, const unsigned char* expected, size_t length,
                   const void* target, bool useCall, size_t guardedLength) {
    if (!site || !expected || !target || length < 5 || length > 16) return false;
    unsigned char patch[16] = {};
    encodeRel32(patch, useCall ? 0xE8 : 0xE9, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(site)),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target)));
    for (size_t i = 5; i < length; ++i) patch[i] = 0x90;
    return installPatch(site, expected, length, patch, guardedLength);
}

bool installPatch(unsigned char* site, const unsigned char* expected, size_t length,
                  const unsigned char* patch, size_t guardedLength) {
    if (!site || !expected || !patch || length == 0 || length > 16) return false;
    if (guardedLength < length) guardedLength = length;
    SuspendedThreads threads = {};
    // Bounded installation retry, never an in-frame Reset retry or loader-lock wait.
    bool suspended = false;
    for (int i = 0; i < 8 && !suspended; ++i) {
        suspended = suspendOtherThreads(&threads, site, guardedLength);
        if (!suspended) Sleep(1);
    }
    if (!suspended) { DM_LOG("detour: could not reach safe patch boundary"); return false; }
    if (std::memcmp(site, expected, guardedLength) != 0) { resumeThreads(&threads); return false; }
    DWORD protection = 0;
    if (!VirtualProtect(site, length, PAGE_EXECUTE_READWRITE, &protection)) { resumeThreads(&threads); return false; }
    std::memcpy(site, patch, length);
    const BOOL flushed = FlushInstructionCache(GetCurrentProcess(), site, length);
    if (!flushed) {
        std::memcpy(site, expected, length);  // verified equal to the original above
        FlushInstructionCache(GetCurrentProcess(), site, length);
    }
    DWORD ignored = 0;
    VirtualProtect(site, length, protection, &ignored);
    resumeThreads(&threads);
    if (flushed) DM_LOG("detour: installed %u bytes at %p", static_cast<unsigned>(length), site);
    return flushed != FALSE;
}

bool installEntryDetour(unsigned char* site, const unsigned char* expected, size_t length,
                        const void* target, unsigned char* trampoline, size_t capacity) {
    if (!trampoline || length < 5 || length > 16 || capacity < length + 5 ||
        std::memcmp(site, expected, length) != 0) return false;
    // Caller provides dedicated VirtualAlloc storage, never a shared .data page.
    std::memcpy(trampoline, expected, length);
    trampoline[length] = 0xE9;
    const uint32_t back = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(site) + length) -
                          static_cast<uint32_t>(reinterpret_cast<uintptr_t>(trampoline) + length + 5);
    std::memcpy(trampoline + length + 1, &back, 4);
    DWORD old = 0;
    if (!VirtualProtect(trampoline, length + 5, PAGE_EXECUTE_READ, &old) ||
        !FlushInstructionCache(GetCurrentProcess(), trampoline, length + 5)) return false;
    return installDetour(site, expected, length, target, false);
}
}
