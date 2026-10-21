#pragma once
#include <d3d9.h>
#include <cstdint>

namespace dm {
// Only owners may invalidate their resources. No COM allocation hooks or
// foreign-pointer lists exist in this bridge.
void compatConfigure(bool enabled);
// Loader-lock work: call on an actual request, never per frame.
bool compatCanReset();
// mandatory: the Reset happens anyway (lost device, Revival's own reset), so a
// known module without the contract is skipped instead of refusing; refusing
// would leave the device dead. Contract owners are still quiesced.
bool compatBegin(IDirect3DDevice9* device, uint32_t epoch, bool mandatory);
// ready=false keeps participants quiesced; ready=true resumes them. A failed
// resume is retried while the device is healthy; it never permits unsafe Reset.
bool compatEnd(bool ready);
bool compatPending();
// Any known overlay module loaded, with or without the contract.
bool compatAnyLoaded();
}
