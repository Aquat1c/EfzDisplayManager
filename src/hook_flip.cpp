#include "hook_flip.h"

#include <windows.h>
#include <d3d9.h>

#include "format.h"
#include "geometry.h"
#include "logging.h"

namespace dm {

namespace {

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
