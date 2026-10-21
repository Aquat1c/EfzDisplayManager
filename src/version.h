#pragma once

#define DM_VERSION_MAJOR 1
#define DM_VERSION_MINOR 1
#define DM_VERSION_PATCH 1

#define DM_VERSION_STRING "1.1.1"

// Short development-stage suffix shown next to the version in the log.
#define DM_STAGE_STRING "stable"

namespace dm {

inline constexpr const char* kVersionString = DM_VERSION_STRING;
inline constexpr const char* kStageString = DM_STAGE_STRING;

}  // namespace dm
