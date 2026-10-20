#pragma once

#define DM_VERSION_MAJOR 0
#define DM_VERSION_MINOR 2
#define DM_VERSION_PATCH 0

#define DM_VERSION_STRING "0.2.0"

// Development stage marker from the feasibility handoff (section 16.3).
// "stage-a-observe": forward-only observation hook at the wrapper's final
// StretchRect; every call is forwarded unchanged and the native display
// path stays fully in charge.
#define DM_STAGE_STRING "stage-a-observe"

namespace dm {

inline constexpr const char* kVersionString = DM_VERSION_STRING;
inline constexpr const char* kStageString = DM_STAGE_STRING;

}  // namespace dm
