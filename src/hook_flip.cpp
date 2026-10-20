#include "hook_flip.h"

#include <windows.h>
#include <d3d9.h>

#include "format.h"
#include "logging.h"

namespace dm {

namespace {

struct ObsState {
    unsigned char* base = nullptr;
    const DdrawProfile* profile = nullptr;
    HookConfig config = {8, 600};
    unsigned long calls = 0;
    // Previous-frame snapshot for change detection.
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

unsigned long hookCallCount() {
    return g_obs.calls;
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
        D3DSURFACE_DESC sd = {}, dd = {};
        char srcDesc[64] = "?";
        char dstDesc[64] = "?";
        if (src && SUCCEEDED(src->GetDesc(&sd))) {
            dm::formatString(srcDesc, sizeof(srcDesc), "%ux%u pool=%u fmt=%u",
                             sd.Width, sd.Height, sd.Pool, sd.Format);
        }
        if (dst && SUCCEEDED(dst->GetDesc(&dd))) {
            dm::formatString(dstDesc, sizeof(dstDesc), "%ux%u pool=%u fmt=%u",
                             dd.Width, dd.Height, dd.Pool, dd.Format);
        }
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
        if (g_obs.hasPrev &&
            (nativeGlobal != g_obs.prevNative || deviceGlobal != g_obs.prevDevice)) {
            DM_LOG("flip #%u: native/device pointer changed (post-reset recreation): "
                   "native %p->%p dev %p->%p",
                   g_obs.calls, g_obs.prevNative, nativeGlobal, g_obs.prevDevice,
                   deviceGlobal);
        }
        DM_LOG("flip #%u: src=[%s] dst=[%s] dstRect=%s filter=%lu pp=%lux%lu "
               "borderless=%lu%s",
               g_obs.calls, srcDesc, dstDesc, rectText, filter, ppW, ppH, borderless,
               changed ? " <changed>" : "");
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

    // Forward-only Stage A: the original operation, unchanged.
    dev->StretchRect(src, srcRect, dst, dstRect,
                     static_cast<D3DTEXTUREFILTERTYPE>(filter));
}

}  // namespace dm
