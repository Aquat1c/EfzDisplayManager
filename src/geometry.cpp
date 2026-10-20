#include "geometry.h"

namespace dm {

bool computeOutputSize(int srcW, int srcH, int bbW, int bbH, ScaleMode mode,
                       int intScale, int customW, int customH, long* outW,
                       long* outH) {
    if (srcW <= 0 || srcH <= 0 || bbW <= 0 || bbH <= 0) return false;

    long w = 0;
    long h = 0;
    switch (mode) {
        case ScaleMode::Integer: {
            long maxFit = bbW / srcW;
            if (bbH / srcH < maxFit) maxFit = bbH / srcH;
            if (maxFit < 1) maxFit = 1;
            // intScale 0 (or negative) means "largest multiple that fits".
            long use = intScale < 1 ? maxFit : intScale;
            if (use > maxFit) use = maxFit;
            if (use < 1) use = 1;
            w = long(srcW) * use;
            h = long(srcH) * use;
            break;
        }
        case ScaleMode::Custom: {
            w = customW > 0 ? customW : srcW;
            h = customH > 0 ? customH : srcH;
            break;
        }
        case ScaleMode::Fit:
        default: {
            // Aspect-correct fit with round-to-nearest, using exact integer
            // arithmetic (no floating point anywhere in the hook path).
            // Width-bound when bbW/srcW <= bbH/srcH, i.e. bbW*srcH <= bbH*srcW.
            if (long(bbW) * srcH <= long(bbH) * srcW) {
                w = bbW;
                h = int((2L * srcH * bbW + srcW) / (2L * srcW));
            } else {
                h = bbH;
                w = int((2L * srcW * bbH + srcH) / (2L * srcH));
            }
            break;
        }
    }

    if (w > bbW) w = bbW;
    if (h > bbH) h = bbH;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    *outW = w;
    *outH = h;
    return true;
}

void centerRect(long bbW, long bbH, long outW, long outH, long* left,
                long* top) {
    *left = (bbW - outW) / 2;
    *top = (bbH - outH) / 2;
}

}  // namespace dm
