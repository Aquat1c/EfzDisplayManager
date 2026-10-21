#pragma once

#include <cstdint>

namespace dm {

struct DisplayMode {
    long w = 0;
    long h = 0;
    long refresh = 0;
    unsigned long format = 0;  // D3DFMT_* from the adapter desktop mode
};

// D3DPRESENT_PARAMETERS field offsets (x86, 0x38-byte struct) - the wrapper's
// parameters live at renderer+0x0C and are passed straight to Reset.
enum : uint32_t {
    kPp_BackBufferWidth = 0x00,
    kPp_BackBufferHeight = 0x04,
    kPp_BackBufferFormat = 0x08,
    kPp_BackBufferCount = 0x0C,
    kPp_MultiSampleType = 0x10,
    kPp_MultiSampleQuality = 0x14,
    kPp_SwapEffect = 0x18,
    kPp_DeviceWindow = 0x1C,
    kPp_Windowed = 0x20,
    kPp_Flags = 0x2C,
    kPp_RefreshRate = 0x30,
    kPp_PresentationInterval = 0x34,
    kPp_Size = 0x38,
};

constexpr uint32_t kSwapEffectDiscard = 1;
constexpr uint32_t kSwapEffectFlipEx = 5;
constexpr uint32_t kPresentFlagLockableBackBuffer = 1;

// Windowed parameters become a FLIPEX swap chain (needs a D3D9Ex device) with
// IMMEDIATE presents. FLIPEX requires 2+ back buffers, no MSAA and no lockable
// back buffer. Exclusive parameters keep (or get back) DISCARD.
void ppUseFlipModel(unsigned char* pp);

// Snapshots the current parameters (used to restore windowed mode later).
void ppSave(unsigned char* dst, const unsigned char* src);

// Converts a parameter block to exclusive fullscreen at `mode` (size, format,
// refresh), DISCARD, device window pinned, IMMEDIATE interval (ppApplyVSync
// may override).
void ppEnterExclusive(unsigned char* pp, const DisplayMode& mode, uintptr_t hwnd);

// Restores a windowed parameter block from a snapshot.
void ppRestoreWindowed(unsigned char* pp, const unsigned char* saved);

// Applies a VSync choice: -1 keeps the current presentation interval,
// 0 = D3DPRESENT_INTERVAL_IMMEDIATE, 1 = D3DPRESENT_INTERVAL_ONE.
void ppApplyVSync(unsigned char* pp, long vsync);

long ppWidth(const unsigned char* pp);
long ppHeight(const unsigned char* pp);

// The listed rate nearest to wanted (ties take the higher). A full-screen
// refresh must be one EnumAdapterModes lists for the exact size, or Reset
// fails. Returns fallback when wanted is 0 or nothing is listed.
unsigned long nearestRefresh(const unsigned long* rates, unsigned count,
                             unsigned long wanted, unsigned long fallback);

// EFZ draws 64 frames a second (three logic steps each) and is timed by
// them, so a frame held up for the screen slows the whole game.
enum { kGameFps = 64 };

// Exclusive refresh, never below kGameFps (exclusive presents wait for the
// screen). A wanted rate snaps to the nearest listed rate >= kGameFps;
// otherwise the desktop rate, or the lowest usable listed rate when the desktop
// is slower. 0 = none fits; the caller then uses borderless.
unsigned long pickExclusiveRefresh(const unsigned long* rates, unsigned count,
                                   unsigned long wanted, unsigned long desktop);

}  // namespace dm
