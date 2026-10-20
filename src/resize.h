#pragma once

namespace dm {

// When-to-resize decision for making the wrapper's backbuffer follow the
// window. Pure logic, unit-tested.
//
// Coexistence contract with Revival's EfzRevival.ini [Global] settings:
// the configured window/backbuffer pair (e.g. 640x480 window with a 1280x960
// supersampled backbuffer) is recorded on the first flip and left untouched.
// Only a stable user-driven change of the CLIENT size (maximize, restore,
// drag resize) moves the backbuffer: to the new client size (1:1), or back
// to Revival's originally configured backbuffer when the window returns to
// its original client size. The borderless toggle also resizes (the wrapper resizes
// the window but never its backbuffer, which would stretch 4:3 content; we
// follow the client so borderless is letterboxed too).
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

// Minimum sensible client size (rejects minimized/zero rects) and the
// stability window in flips (~8 flips ~= 125 ms) before committing.
enum { kResizeMinClientW = 320, kResizeMinClientH = 240, kResizeStableFlips = 8 };
inline int resizeCooldownFlips() { return 30; }

ResizeAction evaluateResize(ResizeState* st, const ResizeInput& in,
                            long* outBbW, long* outBbH);

}  // namespace dm
