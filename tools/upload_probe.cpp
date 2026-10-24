#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <mmsystem.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

// Standalone GPU experiment: this 64 Hz clock is never used by the game.
// Run from an x86 developer prompt; link d3d9.lib user32.lib winmm.lib.
namespace {
template<class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T* operator->() const { return p; }
};
void check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        std::fprintf(stderr, "%s failed: 0x%08lx\n", operation, static_cast<unsigned long>(hr));
        throw std::runtime_error(operation);
    }
}
LONGLONG now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
LRESULT CALLBACK windowProc(HWND w, UINT m, WPARAM a, LPARAM b) {
    if (m == WM_KEYDOWN && a == VK_ESCAPE) { PostQuitMessage(0); return 0; }
    if (m == WM_CLOSE) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(w, m, a, b);
}
bool pump() {
    MSG m;
    while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) {
        if (m.message == WM_QUIT) return false;
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    return true;
}
unsigned pixel(unsigned frame, unsigned x, unsigned y) {
    return ((frame & 255u) << 16) | (((x ^ frame) & 255u) << 8) | ((y ^ (frame >> 8)) & 255u);
}
struct Row {
    unsigned frame;
    LONGLONG start, lockEnd, fillEnd, unlockEnd, copyEnd, beginEnd, endEnd, stretchEnd, presentEnd;
    HRESULT lock, unlock, copy, begin, end, stretch, present;
};
struct Window {
    HWND value = nullptr;
    ~Window() { if (value) DestroyWindow(value); }
};
struct Timer {
    bool enabled = timeBeginPeriod(1) == TIMERR_NOERROR;
    ~Timer() { if (enabled) timeEndPeriod(1); }
};
}

int main(int argc, char** argv) {
    if (argc != 6 || (std::strcmp(argv[1], "offscreen") && std::strcmp(argv[1], "dynamic")) ||
        (std::strcmp(argv[2], "discard") && std::strcmp(argv[2], "copy"))) {
        std::fprintf(stderr, "usage: upload_probe offscreen|dynamic discard|copy refresh seconds output.csv\n"
                             "refresh=0 uses desktop; seconds=1..10; Escape aborts\n");
        return 2;
    }
    const bool dynamic = !std::strcmp(argv[1], "dynamic");
    const bool copySwap = !std::strcmp(argv[2], "copy");
    char* tail = nullptr;
    const unsigned requestedHz = std::strtoul(argv[3], &tail, 10);
    if (!*argv[3] || *tail || requestedHz > 1000) return 2;
    const unsigned seconds = std::strtoul(argv[4], &tail, 10);
    if (!*argv[4] || *tail || seconds < 1 || seconds > 10) return 2;
    try {
        Com<IDirect3D9> d3d;
        d3d.p = Direct3DCreate9(D3D_SDK_VERSION);
        if (!d3d.p) throw std::runtime_error("Direct3DCreate9");
        D3DADAPTER_IDENTIFIER9 id = {};
        check(d3d->GetAdapterIdentifier(0, 0, &id), "GetAdapterIdentifier");
        D3DDISPLAYMODE mode = {};
        check(d3d->GetAdapterDisplayMode(0, &mode), "GetAdapterDisplayMode");
        const unsigned hz = requestedHz ? requestedHz : mode.RefreshRate;
        bool foundMode = false;
        for (UINT i = 0; i < d3d->GetAdapterModeCount(0, mode.Format); ++i) {
            D3DDISPLAYMODE m = {};
            check(d3d->EnumAdapterModes(0, mode.Format, i, &m), "EnumAdapterModes");
            if (m.Width == mode.Width && m.Height == mode.Height && m.RefreshRate == hz) foundMode = true;
        }
        if (!foundMode) throw std::runtime_error("requested desktop-size exclusive refresh unavailable");
        D3DCAPS9 caps = {};
        check(d3d->GetDeviceCaps(0, D3DDEVTYPE_HAL, &caps), "GetDeviceCaps");
        const HRESULT dynamicFormat = d3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, mode.Format,
            D3DUSAGE_DYNAMIC, D3DRTYPE_TEXTURE, D3DFMT_X8R8G8B8);
        std::printf("adapter=%s driver=%s desktop=%ux%u@%u requested=%u source=%s swap=%s\n"
            "Caps2=0x%08lx DevCaps2=0x%08lx dynamic_format_hr=0x%08lx\n",
            id.Description, id.Driver, mode.Width, mode.Height, mode.RefreshRate, hz, argv[1], argv[2],
            caps.Caps2, caps.DevCaps2, static_cast<unsigned long>(dynamicFormat));
        if (dynamic) {
            check(dynamicFormat, "CheckDeviceFormat dynamic");
            if (!(caps.Caps2 & D3DCAPS2_DYNAMICTEXTURES) ||
                !(caps.DevCaps2 & D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES))
                throw std::runtime_error("required dynamic texture/StretchRect caps unavailable");
        }
        WNDCLASSA wc = {};
        wc.lpfnWndProc = windowProc;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.lpszClassName = "DmUploadProbe";
        if (!RegisterClassA(&wc)) throw std::runtime_error("RegisterClassA");
        Window window;
        window.value = CreateWindowExA(0, wc.lpszClassName, "D3D9 upload probe (Escape stops)",
            WS_POPUP | WS_VISIBLE, 0, 0, mode.Width, mode.Height, nullptr, nullptr, wc.hInstance, nullptr);
        if (!window.value) throw std::runtime_error("CreateWindowExA");
        SetForegroundWindow(window.value);
        D3DPRESENT_PARAMETERS pp = {};
        pp.BackBufferWidth = mode.Width;
        pp.BackBufferHeight = mode.Height;
        pp.BackBufferFormat = mode.Format;
        pp.BackBufferCount = 1;
        pp.SwapEffect = copySwap ? D3DSWAPEFFECT_COPY : D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = window.value;
        pp.Windowed = FALSE;
        pp.FullScreen_RefreshRateInHz = hz;
        pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        Com<IDirect3DDevice9> dev;
        check(d3d->CreateDevice(0, D3DDEVTYPE_HAL, window.value, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &pp, &dev.p), "CreateDevice exclusive IMMEDIATE");
        Com<IDirect3DTexture9> texture;
        Com<IDirect3DSurface9> upload, stage, backbuffer;
        if (dynamic) {
            check(dev->CreateTexture(640, 480, 1, D3DUSAGE_DYNAMIC, D3DFMT_X8R8G8B8,
                D3DPOOL_DEFAULT, &texture.p, nullptr), "CreateTexture");
            check(texture->GetSurfaceLevel(0, &upload.p), "GetSurfaceLevel");
        } else {
            check(dev->CreateOffscreenPlainSurface(640, 480, D3DFMT_X8R8G8B8,
                D3DPOOL_DEFAULT, &upload.p, nullptr), "CreateOffscreenPlainSurface");
        }
        check(dev->CreateRenderTarget(640, 480, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0,
            TRUE, &stage.p, nullptr), "CreateRenderTarget lockable");
        check(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer.p), "GetBackBuffer");
        LARGE_INTEGER frequency;
        if (!QueryPerformanceFrequency(&frequency)) throw std::runtime_error("QueryPerformanceFrequency");
        Timer timer;
        std::vector<Row> rows;
        rows.reserve(seconds * 64 + 1);
        const LONGLONG origin = now();
        bool allCallsOk = true;
        for (unsigned frame = 1; frame <= seconds * 64; ++frame) {
            const LONGLONG due = origin + (static_cast<LONGLONG>(frame - 1) * frequency.QuadPart) / 64;
            while (now() < due) {
                if (due - now() > frequency.QuadPart / 500) Sleep(1);
                else YieldProcessor();
            }
            if (!pump()) break;
            if (now() - origin >= static_cast<LONGLONG>(seconds) * frequency.QuadPart) break;
            Row r = {};
            r.frame = frame;
            D3DLOCKED_RECT locked = {};
            r.start = now();
            r.lock = upload->LockRect(&locked, nullptr, dynamic ? D3DLOCK_DISCARD : 0);
            r.lockEnd = now();
            if (SUCCEEDED(r.lock)) {
                for (unsigned y = 0; y != 480; ++y) {
                    unsigned* out = reinterpret_cast<unsigned*>(static_cast<unsigned char*>(locked.pBits) + y * locked.Pitch);
                    for (unsigned x = 0; x != 640; ++x) out[x] = pixel(frame, x, y);
                }
            }
            r.fillEnd = now();
            r.unlock = SUCCEEDED(r.lock) ? upload->UnlockRect() : r.lock;
            r.unlockEnd = now();
            r.copy = SUCCEEDED(r.unlock) ? dev->StretchRect(upload.p, nullptr, stage.p, nullptr, D3DTEXF_NONE) : r.unlock;
            r.copyEnd = now();
            r.begin = SUCCEEDED(r.copy) ? dev->BeginScene() : r.copy;
            r.beginEnd = now();
            r.end = SUCCEEDED(r.begin) ? dev->EndScene() : r.begin;
            r.endEnd = now();
            r.stretch = SUCCEEDED(r.end) ? dev->StretchRect(stage.p, nullptr, backbuffer.p, nullptr, D3DTEXF_POINT) : r.end;
            r.stretchEnd = now();
            r.present = SUCCEEDED(r.stretch) ? dev->Present(nullptr, nullptr, nullptr, nullptr) : r.stretch;
            r.presentEnd = now();
            rows.push_back(r);
            if (FAILED(r.present)) { allCallsOk = false; break; }
        }
        const LONGLONG measuredEnd = now();
        // Readback happens after measurement and may wait; no GPU wait/query or extra lock occurs in the frame loop.
        int identity = 0;
        if (allCallsOk && !rows.empty()) {
            Com<IDirect3DSurface9> readback;
            check(dev->CreateOffscreenPlainSurface(640, 480, D3DFMT_X8R8G8B8,
                D3DPOOL_SYSTEMMEM, &readback.p, nullptr), "CreateOffscreenPlainSurface readback");
            check(dev->GetRenderTargetData(stage.p, readback.p), "GetRenderTargetData after measurement");
            D3DLOCKED_RECT read = {};
            check(readback->LockRect(&read, nullptr, D3DLOCK_READONLY), "LockRect readback after measurement");
            identity = 1;
            for (unsigned y = 0; y != 480; ++y) {
                const unsigned* in = reinterpret_cast<unsigned*>(static_cast<unsigned char*>(read.pBits) + y * read.Pitch);
                for (unsigned x = 0; x != 640; ++x)
                    if ((in[x] & 0xffffffu) != pixel(rows.back().frame, x, y)) identity = -1;
            }
            check(readback->UnlockRect(), "UnlockRect readback");
        }
        FILE* file = nullptr;
        if (fopen_s(&file, argv[5], "wb") || !file) throw std::runtime_error("open CSV");
        std::fprintf(file, "frame,qpc_frequency,start_qpc,lock_end_qpc,fill_end_qpc,unlock_end_qpc,copy_end_qpc,begin_end_qpc,end_end_qpc,stretch_end_qpc,present_end_qpc,lock_hr,unlock_hr,copy_hr,begin_hr,end_hr,stretch_hr,present_hr,present_attempted,present_submitted,image_frame,submit_image_age_frames\n");
        unsigned attempts = 0, submissions = 0;
        for (const Row& r : rows) {
            const bool attempted = SUCCEEDED(r.stretch);
            const bool submitted = attempted && SUCCEEDED(r.present);
            attempts += attempted;
            submissions += submitted;
            std::fprintf(file, "%u,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,%lld,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx,%u,%u,%u,%d\n",
                r.frame, frequency.QuadPart, r.start, r.lockEnd, r.fillEnd, r.unlockEnd, r.copyEnd,
                r.beginEnd, r.endEnd, r.stretchEnd, r.presentEnd, static_cast<unsigned long>(r.lock),
                static_cast<unsigned long>(r.unlock), static_cast<unsigned long>(r.copy),
                static_cast<unsigned long>(r.begin), static_cast<unsigned long>(r.end),
                static_cast<unsigned long>(r.stretch), static_cast<unsigned long>(r.present),
                attempted, submitted, r.frame, submitted ? 0 : -1);
        }
        const bool writeOk = !std::ferror(file);
        const int closeResult = std::fclose(file);
        if (!writeOk || closeResult) throw std::runtime_error("write CSV");
        std::printf("iterations=%zu attempts=%u submitted=%u elapsed_ms=%.3f same_image_readback=%s csv=%s\n",
            rows.size(), attempts, submissions, (measuredEnd - origin) * 1000.0 / frequency.QuadPart,
            identity == 1 ? "PASS" : identity == -1 ? "FAIL" : "NOT_TESTED", argv[5]);
        std::puts("Displayed images, driver queue age and input latency are NOT measured by this probe.");
        return allCallsOk && identity == 1 && attempts == submissions ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "probe failed: %s\n", e.what());
        return 1;
    }
}
