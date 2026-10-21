#include "fullscreen.h"

#include <cstring>

namespace dm {

namespace {

uint32_t readU32At(const unsigned char* p, uint32_t off) {
    uint32_t v;
    memcpy(&v, p + off, 4);
    return v;
}

void writeU32At(unsigned char* p, uint32_t off, uint32_t v) {
    memcpy(p + off, &v, 4);
}

}  // namespace

void ppSave(unsigned char* dst, const unsigned char* src) {
    memcpy(dst, src, kPp_Size);
}

void ppEnterExclusive(unsigned char* pp, const DisplayMode& mode, uintptr_t hwnd) {
    writeU32At(pp, kPp_BackBufferWidth, uint32_t(mode.w));
    writeU32At(pp, kPp_BackBufferHeight, uint32_t(mode.h));
    writeU32At(pp, kPp_BackBufferFormat, mode.format);
    writeU32At(pp, kPp_SwapEffect, kSwapEffectDiscard);
    writeU32At(pp, kPp_DeviceWindow, uint32_t(hwnd));
    writeU32At(pp, kPp_Windowed, 0);
    writeU32At(pp, kPp_RefreshRate, uint32_t(mode.refresh));
    writeU32At(pp, kPp_PresentationInterval, 0x80000000u);  // D3DPRESENT_INTERVAL_IMMEDIATE
}

void ppRestoreWindowed(unsigned char* pp, const unsigned char* saved) {
    memcpy(pp, saved, kPp_Size);
}

void ppApplyVSync(unsigned char* pp, long vsync) {
    if (vsync == 0) {
        writeU32At(pp, kPp_PresentationInterval, 0x80000000u);  // IMMEDIATE
    } else if (vsync == 1) {
        writeU32At(pp, kPp_PresentationInterval, 1);  // ONE
    }
    // any other value: keep the wrapper's interval untouched.
}

long ppWidth(const unsigned char* pp) {
    return long(readU32At(pp, kPp_BackBufferWidth));
}

long ppHeight(const unsigned char* pp) {
    return long(readU32At(pp, kPp_BackBufferHeight));
}

unsigned long nearestRefresh(const unsigned long* rates, unsigned count,
                             unsigned long wanted, unsigned long fallback) {
    if (!wanted || !count) return fallback;
    unsigned long best = rates[0];
    for (unsigned i = 1; i < count; ++i) {
        const unsigned long d = rates[i] > wanted ? rates[i] - wanted : wanted - rates[i];
        const unsigned long bestD = best > wanted ? best - wanted : wanted - best;
        if (d < bestD || (d == bestD && rates[i] > best)) best = rates[i];
    }
    return best;
}

void ppUseFlipModel(unsigned char* pp) {
    if (!readU32At(pp, kPp_Windowed)) {
        if (readU32At(pp, kPp_SwapEffect) == kSwapEffectFlipEx) writeU32At(pp, kPp_SwapEffect, kSwapEffectDiscard);
        return;
    }
    writeU32At(pp, kPp_SwapEffect, kSwapEffectFlipEx);
    writeU32At(pp, kPp_PresentationInterval, 0x80000000u);  // IMMEDIATE: never wait
    if (readU32At(pp, kPp_BackBufferCount) < 2) writeU32At(pp, kPp_BackBufferCount, 2);
    writeU32At(pp, kPp_MultiSampleType, 0);
    writeU32At(pp, kPp_MultiSampleQuality, 0);
    writeU32At(pp, kPp_Flags, readU32At(pp, kPp_Flags) & ~kPresentFlagLockableBackBuffer);
}

unsigned long pickExclusiveRefresh(const unsigned long* rates, unsigned count,
                                   unsigned long wanted, unsigned long desktop) {
    unsigned long usable[64];
    unsigned n = 0;
    for (unsigned i = 0; i < count && n < 64; ++i)
        if (rates[i] >= kGameFps) usable[n++] = rates[i];
    if (!n) return 0;
    if (wanted) return nearestRefresh(usable, n, wanted, 0);
    if (desktop >= kGameFps) return desktop;
    unsigned long lowest = usable[0];
    for (unsigned i = 1; i < n; ++i)
        if (usable[i] < lowest) lowest = usable[i];
    return lowest;
}

}  // namespace dm
