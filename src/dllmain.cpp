// EFZ DisplayManager - transparent bootstrap stub.
//
// Stage "transparent-bootstrap" of the feasibility handoff: the module loads
// with the game (via EfzModManager or a proxy loader), records a diagnostic
// line, and installs no hooks. The Revival Ddraw.dll's native display path
// remains fully in charge. Hook installation, package admission, and the
// final-scale adapter arrive in later stages; see
// EFZ_DisplayManager_Feasibility_2026-10-20.md section 16.3.

#include <windows.h>

#include "logging.h"
#include "version.h"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(module);
            dm::log::init(module);
            DM_LOG("EFZ DisplayManager v%s (%s): attached, no hooks installed; "
                   "native display path remains in charge",
                   dm::kVersionString, dm::kStageString);
            break;
        case DLL_PROCESS_DETACH:
            DM_LOG("EFZ DisplayManager v%s: detached", dm::kVersionString);
            dm::log::shutdown();
            break;
        default:
            break;
    }
    return TRUE;
}
