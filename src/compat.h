#pragma once
#include <d3d9.h>
#include <cstdint>

namespace dm {
// Only owners may invalidate their resources. No COM allocation hooks or
// foreign-pointer lists exist in this bridge.
void compatConfigure(bool enabled);
bool compatCanReset();
bool compatBegin(IDirect3DDevice9* device, uint32_t epoch);
// ready=false keeps participants quiesced; ready=true resumes them. A failed
// resume is retried while the device is healthy; it never permits unsafe Reset.
bool compatEnd(bool ready);
bool compatPending();
}
