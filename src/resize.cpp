#include "resize.h"

namespace dm {

ResizeAction evaluateResize(ResizeState* st, const ResizeInput& in,
                            long* outBbW, long* outBbH) {
    if (st->cooldown > 0) {
        --st->cooldown;
        st->candidateCount = 0;
        return ResizeAction::None;
    }
    // Borderless/fullscreen is owned by Revival's toggle and the wrapper's
    // own reset path; stay quiet there.
    if (in.borderless) {
        st->candidateCount = 0;
        return ResizeAction::None;
    }
    // Reject minimized / degenerate windows.
    if (in.clientW < kResizeMinClientW || in.clientH < kResizeMinClientH) {
        st->candidateCount = 0;
        return ResizeAction::None;
    }
    if (!st->initialized) {
        st->origClientW = in.clientW;
        st->origClientH = in.clientH;
        st->origBbW = in.bbW;
        st->origBbH = in.bbH;
        st->setClientW = in.clientW;
        st->setClientH = in.clientH;
        st->initialized = true;
        return ResizeAction::None;
    }
    if (in.clientW == st->setClientW && in.clientH == st->setClientH) {
        st->candidateCount = 0;
        return ResizeAction::None;
    }

    if (in.clientW == st->candidateW && in.clientH == st->candidateH) {
        ++st->candidateCount;
    } else {
        st->candidateW = in.clientW;
        st->candidateH = in.clientH;
        st->candidateCount = 1;
    }
    if (st->candidateCount < kResizeStableFlips) return ResizeAction::None;

    // Stable new client size: follow it 1:1, or restore Revival's configured
    // backbuffer when the window is back at its original size.
    if (in.clientW == st->origClientW && in.clientH == st->origClientH &&
        in.bbW != st->origBbW) {
        *outBbW = st->origBbW;
        *outBbH = st->origBbH;
    } else {
        *outBbW = in.clientW;
        *outBbH = in.clientH;
    }
    st->setClientW = in.clientW;
    st->setClientH = in.clientH;
    st->candidateCount = 0;
    st->cooldown = resizeCooldownFlips();
    return ResizeAction::Reset;
}

}  // namespace dm
