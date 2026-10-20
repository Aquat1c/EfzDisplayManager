#pragma once

#define DM_VERSION_MAJOR 0
#define DM_VERSION_MINOR 1
#define DM_VERSION_PATCH 0

#define DM_VERSION_STRING "0.1.0"

// Development stage marker from the feasibility handoff (section 16.3).
// "transparent-bootstrap": the DLL loads and logs, installs no hooks, and
// leaves the native display path fully in charge.
#define DM_STAGE_STRING "transparent-bootstrap"

namespace dm {

inline constexpr const char* kVersionString = DM_VERSION_STRING;
inline constexpr const char* kStageString = DM_STAGE_STRING;

}  // namespace dm
