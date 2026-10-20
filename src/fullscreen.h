#pragma once

#include <cstdint>

namespace dm {

struct DisplayMode {
    long w = 0;
    long h = 0;
    long refresh = 0;
};

// D3DPRESENT_PARAMETERS field offsets (x86, 0x38-byte struct) - the wrapper's
// parameters live at renderer+0x0C and are passed straight to Reset.
enum : uint32_t {
    kPp_BackBufferWidth = 0x00,
    kPp_BackBufferHeight = 0x04,
    kPp_SwapEffect = 0x18,
    kPp_DeviceWindow = 0x1C,
    kPp_Windowed = 0x20,
    kPp_RefreshRate = 0x30,
    kPp_PresentationInterval = 0x34,
    kPp_Size = 0x38,
};

constexpr uint32_t kSwapEffectDiscard = 1;

// Snapshots the current parameters (used to restore windowed mode later).
void ppSave(unsigned char* dst, const unsigned char* src);

// Converts a live parameter block to exclusive fullscreen at the given mode:
// backbuffer = mode, Windowed = FALSE, SwapEffect = DISCARD (required for
// predictable exclusive behavior), refresh set, device window pinned.
void ppEnterExclusive(unsigned char* pp, const DisplayMode& mode, uintptr_t hwnd);

// Restores a windowed parameter block from a snapshot.
void ppRestoreWindowed(unsigned char* pp, const unsigned char* saved);

// Applies a VSync choice: -1 keeps the current presentation interval,
// 0 = D3DPRESENT_INTERVAL_IMMEDIATE, 1 = D3DPRESENT_INTERVAL_ONE.
void ppApplyVSync(unsigned char* pp, long vsync);

long ppWidth(const unsigned char* pp);
long ppHeight(const unsigned char* pp);
bool ppIsWindowed(const unsigned char* pp);

}  // namespace dm
