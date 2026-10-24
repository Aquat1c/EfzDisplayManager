#pragma once

#include <windows.h>

#include "geometry.h"

namespace dm {

// [Display] keys of DisplayManager.ini; see that file for user-facing notes.
struct Config {
    int enabled;                // Enabled (1)
    int logFirst;               // LogFirst (6): log the first N final blits
    int logEvery;               // LogEvery (0): then every Nth; 0 = never
    ScaleMode mode;             // Mode: FitToScreen | IntegerScaling | CustomResolution
    int integerScale;           // IntegerScale (0 = largest multiple that fits)
    int customWidth;            // CustomWidth (640)
    int customHeight;           // CustomHeight (480)
    int sharpness;              // Sharpness 10-40 (= 1.0-4.0, 20); Sharp filter only
    unsigned long filter;       // Filter: Linear = 0 (keep Revival's filter),
                                // Point = D3DTEXF_POINT, Sharp = 100, xBR = 101
    unsigned long backgroundColor;  // BackgroundColor RRGGBB (000000)
    int borderlessFullscreen;   // FullscreenMode: Exclusive (default, also 0) | Borderless (also 1)
    unsigned long fullscreenRefresh;  // FullscreenRefresh: exclusive Hz, 0 = desktop rate
    int fullscreenDelayMs;      // FullscreenDelayMs: test/automation delay before
                                // the Fullscreen=1 startup request
    int fullscreen;             // Fullscreen (0): start in fullscreen (FullscreenMode target)
    int fullscreenHotkey;       // FullscreenHotkey (1): F11 / Alt+Enter toggle
    int hideCursor;             // HideCursor (1): hide the idle pointer in fullscreen
    long vsync;                 // VSync (0): 1 = exclusive presents wait for the screen;
                                // else immediate
    int dynamicUpload;          // DynamicUpload (1): current-image DISCARD upload
    int traceFrames;            // TraceFrames (0): bounded in-memory diagnostic capture
    int lowLatencyWindow;       // LowLatencyWindow (0): D3D9Ex flip model in window/borderless
    int compatLayer;            // CompatLayer (1): reset contract with known overlays;
                                // 0 + a loaded overlay blocks voluntary resets
    int resizeBackBuffer;       // ResizeBackBuffer (1): backbuffer follows the window
    int windowAdjustStyle;      // WindowAdjustStyle (1): enable the maximize box
    int windowDragResize;       // WindowDragResize (1): sizing border; a held drag runs
                                // Win32's modal size loop and pauses the game
};

// Repair is allowed only from the startup worker, never under the loader lock.
// Wide APIs preserve game paths outside the system code page.
Config loadConfig(HMODULE module, bool repair = false);
Config loadConfigFile(const wchar_t* ini, bool repair);

}  // namespace dm
