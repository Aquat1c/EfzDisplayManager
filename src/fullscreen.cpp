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
    writeU32At(pp, kPp_Windowed, 0);  // FALSE: exclusive fullscreen
    writeU32At(pp, kPp_RefreshRate, uint32_t(mode.refresh));
    writeU32At(pp, kPp_PresentationInterval, 0);  // IMMEDIATE: low latency
}

void ppRestoreWindowed(unsigned char* pp, const unsigned char* saved) {
    memcpy(pp, saved, kPp_Size);
}

void ppApplyVSync(unsigned char* pp, long vsync) {
    if (vsync == 0) {
        writeU32At(pp, kPp_PresentationInterval, 0);  // IMMEDIATE
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

bool ppIsWindowed(const unsigned char* pp) {
    return readU32At(pp, kPp_Windowed) != 0;
}

}  // namespace dm
