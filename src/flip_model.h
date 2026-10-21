#pragma once

#include <windows.h>

namespace dm {

// LowLatencyWindow=1: redirects the wrapper's Direct3DCreate9 import to
// Direct3DCreate9Ex, so its device is created as D3D9Ex with a flip-model
// (FLIPEX) swap chain in windowed and borderless mode. Must run before the
// wrapper creates its device (DllMain). Returns false when the import is
// not found; the wrapper then keeps its classic device.
bool flipModelRedirect(HMODULE ddraw);

// True once a device was created with the flip model.
bool flipModelActive();

}  // namespace dm
