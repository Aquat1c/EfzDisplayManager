// Manual legacy-owner negative fixture; never shipped. This deliberately has
// no EFZ_QueryOverlayResetApiV1 export. DisplayManager must detect ResetProbe.dll
// and refuse voluntary Reset before destroying any owner resources. This is not
// a production D3D9 failure-injection harness (it uses a worker thread and a
// fixed split-J address); use only an isolated matching test game copy.
// Log: ResetProbe.log next to the DLL.

#include <windows.h>
#include <d3d9.h>

#include <cstdio>

static FILE* g_log = nullptr;

static void L(const char* fmt, ...) {
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    fputc('\n', g_log);
    fflush(g_log);
    va_end(ap);
}

static DWORD WINAPI probeThread(void*) {
    wchar_t dir[MAX_PATH];
    GetModuleFileNameW(nullptr, dir, MAX_PATH);  // placeholder, replaced below
    HMODULE self = nullptr;
    // Obtain our own module path via the thread's start address trick is
    // overkill: use GetModuleHandleEx on our export instead.
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&probeThread), &self);
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(self, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) *slash = L'\0';
    wchar_t logPath[MAX_PATH];
    swprintf(logPath, MAX_PATH, L"%s\\ResetProbe.log", path);
    if (_wfopen_s(&g_log, logPath, L"w") != 0) g_log = nullptr;
    L("ResetProbe: attached");

    HMODULE ddraw = nullptr;
    for (int i = 0; i < 480 && !ddraw; ++i) {
        ddraw = GetModuleHandleA("DDRAW.dll");
        if (!ddraw) Sleep(250);
    }
    if (!ddraw) {
        L("ResetProbe: DDRAW.dll never appeared");
        return 0;
    }
    L("ResetProbe: DDRAW.dll at %p", ddraw);

    // Renderer-split J device global (renderer object +0x44).
    IDirect3DDevice9* dev = nullptr;
    for (int i = 0; i < 480; ++i) {
        dev = *reinterpret_cast<IDirect3DDevice9**>(
            reinterpret_cast<unsigned char*>(ddraw) + 0xDF2C4);
        if (dev) break;
        Sleep(250);
    }
    if (!dev) {
        L("ResetProbe: device global never appeared");
        return 0;
    }
    L("ResetProbe: device %p - manager must block voluntary Reset for this legacy owner", dev);
    Sleep(3000);

    IDirect3DTexture9* tex = nullptr;
    HRESULT hr = dev->CreateTexture(64, 64, 1, 0, D3DFMT_A8R8G8B8,
                                    D3DPOOL_DEFAULT, &tex, nullptr);
    L("ResetProbe: CreateTexture(DEFAULT pool) hr=%08lx tex=%p - holding it",
      static_cast<unsigned long>(hr), static_cast<void*>(tex));
    if (FAILED(hr)) tex = nullptr;

    Sleep(INFINITE);
    if (tex) tex->Release();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        DWORD tid = 0;
        HANDLE t = CreateThread(nullptr, 0, probeThread, nullptr, 0, &tid);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
