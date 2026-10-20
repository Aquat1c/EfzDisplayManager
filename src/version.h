#pragma once

#define DM_VERSION_MAJOR 0
#define DM_VERSION_MINOR 3
#define DM_VERSION_PATCH 0

#define DM_VERSION_STRING "0.7.2"

// Development stage marker. "exclusive-fullscreen": the final-blit hook now applies
// configured output geometry (aspect-fit / integer / custom, centered),
// an optional Point-filter override, and border fill; source, backbuffer,
// and presentation remain the wrapper's.
#define DM_STAGE_STRING "exclusive-fullscreen"

namespace dm {

inline constexpr const char* kVersionString = DM_VERSION_STRING;
inline constexpr const char* kStageString = DM_STAGE_STRING;

}  // namespace dm
