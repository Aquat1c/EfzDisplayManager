// EFZ DisplayManager - Stage A transparent observation module.
//
// Loaded by EfzModManager (mods/DisplayManager/DisplayManager.dll) inside
// the proxy's own DllMain under loader lock, so this DllMain does nothing
// beyond disabling thread callbacks, opening the log, and starting the
// bootstrap worker. The worker admits the exact renderer-split Revival 1.02j
// wrapper by SHA-256 + PE timestamp, then installs a forward-only hook at
// the wrapper's shared final StretchRect call site. The native display path
// stays in charge; see docs/STAGE_A_RESEARCH_2026-10-20.md section 9.

#include <windows.h>

#include "logging.h"
#include "version.h"

namespace dm {
void startBootstrap(HMODULE module);
void shutdownFromDetach();
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(module);
            dm::log::init(module);
            DM_LOG("EFZ DisplayManager v%s (%s): attached, deferring to bootstrap "
                   "worker",
                   dm::kVersionString, dm::kStageString);
            dm::startBootstrap(module);
            break;
        case DLL_PROCESS_DETACH:
            if (reserved == nullptr) {
                dm::shutdownFromDetach();
                DM_LOG("EFZ DisplayManager v%s: detached", dm::kVersionString);
                dm::log::shutdown();
            }
            // reserved != nullptr means process termination: touch nothing.
            break;
        default:
            break;
    }
    return TRUE;
}
