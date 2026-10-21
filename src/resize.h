#pragma once

namespace dm {

// When to resize the wrapper's backbuffer to follow the window. Pure logic.
//
// Revival's configured window/backbuffer pair (e.g. a 640x480 window with a
// 1280x960 supersampled backbuffer) is recorded on the first flip. Only a
// stable change of the client size moves the backbuffer: to the client size
// (1:1), or back to the configured backbuffer when the client returns to its
// original size. Revival's borderless toggle resizes only the window (4:3
// stretched); following the client letterboxes it too.
struct ResizeState {
    bool initialized = false;
    long origClientW = 0, origClientH = 0;
    long origBbW = 0, origBbH = 0;
    long setClientW = 0, setClientH = 0;  // client size at last setpoint
    long candidateW = 0, candidateH = 0;
    int candidateCount = 0;
    int cooldown = 0;  // flips to skip after issuing a reset
};

struct ResizeInput {
    long clientW, clientH;
    long bbW, bbH;
    int borderless;
};

enum class ResizeAction { None, Reset };

// Minimum client size (rejects minimized windows) and flips a new size must
// hold before committing (8 flips = 125 ms).
enum { kResizeMinClientW = 320, kResizeMinClientH = 240, kResizeStableFlips = 8 };
inline int resizeCooldownFlips() { return 30; }

ResizeAction evaluateResize(ResizeState* st, const ResizeInput& in,
                            long* outBbW, long* outBbH);

}  // namespace dm
