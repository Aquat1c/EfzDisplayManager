#include "upload_trace.h"
#include "detour.h"
#include "logging.h"
#include "renderer_reset.h"
#include "sha256.h"
#include <cstring>

extern "C" volatile unsigned long g_dmNativeCompleted = 0;
extern "C" void* g_dmNativeContinue = nullptr;

namespace dm {
namespace {
unsigned char* g_base = nullptr;
const DdrawProfile* g_profile = nullptr;
bool g_wantDynamic = false;
bool g_ready = false;
volatile LONG g_install = 0;
FrameTraceRecord* g_records = nullptr;
FrameTraceRecord* g_record = nullptr;
unsigned long g_capacity = 0, g_count = 0, g_iteration = 0;
HANDLE g_traceFile = INVALID_HANDLE_VALUE;
struct TraceHeader {
    unsigned long magic, version, recordBytes, count, pid, nativeWitness;
    LARGE_INTEGER frequency;
} g_header = {};
static_assert(sizeof(TraceHeader) == 32 && sizeof(FrameTraceRecord) == 184, "trace ABI");

DWORD WINAPI writeTrace(void*) {
    DWORD written = 0;
    const DWORD bytes = g_capacity * sizeof(FrameTraceRecord);
    const bool ok = WriteFile(g_traceFile, &g_header, sizeof(g_header), &written, nullptr) &&
        written == sizeof(g_header) &&
        WriteFile(g_traceFile, g_records, bytes, &written, nullptr) && written == bytes;
    CloseHandle(g_traceFile);
    g_traceFile = INVALID_HANDLE_VALUE;
    DM_LOG("trace: %s %lu iterations; display/input/driver-queue age not measured",
           ok ? "saved" : "write failed for", g_capacity);
    // Storage remains process-owned: the worker never races frame callbacks.
    return 0;
}

void result(TraceResult which, HRESULT hr) {
    if (g_record) g_record->result[which] = hr;
}
HRESULT sourceLock(IDirect3DSurface9* surface, D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags) {
    if (g_record) g_record->source = reinterpret_cast<unsigned long>(surface);
    uploadTraceMark(SourceLockBegin);
    const HRESULT hr = surface->LockRect(locked, rect, flags);
    uploadTraceMark(SourceLockEnd);
    result(SourceLockResult, hr);
    return hr;
}
HRESULT destinationLock(IDirect3DSurface9* surface, D3DLOCKED_RECT* locked, const RECT* rect, DWORD flags) {
    if (g_record) g_record->destination = reinterpret_cast<unsigned long>(surface);
    uploadTraceMark(UploadLockBegin);
    const HRESULT hr = rendererUploadLock(surface, locked, rect, flags);
    uploadTraceMark(UploadLockEnd);
    result(UploadLockResult, hr);
    if (g_record) g_record->dynamicUpload = rendererUploadActive() ? 1 : 0;
    return hr;
}
HRESULT destinationUnlock(IDirect3DSurface9* surface) {
    uploadTraceMark(UploadUnlockBegin);
    const HRESULT hr = rendererUploadUnlock(surface);
    uploadTraceMark(UploadUnlockEnd);
    result(UploadUnlockResult, hr);
    if (g_record && hr == D3D_OK && g_record->result[SourceLockResult] == D3D_OK &&
        g_record->result[UploadLockResult] == D3D_OK)
        g_record->uploadedIteration = g_record->iteration;
    return hr;
}
HRESULT sourceUnlock(IDirect3DSurface9* surface) {
    uploadTraceMark(SourceUnlockBegin);
    const HRESULT hr = surface->UnlockRect();
    uploadTraceMark(SourceUnlockEnd);
    result(SourceUnlockResult, hr);
    return hr;
}
}

extern "C" HRESULT __stdcall dmSourceLockPush(IDirect3DSurface9* s, D3DLOCKED_RECT* l, const RECT* r, DWORD f) {
    return sourceLock(s, l, r, f);
}
extern "C" HRESULT __stdcall dmDestinationLockPush(IDirect3DSurface9* s, D3DLOCKED_RECT* l, const RECT* r, DWORD f) {
    return destinationLock(s, l, r, f);
}
extern "C" HRESULT __cdecl dmSourceLockMov(IDirect3DSurface9* s, D3DLOCKED_RECT* l, const RECT* r, DWORD f) {
    return sourceLock(s, l, r, f);
}
extern "C" HRESULT __cdecl dmDestinationLockMov(IDirect3DSurface9* s, D3DLOCKED_RECT* l, const RECT* r, DWORD f) {
    return destinationLock(s, l, r, f);
}
extern "C" HRESULT __stdcall dmDestinationUnlockPush(IDirect3DSurface9* s) { return destinationUnlock(s); }
extern "C" HRESULT __stdcall dmSourceUnlockPush(IDirect3DSurface9* s) { return sourceUnlock(s); }
extern "C" HRESULT __cdecl dmDestinationUnlockMov(IDirect3DSurface9* s) { return destinationUnlock(s); }
extern "C" HRESULT __cdecl dmSourceUnlockMov(IDirect3DSurface9* s) { return sourceUnlock(s); }

extern "C" __declspec(naked) void dmSourceLockShim() {
    __asm {
        push [esp+16]
        push [esp+16]
        push [esp+16]
        push [esp+16]
        call dmSourceLockPush
        mov esi, [ebp+8]
        ret 16
    }
}
extern "C" __declspec(naked) void dmDestinationLockShim() {
    __asm {
        push [esp+16]
        push [esp+16]
        push [esp+16]
        push [esp+16]
        call dmDestinationLockPush
        xor edi, edi
        ret 16
    }
}
extern "C" HRESULT __stdcall dmUploadCopy(IDirect3DDevice9* dev, IDirect3DSurface9* src,
        const RECT* sr, IDirect3DSurface9* dst, const RECT* dr, D3DTEXTUREFILTERTYPE filter) {
    uploadTraceMark(UploadCopyBegin);
    const HRESULT hr = dev->StretchRect(rendererUploadSource(src), sr, dst, dr, filter);
    uploadTraceMark(UploadCopyEnd);
    result(UploadCopyResult, hr);
    return hr;
}

extern "C" __declspec(naked) void dmNativeCompletedShim() {
    __asm {
        mov byte ptr ds:[0790148h], al
        pushfd
        inc dword ptr [g_dmNativeCompleted]
        popfd
        jmp dword ptr [g_dmNativeContinue]
    }
}

namespace {
void nativeWitness() {
    auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (base != reinterpret_cast<unsigned char*>(0x400000)) return;
    wchar_t path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    unsigned char hash[32];
    static const unsigned char expectedHash[32] = {
        0x3a,0x16,0x16,0xd6,0xc7,0x6d,0x2e,0xea,0x08,0xa9,0x5b,0xb1,0x98,0xd8,0x53,0xe9,
        0x6c,0xb6,0xb9,0xb3,0x26,0x6e,0x27,0x7c,0xa1,0xd1,0x67,0x42,0x6d,0xd6,0x64,0x81};
    static const unsigned char expected[] = {0xa2,0x48,0x01,0x79,0x00};
    if (!length || length >= MAX_PATH || !sha256File(path, hash) ||
        std::memcmp(hash, expectedHash, 32) != 0) return;
    g_dmNativeContinue = base + 0x164a;
    if (installDetour(base + 0x1645, expected, sizeof(expected), &dmNativeCompletedShim, false))
        g_header.nativeWitness = 1;
}

struct Site {
    unsigned long rva;
    unsigned char bytes[6];
    unsigned length;
    const void* hook;
    unsigned char push;
};
bool installUploadHooks() {
    // Absent fixtures keep their existing rendering path; similar RVAs are not evidence.
    const bool push = g_profile == &rev2020Profile();
    if (!push && g_profile != &originalJProfile()) {
        DM_LOG("upload: this wrapper has no audited upload anchors; original path retained");
        return false;
    }
    const Site msvc[] = {
        {0x2000c,{0xff,0x50,0x34,0x8b,0x75,0x08},6,&dmSourceLockShim,0},
        {0x2001d,{0xff,0x50,0x34,0x33,0xff},5,&dmDestinationLockShim,0},
        {0x2006a,{0x8b,0x06,0x56,0xff,0x50,0x38},6,&dmDestinationUnlockPush,0x56},
        {0x20070,{0x8b,0x03,0x53,0xff,0x50,0x38},6,&dmSourceUnlockPush,0x53},
        {0x1fa4f,{0xff,0x90,0x88,0,0,0},6,&dmUploadCopy,0},
    };
    const Site mingw[] = {
        {0x27674,{0xff,0x50,0x34,0x83,0xec,0x10},6,&dmSourceLockMov,0},
        {0x2769f,{0xff,0x50,0x34,0x83,0xec,0x10},6,&dmDestinationLockMov,0},
        {0x27741,{0xff,0x50,0x38,0x83,0xec,0x04},6,&dmDestinationUnlockMov,0},
        {0x27754,{0xff,0x50,0x38,0x83,0xec,0x04},6,&dmSourceUnlockMov,0},
        {0x27868,{0xff,0x90,0x88,0,0,0},6,&dmUploadCopy,0},
    };
    const Site* sites = push ? msvc : mingw;
    for (unsigned i = 0; i < 5; ++i)
        if (std::memcmp(g_base + sites[i].rva, sites[i].bytes, sites[i].length)) {
            DM_LOG("upload: call-site bytes changed; original storage retained");
            return false;
        }
    for (unsigned i = 0; i < 5; ++i) {
        const Site& s = sites[i];
        unsigned char patch[6] = {0x90,0x90,0x90,0x90,0x90,0x90};
        const unsigned offset = s.push ? 1 : 0;
        if (s.push) patch[0] = s.push;
        encodeRel32(patch + offset, 0xe8, reinterpret_cast<uint32_t>(g_base + s.rva + offset),
                    reinterpret_cast<uint32_t>(s.hook));
        if (!installPatch(g_base + s.rva, s.bytes, s.length, patch)) return false;
    }
    rendererResetUseDynamicUpload(g_wantDynamic);
    DM_LOG("upload: original lock/unlock/copy calls hooked, dynamic=%u", g_wantDynamic ? 1u : 0u);
    return true;
}
}

void uploadTraceConfigure(HMODULE module, unsigned char* base, const DdrawProfile* profile,
                          bool dynamicUpload, unsigned frames) {
    g_base = base;
    g_profile = profile;
    g_wantDynamic = dynamicUpload;
    if (frames) {
        if (frames > 32768) frames = 32768;
        wchar_t path[MAX_PATH];
        const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
        wchar_t* slash = nullptr;
        if (length && length < MAX_PATH)
            for (wchar_t* p = path; *p; ++p) if (*p == L'\\') slash = p;
        static const wchar_t name[] = L"DisplayManager-trace.bin";
        if (slash && static_cast<size_t>(slash - path) + 1 + sizeof(name)/sizeof(name[0]) <= MAX_PATH &&
            QueryPerformanceFrequency(&g_header.frequency)) {
            lstrcpyW(slash + 1, name);
            g_records = static_cast<FrameTraceRecord*>(VirtualAlloc(nullptr, frames * sizeof(FrameTraceRecord),
                MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
            if (g_records)
                g_traceFile = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (g_traceFile != INVALID_HANDLE_VALUE) {
                g_capacity = frames;
                g_header.magic = 0x52544d44; // DMTR
                g_header.version = 2;
                g_header.recordBytes = sizeof(FrameTraceRecord);
                g_header.count = frames;
                g_header.pid = GetCurrentProcessId();
                nativeWitness();
                DM_LOG("trace: armed for %u iterations, native completed-update witness=%lu",
                       frames, g_header.nativeWitness);
            } else {
                DM_LOG("trace: allocation/file creation failed; recording disabled");
            }
        }
    }
    if (dynamicUpload || g_capacity) InterlockedExchange(&g_install, 1);
}

void uploadTraceFrameStart() {
    g_record = nullptr;
    if (InterlockedExchange(&g_install, 0)) {
        installUploadHooks();
        g_ready = true;
    }
    // The bootstrap thread publishes configuration only after it is complete.
    if (!g_ready) return;
    ++g_iteration;
    if (!g_capacity) return;
    if (g_count == g_capacity) {
        ++g_count;
        HANDLE worker = CreateThread(nullptr, 0, &writeTrace, nullptr, 0, nullptr);
        if (worker) CloseHandle(worker);
        else DM_LOG("trace: cannot start output worker; buffered data not written");
        return;
    }
    if (g_count > g_capacity) return;
    g_record = &g_records[g_count++];
    g_record->iteration = g_iteration;
    g_record->nativeCompleted = g_dmNativeCompleted;
    g_record->generation = rendererResetGeneration();
    g_record->windowed = *reinterpret_cast<unsigned long*>(g_base + g_profile->ppRva + 0x20);
    g_record->dynamicUpload = rendererUploadActive() ? 1 : 0;
    for (unsigned i = 0; i < TraceResultCount; ++i) g_record->result[i] = 0x7fffffff;
    uploadTraceMark(TraceEntry);
}
void uploadTraceMark(TracePoint point) {
    if (g_record) QueryPerformanceCounter(&g_record->tick[point]);
}
void uploadTracePresent(HRESULT hr) {
    uploadTraceMark(PresentEnd);
    if (!g_record) return;
    ++g_record->presentAttempts;
    g_record->result[PresentResult] = hr;
    if (hr == D3D_OK && g_record->result[UploadCopyResult] == D3D_OK &&
        g_record->result[SourceUnlockResult] == D3D_OK &&
        g_record->result[FinalCopyResult] == D3D_OK)
        g_record->submittedIteration = g_record->uploadedIteration;
}
void uploadTraceFinalCopy(HRESULT hr) { result(FinalCopyResult, hr); }
}
