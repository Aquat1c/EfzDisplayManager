#include "compat.h"

#include <windows.h>

#include "logging.h"

namespace dm {

namespace {

// IDirect3DDevice9 vtable slots (verified against the binary anchors
// Reset=16, Present=17, GetBackBuffer=18, StretchRect=34, SetRenderTarget=37,
// GetRenderTarget=38, BeginScene=41, EndScene=42, Clear=43 - all consistent
// with the SDK interface order).
enum {
    kVtReset = 16,
    kVtCreateTexture = 23,
    kVtCreateVolumeTexture = 24,
    kVtCreateCubeTexture = 25,
    kVtCreateVertexBuffer = 26,
    kVtCreateIndexBuffer = 27,
    kVtCreateRenderTarget = 28,
    kVtCreateDepthStencilSurface = 29,
    kVtCreateOffscreenPlainSurface = 36,
};

// The D3D9 device has 118 vtable entries.
enum { kVtCount = 118 };

bool g_installed = false;

// Tracked foreign resources (created by anyone through the device after the
// layer was installed). Fixed-size, lock-free append: all creation happens
// on the game thread; our reset path also runs on the game thread (Flip
// entry), so no cross-thread races exist. Overflow = resource untracked
// (Reset then fails and the caller rolls back - safe).
void* g_tracked[256];
int g_trackedCount = 0;
bool g_trackingSuspended = false;

template <typename T>
inline void trackIfDefaultPool(D3DPOOL pool, T* obj) {
    if (pool != D3DPOOL_DEFAULT || !obj || g_trackingSuspended) return;
    trackPointer(obj);
}

// Pointer-deduped tracking: some callers hand out the same object every
// frame; unique pointers only. (GetBackBuffer is deliberately NOT tracked:
// force-releasing it would eat the swapchain's own references.)
inline void trackPointer(void* obj) {
    for (int i = 0; i < g_trackedCount; ++i) {
        if (g_tracked[i] == obj) return;
    }
    if (g_trackedCount < 256) {
        g_tracked[g_trackedCount++] = obj;
    }
}

typedef long(__stdcall* CreateTextureFn)(void*, UINT, UINT, UINT, DWORD, DWORD,
                                         D3DPOOL, IDirect3DTexture9**, void*);
CreateTextureFn g_origCreateTexture;
long __stdcall dmCreateTexture(void* self, UINT w, UINT h, UINT lv, DWORD usage,
                               DWORD fmt, D3DPOOL pool,
                               IDirect3DTexture9** out, void* shared) {
    long hr = g_origCreateTexture(self, w, h, lv, usage, fmt, pool, out, shared);
    if (SUCCEEDED(hr) && out && *out) {
        trackIfDefaultPool(pool, *out);
    }
    return hr;
}

typedef long(__stdcall* CreateVBFn)(void*, UINT, DWORD, DWORD, D3DPOOL,
                                    IDirect3DVertexBuffer9**, void*);
CreateVBFn g_origCreateVB;
long __stdcall dmCreateVB(void* self, UINT len, DWORD usage, DWORD fvf,
                          D3DPOOL pool, IDirect3DVertexBuffer9** out,
                          void* shared) {
    long hr = g_origCreateVB(self, len, usage, fvf, pool, out, shared);
    if (SUCCEEDED(hr) && out && *out) {
        trackIfDefaultPool(pool, *out);
    }
    return hr;
}

typedef long(__stdcall* CreateIBFn)(void*, UINT, DWORD, D3DFORMAT, D3DPOOL,
                                    IDirect3DIndexBuffer9**, void*);
CreateIBFn g_origCreateIB;
long __stdcall dmCreateIB(void* self, UINT len, DWORD usage, D3DFORMAT fmt,
                          D3DPOOL pool, IDirect3DIndexBuffer9** out,
                          void* shared) {
    long hr = g_origCreateIB(self, len, usage, fmt, pool, out, shared);
    if (SUCCEEDED(hr) && out && *out) {
        trackIfDefaultPool(pool, *out);
    }
    return hr;
}

typedef long(__stdcall* CreateVolTexFn)(void*, UINT, UINT, UINT, UINT, DWORD,
                                        DWORD, D3DPOOL,
                                        IDirect3DVolumeTexture9**, void*);
CreateVolTexFn g_origCreateVolTex;
long __stdcall dmCreateVolTex(void* self, UINT w, UINT h, UINT d, UINT lv,
                              DWORD usage, DWORD fmt, D3DPOOL pool,
                              IDirect3DVolumeTexture9** out, void* shared) {
    long hr = g_origCreateVolTex(self, w, h, d, lv, usage, fmt, pool, out,
                                 shared);
    if (SUCCEEDED(hr) && out && *out) {
        trackIfDefaultPool(pool, *out);
    }
    return hr;
}

typedef long(__stdcall* CreateCubeTexFn)(void*, UINT, UINT, DWORD, DWORD,
                                         D3DPOOL,
                                         IDirect3DCubeTexture9**, void*);
CreateCubeTexFn g_origCreateCubeTex;
long __stdcall dmCreateCubeTex(void* self, UINT edge, UINT lv, DWORD usage,
                               DWORD fmt, D3DPOOL pool,
                               IDirect3DCubeTexture9** out, void* shared) {
    long hr = g_origCreateCubeTex(self, edge, lv, usage, fmt, pool, out,
                                  shared);
    if (SUCCEEDED(hr) && out && *out) {
        trackIfDefaultPool(pool, *out);
    }
    return hr;
}

typedef long(__stdcall* CreateDSFn)(void*, UINT, UINT, D3DFORMAT,
                                    D3DMULTISAMPLE_TYPE, DWORD, BOOL,
                                    IDirect3DSurface9**, void*);
CreateDSFn g_origCreateDS;
long __stdcall dmCreateDS(void* self, UINT w, UINT h, D3DFORMAT fmt,
                          D3DMULTISAMPLE_TYPE ms, DWORD q, BOOL discard,
                          IDirect3DSurface9** out, void* shared) {
    long hr = g_origCreateDS(self, w, h, fmt, ms, q, discard, out, shared);
    if (SUCCEEDED(hr) && out && *out) {
        trackIfDefaultPool(D3DPOOL_DEFAULT, *out);
    }
    return hr;
}

typedef long(__stdcall* CreateRTFn)(void*, UINT, UINT, D3DFORMAT,
                                     D3DMULTISAMPLE_TYPE, DWORD, BOOL,
                                     IDirect3DSurface9**, void*);
CreateRTFn g_origCreateRT;
long __stdcall dmCreateRT(void* self, UINT w, UINT h, D3DFORMAT fmt,
                          D3DMULTISAMPLE_TYPE ms, DWORD msQuality,
                          BOOL lockable, IDirect3DSurface9** out,
                          void* shared) {
    long hr = g_origCreateRT(self, w, h, fmt, ms, msQuality, lockable, out,
                             shared);
    if (SUCCEEDED(hr) && out && *out) {
        trackIfDefaultPool(D3DPOOL_DEFAULT, *out);
    }
    return hr;
}

typedef long(__stdcall* CreateOffsFn)(void*, UINT, UINT, D3DFORMAT, D3DPOOL,
                                      IDirect3DSurface9**, void*);
CreateOffsFn g_origCreateOffs;
long __stdcall dmCreateOffs(void* self, UINT w, UINT h, D3DFORMAT fmt,
                            D3DPOOL pool, IDirect3DSurface9** out, void* sharedHandle) {
    long hr = g_origCreateOffs(self, w, h, fmt, pool, out, sharedHandle);
    if (SUCCEEDED(hr) && out && *out) {
        trackIfDefaultPool(pool, *out);
    }
    return hr;
}

}  // namespace

bool compatInstall(IDirect3DDevice9* dev) {
    if (g_installed || !dev) return g_installed;

    // Modern d3d9 gives each device a per-instance vtable on the heap, so
    // the creation slots can be patched IN PLACE - no table-length guessing,
    // no instance pointer swap.
    void** vt = *reinterpret_cast<void***>(dev);
    if (IsBadReadPtr(vt, (kVtCreateOffscreenPlainSurface + 1) * sizeof(void*))) {
        DM_LOG("compat: vtable unreadable; layer disabled");
        return false;
    }
    g_origCreateTexture = (CreateTextureFn)vt[kVtCreateTexture];
    g_origCreateVB = (CreateVBFn)vt[kVtCreateVertexBuffer];
    g_origCreateRT = (CreateRTFn)vt[kVtCreateRenderTarget];
    g_origCreateOffs = (CreateOffsFn)vt[kVtCreateOffscreenPlainSurface];
    g_origCreateIB = (CreateIBFn)vt[kVtCreateIndexBuffer];
    g_origCreateVolTex = (CreateVolTexFn)vt[kVtCreateVolumeTexture];
    g_origCreateCubeTex = (CreateCubeTexFn)vt[kVtCreateCubeTexture];
    g_origCreateDS = (CreateDSFn)vt[kVtCreateDepthStencilSurface];

    DWORD oldProtect = 0;
    const int first = kVtCreateTexture;
    const int count = kVtCreateOffscreenPlainSurface - kVtCreateTexture + 1;
    // (covers slots 23..36 incl. index buffer, volume/cube textures,
    // depth-stencil surfaces)
    if (!VirtualProtect(&vt[first], count * sizeof(void*), PAGE_READWRITE,
                        &oldProtect)) {
        DM_LOG("compat: vtable protect failed (%lu)",
               unsigned long(GetLastError()));
        return false;
    }
    vt[kVtCreateTexture] = (void*)&dmCreateTexture;
    vt[kVtCreateVertexBuffer] = (void*)&dmCreateVB;
    vt[kVtCreateRenderTarget] = (void*)&dmCreateRT;
    vt[kVtCreateOffscreenPlainSurface] = (void*)&dmCreateOffs;
    vt[kVtCreateIndexBuffer] = (void*)&dmCreateIB;
    vt[kVtCreateVolumeTexture] = (void*)&dmCreateVolTex;
    vt[kVtCreateCubeTexture] = (void*)&dmCreateCubeTex;
    vt[kVtCreateDepthStencilSurface] = (void*)&dmCreateDS;
    FlushInstructionCache(GetCurrentProcess(), &vt[first],
                          count * sizeof(void*));
    g_installed = true;
    DM_LOG("compat: in-place vtable hooks installed (vt=%p tex=%p)",
           vt, vt[kVtCreateTexture]);
    return true;
}

void compatExcludeForeign(void* a, void* b) {
    for (int i = 0; i < g_trackedCount; ) {
        if (g_tracked[i] == a || g_tracked[i] == b) {
            g_tracked[i] = g_tracked[g_trackedCount - 1];
            --g_trackedCount;
        } else {
            ++i;
        }
    }
}

int compatReleaseForeignResources() {
    const int n = g_trackedCount;
    for (int i = 0; i < n; ++i) {
        IUnknown* obj = reinterpret_cast<IUnknown*>(g_tracked[i]);
        if (!obj) continue;
        // Release every reference: the owner's and any internal ones. Cap
        // the loop so a pathological refcount cannot hang the game.
        for (int r = 0; r < 16; ++r) {
            ULONG remaining = obj->Release();
            if (remaining == 0) break;
        }
    }
    g_trackedCount = 0;
    return n;
}

void compatSuspendTracking() { g_trackingSuspended = true; }
void compatResumeTracking() { g_trackingSuspended = false; }

bool compatActive() {
    return g_installed;
}

}  // namespace dm
