#pragma once

namespace dm {

enum class ScaleMode { Fit = 0, Integer = 1, Custom = 2 };

// Output-rectangle size for scaling a srcW x srcH native frame into a
// bbW x bbH output, per SokuDisplayManager's computeOutput semantics:
//  - Fit:     aspect-correct fit, rounded to nearest, clamped to the output.
//  - Integer: integer multiples of the source, clamped to the largest
//             multiple that fits; intScale 0 or below means "largest fit".
//  - Custom:  explicit size, clamped to the output.
// Returns false when the inputs are not usable (zero/negative dims).
bool computeOutputSize(int srcW, int srcH, int bbW, int bbH, ScaleMode mode,
                       int intScale, int customW, int customH, long* outW,
                       long* outH);

// Centers an outW x outH image in bbW x bbH.
void centerRect(long bbW, long bbH, long outW, long outH, long* left,
                long* top);

// Same member layout as a Win32 RECT.
struct Box {
    long left, top, right, bottom;
};

// Writes the up-to-four bars of bbW x bbH not covered by image; returns the
// count. Filling only these costs a quarter of a full-backbuffer fill at 16:9.
int letterboxBars(long bbW, long bbH, const Box& image, Box out[4]);

}  // namespace dm
