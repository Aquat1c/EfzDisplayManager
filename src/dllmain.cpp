// EFZ DisplayManager. EfzModManager LoadLibrary's this DLL inside its own
// DllMain under loader lock, so this DllMain does nothing beyond disabling
// thread callbacks, opening the log, and starting the bootstrap worker.

#include <windows.h>

#include "logging.h"
#include "version.h"

namespace dm {
void startBootstrap(HMODULE module);
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
            // Armed hooks pin the module, so only an inert module unloads here.
            if (reserved == nullptr) {
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
