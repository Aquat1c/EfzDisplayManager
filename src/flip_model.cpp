// Flip-model presentation for windowed and borderless mode. A D3D9Ex FLIPEX
// swap chain never tears, and Windows can flip a borderless window straight
// to the screen, skipping the compositor's extra frame of delay.
//
// D3D9Ex rejects D3DPOOL_MANAGED, which other mods and D3DX (the wrapper's
// font) use. A refused managed texture is retried in D3D9Ex's hidden managed
// pool (6, as Special K does), then as a DEFAULT dynamic texture (as dxwrapper
// does). Both keep LockRect working.

#include "flip_model.h"

#include <d3d9.h>

#include "fullscreen.h"
#include "logging.h"

namespace dm {
namespace {

using Create9Fn = IDirect3D9*(WINAPI*)(UINT);
using Create9ExFn = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
using CreateDeviceFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                                                   D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
using CreateTextureFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, UINT, UINT, UINT, DWORD, D3DFORMAT,
                                                    D3DPOOL, IDirect3DTexture9**, HANDLE*);

// IDirect3D9::CreateDevice and IDirect3DDevice9::CreateTexture vtable slots.
enum { kSlotCreateDevice = 16, kSlotCreateTexture = 23 };
const D3DPOOL kPoolManagedEx = static_cast<D3DPOOL>(6);
// IID_IDirect3DDevice9Ex, spelled out so no GUID library is linked.
const GUID kIidDevice9Ex = {0xb18b10ce, 0x2649, 0x405a, {0x87, 0x0f, 0x95, 0xf7, 0x77, 0xd4, 0x31, 0x3a}};

Create9Fn g_create9 = nullptr;
CreateDeviceFn g_createDevice = nullptr;
CreateTextureFn g_createTexture = nullptr;
IDirect3D9* g_d3dEx = nullptr;
bool g_active = false;

bool writePointer(void** slot, void* value) {
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    InterlockedExchangePointer(slot, value);
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}

// Replaces one entry of the class vtable inside d3d9.dll (shared by every
// object of the class), once. Never replace the vtable pointer itself (see
// renderer_reset.cpp bindDevice).
template <class Fn>
void hookSlot(void* object, int index, Fn hook, Fn* original) {
    if (*original) return;
    void** slot = *reinterpret_cast<void***>(object) + index;
    Fn previous = reinterpret_cast<Fn>(*slot);
    if (writePointer(slot, reinterpret_cast<void*>(hook))) *original = previous;
}

HRESULT STDMETHODCALLTYPE createTexture(IDirect3DDevice9* dev, UINT w, UINT h, UINT levels, DWORD usage,
                                        D3DFORMAT format, D3DPOOL pool, IDirect3DTexture9** out,
                                        HANDLE* shared) {
    HRESULT hr = g_createTexture(dev, w, h, levels, usage, format, pool, out, shared);
    if (hr != D3DERR_INVALIDCALL || pool != D3DPOOL_MANAGED) return hr;
    hr = g_createTexture(dev, w, h, levels, usage, format, kPoolManagedEx, out, shared);
    if (FAILED(hr))
        hr = g_createTexture(dev, w, h, levels, usage | D3DUSAGE_DYNAMIC, format, D3DPOOL_DEFAULT, out, shared);
    static bool logged = false;
    if (!logged) {
        logged = true;
        DM_LOG("flip model: managed texture %ux%u recreated for D3D9Ex: %08lx", w, h, static_cast<long>(hr));
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE createDevice(IDirect3D9* self, UINT adapter, D3DDEVTYPE type, HWND focus,
                                       DWORD flags, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out) {
    if (self != g_d3dEx || !pp || !out) return g_createDevice(self, adapter, type, focus, flags, pp, out);
    const D3DPRESENT_PARAMETERS requested = *pp;
    ppUseFlipModel(reinterpret_cast<unsigned char*>(pp));
    HRESULT hr = g_createDevice(self, adapter, type, focus, flags, pp, out);
    if (FAILED(hr) && pp->SwapEffect != requested.SwapEffect) {
        // Vista has D3D9Ex but no FLIPEX.
        DM_LOG("flip model: device creation refused it (%08lx); using the classic swap chain",
               static_cast<long>(hr));
        *pp = requested;
        hr = g_createDevice(self, adapter, type, focus, flags, pp, out);
    } else if (SUCCEEDED(hr) && pp->SwapEffect == D3DSWAPEFFECT_FLIPEX && !g_active) {
        g_active = true;
        DM_LOG("flip model: active (window and borderless presents never wait)");
    }
    if (FAILED(hr) || !*out) return hr;
    hookSlot(*out, kSlotCreateTexture, &createTexture, &g_createTexture);
    // At most two queued frames (default three). One is too few: a borderless
    // window flipped straight to the screen then drops frames (50 of 60 at 60 Hz).
    IDirect3DDevice9Ex* ex = nullptr;
    if (SUCCEEDED((*out)->QueryInterface(kIidDevice9Ex, reinterpret_cast<void**>(&ex))) && ex) {
        ex->SetMaximumFrameLatency(2);
        ex->Release();
    }
    return hr;
}

IDirect3D9* WINAPI direct3DCreate9(UINT sdkVersion) {
    auto createEx = reinterpret_cast<Create9ExFn>(
        GetProcAddress(GetModuleHandleA("d3d9.dll"), "Direct3DCreate9Ex"));
    IDirect3D9Ex* ex = nullptr;
    if (!createEx || FAILED(createEx(sdkVersion, &ex)) || !ex) {
        DM_LOG("flip model: D3D9Ex unavailable (Windows XP?); classic device");
        return g_create9(sdkVersion);
    }
    g_d3dEx = ex;
    hookSlot(ex, kSlotCreateDevice, &createDevice, &g_createDevice);
    return ex;
}

}  // namespace

bool flipModelRedirect(HMODULE ddraw) {
    HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
    void* real = d3d9 ? reinterpret_cast<void*>(GetProcAddress(d3d9, "Direct3DCreate9")) : nullptr;
    if (!ddraw || !real) return false;
    auto* base = reinterpret_cast<unsigned char*>(ddraw);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY& imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imports.VirtualAddress) return false;
    // The bound import slot holding the real export is the one to redirect.
    for (auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + imports.VirtualAddress); desc->Name; ++desc) {
        if (lstrcmpiA(reinterpret_cast<char*>(base + desc->Name), "d3d9.dll") != 0) continue;
        for (void** slot = reinterpret_cast<void**>(base + desc->FirstThunk); *slot; ++slot) {
            if (*slot != real) continue;
            g_create9 = reinterpret_cast<Create9Fn>(real);
            return writePointer(slot, reinterpret_cast<void*>(&direct3DCreate9));
        }
    }
    return false;
}

bool flipModelActive() { return g_active; }

}  // namespace dm
