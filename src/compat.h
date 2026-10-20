#pragma once

#include <d3d9.h>

namespace dm {

// Compatibility layer for exclusive fullscreen / backbuffer resize next to
// mods that hold D3D9 DEFAULT-pool resources (efz-training-mode and
// efz_netplay_mod ImGui buffers). IDirect3DDevice9::Reset refuses
// (D3DERR_INVALIDCALL) while such resources are outstanding, so we install
// a per-instance device vtable and track every DEFAULT-pool resource anyone
// creates through the wrapper's device; right before OUR resets the tracked
// foreign resources are force-released so the Reset can succeed. Owners that
// rebuild after resets (both known overlay mods watch the backbuffer size
// and rebind) come back on their own; a resource that is never touched
// again (like the in-tree ResetProbe fixture) is simply leaked cleanly.

// Installs the tracking vtable on the given device (once). Returns false
// when the vtable copy/patch cannot be done; the game keeps working without
// the layer and resets keep the mods gate.
bool compatInstall(IDirect3DDevice9* dev);

// Force-releases every tracked foreign DEFAULT-pool resource. Call right
// before Reset (after releasing OUR OWN resources - those are untracked).
// Returns how many resources were released.
int compatReleaseForeignResources();

// Pauses/resumes tracking so OUR OWN shader-pipeline resources are not
// registered as foreign (they are released explicitly around resets).
void compatSuspendTracking();
void compatResumeTracking();

// True when the layer is active.
bool compatActive();

}  // namespace dm
