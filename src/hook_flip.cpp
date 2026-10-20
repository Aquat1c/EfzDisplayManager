#include "hook_flip.h"

#include <windows.h>
#include <d3d9.h>

#include "format.h"
#include "geometry.h"
#include "logging.h"
#include "resize.h"

// Trampoline for the Flip-entry detour: original 7-byte prologue + jmp back.
extern "C" unsigned char g_entryTrampoline[16] = {0xCC};
extern "C" void* g_entryTrampolinePtr = nullptr;

namespace dm {

namespace {

bool g_resizeEnabled = false;
ResizeState g_resizeState;

typedef long (__stdcall* VtMethodFn)(void* self);

// D3DX font vtable slots used around a device reset (established from the
// wrapper's own reset helper in the static research).
enum { kFontOnLostDevice = 16, kFontOnResetDevice = 17 };

struct AdaptConfig {
    bool active;
    ScaleMode mode;
    int integerScale;
    int customWidth;
    int customHeight;
    unsigned long filterOverride;      // 0 = keep the wrapper's filter
    unsigned long backgroundColor;     // ARGB
};

struct ObsState {
    unsigned char* base = nullptr;
    const DdrawProfile* profile = nullptr;
    HookConfig config = {8, 600};
    AdaptConfig adapt;

    unsigned long calls = 0;

    // Previous-frame snapshot for observation change detection.
    bool hasPrev = false;
    IDirect3DSurface9* prevSrc = nullptr;
    IDirect3DSurface9* prevDst = nullptr;
    unsigned long prevFilter = 0;
    unsigned long prevPpW = 0, prevPpH = 0;
    unsigned long prevBorderless = 0;
    void* prevDevice = nullptr;
    void* prevNative = nullptr;
    RECT prevRect = {};
    bool prevRectNull = true;

    // Surface description cache, refreshed when the surface pointers change
    // (also the post-reset recreation signal).
    bool haveDescs = false;
    unsigned long srcW = 0, srcH = 0, bbW = 0, bbH = 0;
};

ObsState g_obs;

unsigned long readU32(unsigned char* base, uint32_t rva) {
    return *reinterpret_cast<unsigned long*>(base + rva);
}

}  // namespace

void setHookContext(unsigned char* ddrawBase, const DdrawProfile* profile,
                    const HookConfig& config) {
    g_obs.base = ddrawBase;
    g_obs.profile = profile;
    g_obs.config = config;
}

void setAdaptConfig(ScaleMode mode, int integerScale, int customWidth,
                    int customHeight, unsigned long filterOverride,
                    unsigned long backgroundColor) {
    g_obs.adapt.active = true;
    g_obs.adapt.mode = mode;
    g_obs.adapt.integerScale = integerScale;
    g_obs.adapt.customWidth = customWidth;
    g_obs.adapt.customHeight = customHeight;
    g_obs.adapt.filterOverride = filterOverride;
    g_obs.adapt.backgroundColor = backgroundColor;
}

void setResizeConfig(bool enabled) {
    g_resizeEnabled = enabled;
}

unsigned char* entryTrampolineBuffer() {
    return g_entryTrampoline;
}

size_t entryTrampolineCapacity() {
    return sizeof(g_entryTrampoline);
}

void prepareEntryTrampoline() {
    g_entryTrampolinePtr = g_entryTrampoline;
}

unsigned long hookCallCount() {
    return g_obs.calls;
}

extern "C" void dmFlipEntryWork() {
    if (!g_resizeEnabled || !g_obs.base || !g_obs.profile) return;
    HWND hwnd = reinterpret_cast<HWND>(
        *reinterpret_cast<void**>(g_obs.base + g_obs.profile->hwndRva));
    RECT rc;
    if (!hwnd || !GetClientRect(hwnd, &rc)) return;

    ResizeInput in;
    in.clientW = rc.right - rc.left;
    in.clientH = rc.bottom - rc.top;
    in.bbW = long(readU32(g_obs.base, g_obs.profile->ppRva));
    in.bbH = long(readU32(g_obs.base, g_obs.profile->ppRva + 4));
    in.borderless = *reinterpret_cast<unsigned char*>(
        g_obs.base + g_obs.profile->borderlessRva);
    long newW = 0, newH = 0;
    if (evaluateResize(&g_resizeState, in, &newW, &newH) != ResizeAction::Reset) {
        return;
    }
    DM_LOG("resize: client %ldx%ld stable; backbuffer %ldx%ld -> %ldx%ld",
           in.clientW, in.clientH, in.bbW, in.bbH, newW, newH);

    // Own font-safe reset sequence (the wrapper's helper dereferences the
    // font at +0x74 without a null check and crashes in direct efz.exe
    // launches where Revival's text render never initialized).
    IDirect3DDevice9* dev = *reinterpret_cast<IDirect3DDevice9**>(
        g_obs.base + g_obs.profile->deviceRva);
    void* font = *reinterpret_cast<void**>(g_obs.base + g_obs.profile->fontRva);
    IDirect3DSurface9* staging = *reinterpret_cast<IDirect3DSurface9**>(
        g_obs.base + g_obs.profile->rendererObjectRva);
    IDirect3DSurface9* native = *reinterpret_cast<IDirect3DSurface9**>(
        g_obs.base + g_obs.profile->nativeSurfaceRva);
    if (!dev || !staging || !native) {
        DM_LOG("resize: device/surfaces missing; skipping reset");
        return;
    }

    // Capture the surface descriptions before releasing anything so the
    // recreation matches the wrapper's formats exactly.
    D3DSURFACE_DESC dS = {}, dN = {};
    if (FAILED(staging->GetDesc(&dS)) || FAILED(native->GetDesc(&dN))) {
        DM_LOG("resize: GetDesc failed; skipping reset");
        return;
    }

    void*** fontVt = reinterpret_cast<void***>(font);
    if (font && fontVt) {
        reinterpret_cast<VtMethodFn>(fontVt[0][kFontOnLostDevice])(font);
    }
    staging->Release();
    native->Release();
    *reinterpret_cast<IDirect3DSurface9**>(g_obs.base +
                                           g_obs.profile->rendererObjectRva) = nullptr;
    *reinterpret_cast<IDirect3DSurface9**>(g_obs.base +
                                           g_obs.profile->nativeSurfaceRva) = nullptr;

    *reinterpret_cast<long*>(g_obs.base + g_obs.profile->ppRva) = newW;
    *reinterpret_cast<long*>(g_obs.base + g_obs.profile->ppRva + 4) = newH;
    HRESULT hr = dev->Reset(reinterpret_cast<D3DPRESENT_PARAMETERS*>(
        g_obs.base + g_obs.profile->ppRva));
    DM_LOG("resize: Reset hr=%08lx", long(hr));

    IDirect3DSurface9* s = nullptr;
    IDirect3DSurface9* n = nullptr;
    if (dS.Usage & D3DUSAGE_RENDERTARGET) {
        dev->CreateRenderTarget(dS.Width, dS.Height, dS.Format,
                                dS.MultiSampleType, dS.MultiSampleQuality,
                                TRUE, &s, nullptr);
    } else {
        dev->CreateOffscreenPlainSurface(dS.Width, dS.Height, dS.Format,
                                         dS.Pool, &s, nullptr);
    }
    if (dN.Usage & D3DUSAGE_RENDERTARGET) {
        dev->CreateRenderTarget(dN.Width, dN.Height, dN.Format,
                                dN.MultiSampleType, dN.MultiSampleQuality,
                                TRUE, &n, nullptr);
    } else {
        dev->CreateOffscreenPlainSurface(dN.Width, dN.Height, dN.Format,
                                         dN.Pool, &n, nullptr);
    }
    if (s) {
        *reinterpret_cast<IDirect3DSurface9**>(
            g_obs.base + g_obs.profile->rendererObjectRva) = s;
    }
    if (n) {
        *reinterpret_cast<IDirect3DSurface9**>(
            g_obs.base + g_obs.profile->nativeSurfaceRva) = n;
    }
    if (!s || !n) {
        DM_LOG("resize: surface recreation incomplete (staging=%d native=%d)",
               s ? 1 : 0, n ? 1 : 0);
    }
    if (font && fontVt) {
        reinterpret_cast<VtMethodFn>(fontVt[0][kFontOnResetDevice])(font);
    }
    g_obs.haveDescs = false;
}

extern "C" __declspec(naked) void dmFlipEntryShim() {
    __asm {
        call dmFlipEntryWork
        jmp dword ptr [g_entryTrampolinePtr]
    }
}

extern "C" void __stdcall dmFinalStretchHook(IDirect3DDevice9* dev,
                                             IDirect3DSurface9* src,
                                             const RECT* srcRect,
                                             IDirect3DSurface9* dst,
                                             const RECT* dstRect,
                                             unsigned long filter) {
    ++g_obs.calls;

    // Renderer-object globals (verified admission guarantees the layout).
    const unsigned long ppW = readU32(g_obs.base, g_obs.profile->ppRva);
    const unsigned long ppH = readU32(g_obs.base, g_obs.profile->ppRva + 4);
    const unsigned long borderless =
        *reinterpret_cast<unsigned char*>(g_obs.base + g_obs.profile->borderlessRva);
    void* deviceGlobal =
        *reinterpret_cast<void**>(g_obs.base + g_obs.profile->deviceRva);
    void* nativeGlobal =
        *reinterpret_cast<void**>(g_obs.base + g_obs.profile->nativeSurfaceRva);

    // Refresh the surface description cache when the surfaces change.
    if (!g_obs.haveDescs || src != g_obs.prevSrc || dst != g_obs.prevDst) {
        D3DSURFACE_DESC sd = {}, dd = {};
        bool ok = true;
        if (src && SUCCEEDED(src->GetDesc(&sd)) && sd.Width && sd.Height) {
            g_obs.srcW = sd.Width;
            g_obs.srcH = sd.Height;
        } else {
            ok = false;
        }
        if (dst && SUCCEEDED(dst->GetDesc(&dd)) && dd.Width && dd.Height) {
            g_obs.bbW = dd.Width;
            g_obs.bbH = dd.Height;
        } else {
            ok = false;
        }
        g_obs.haveDescs = ok;
        if (ok && dm::log::enabled()) {
            DM_LOG("surfaces: native %ux%u, backbuffer %ux%u (src %p dst %p)",
                   g_obs.srcW, g_obs.srcH, g_obs.bbW, g_obs.bbH, src, dst);
        }
    }

    const bool rectNull = dstRect == nullptr;
    const bool rectSame = rectNull == g_obs.prevRectNull &&
                          (rectNull || (dstRect->left == g_obs.prevRect.left &&
                                        dstRect->top == g_obs.prevRect.top &&
                                        dstRect->right == g_obs.prevRect.right &&
                                        dstRect->bottom == g_obs.prevRect.bottom));
    const bool changed =
        !g_obs.hasPrev || src != g_obs.prevSrc || dst != g_obs.prevDst ||
        filter != g_obs.prevFilter || ppW != g_obs.prevPpW || ppH != g_obs.prevPpH ||
        borderless != g_obs.prevBorderless || deviceGlobal != g_obs.prevDevice ||
        nativeGlobal != g_obs.prevNative || !rectSame;
    const bool due = g_obs.calls <= unsigned(g_obs.config.logFirst) ||
                     (g_obs.config.logEvery > 0 &&
                      g_obs.calls % unsigned(g_obs.config.logEvery) == 0);

    if ((changed || due) && dm::log::enabled()) {
        char rectText[80];
        if (rectNull) {
            dm::formatString(rectText, sizeof(rectText), "%s", "NULL(full)");
        } else {
            dm::formatString(rectText, sizeof(rectText),
                             "(%ld,%ld,%ld,%ld) %ldx%ld", dstRect->left,
                             dstRect->top, dstRect->right, dstRect->bottom,
                             dstRect->right - dstRect->left,
                             dstRect->bottom - dstRect->top);
        }
        DM_LOG("flip #%u: dstRect=%s filter=%lu pp=%lux%lu borderless=%lu%s",
               g_obs.calls, rectText, filter, ppW, ppH, borderless,
               changed ? " <changed>" : "");
        if (g_obs.hasPrev &&
            (nativeGlobal != g_obs.prevNative || deviceGlobal != g_obs.prevDevice)) {
            DM_LOG("flip #%u: native/device pointer changed (post-reset "
                   "recreation)",
                   g_obs.calls);
        }
    }

    g_obs.hasPrev = true;
    g_obs.prevSrc = src;
    g_obs.prevDst = dst;
    g_obs.prevFilter = filter;
    g_obs.prevPpW = ppW;
    g_obs.prevPpH = ppH;
    g_obs.prevBorderless = borderless;
    g_obs.prevDevice = deviceGlobal;
    g_obs.prevNative = nativeGlobal;
    g_obs.prevRectNull = rectNull;
    if (!rectNull) g_obs.prevRect = *dstRect;

    // ---- Forwarding: adaptation or native passthrough ----
    if (!g_obs.adapt.active || !g_obs.haveDescs) {
        dev->StretchRect(src, srcRect, dst, dstRect,
                         static_cast<D3DTEXTUREFILTERTYPE>(filter));
        return;
    }

    long outW = 0, outH = 0;
    if (!computeOutputSize(int(g_obs.srcW), int(g_obs.srcH), int(g_obs.bbW),
                           int(g_obs.bbH), g_obs.adapt.mode,
                           g_obs.adapt.integerScale, g_obs.adapt.customWidth,
                           g_obs.adapt.customHeight, &outW, &outH)) {
        dev->StretchRect(src, srcRect, dst, dstRect,
                         static_cast<D3DTEXTUREFILTERTYPE>(filter));
        return;
    }

    long left = 0, top = 0;
    centerRect(g_obs.bbW, g_obs.bbH, outW, outH, &left, &top);
    RECT outRect = {left, top, left + outW, top + outH};

    const unsigned long effectiveFilter =
        g_obs.adapt.filterOverride != 0 ? g_obs.adapt.filterOverride : filter;

    if ((changed || due) && dm::log::enabled()) {
        DM_LOG("adapt: -> (%ld,%ld,%ld,%ld) %ldx%ld filter=%lu (was %lu)",
               outRect.left, outRect.top, outRect.right, outRect.bottom, outW,
               outH, effectiveFilter, filter);
    }

    // Fill the letterbox border when the image does not cover the output.
    if (outW < long(g_obs.bbW) || outH < long(g_obs.bbH)) {
        dev->ColorFill(dst, nullptr,
                       static_cast<D3DCOLOR>(g_obs.adapt.backgroundColor));
    }

    dev->StretchRect(src, srcRect, dst, &outRect,
                     static_cast<D3DTEXTUREFILTERTYPE>(effectiveFilter));
}

}  // namespace dm
