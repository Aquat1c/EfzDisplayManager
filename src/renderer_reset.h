#pragma once
#include <windows.h>
#include <d3d9.h>
#include "fullscreen.h"
#include "profiles.h"
#include "reset_transaction.h"

namespace dm {
void rendererResetConfigure(unsigned char* base, const DdrawProfile* profile);
bool rendererResetSignature(const unsigned char** bytes, size_t* length);
void rendererResetEnable(bool enabled);
// The device is D3D9Ex: every windowed Reset keeps the flip model.
void rendererResetUseFlipModel(bool enabled);
void rendererResetUseDynamicUpload(bool enabled);
bool rendererUploadActive();
HRESULT rendererUploadLock(IDirect3DSurface9* surface, D3DLOCKED_RECT* locked,
                           const RECT* rect, DWORD flags);
HRESULT rendererUploadUnlock(IDirect3DSurface9* surface);
IDirect3DSurface9* rendererUploadSource(IDirect3DSurface9* surface);
// Cheap per-frame gate; the overlay module scan happens inside the request.
bool rendererResetCanRequest();
bool rendererResetFrame(IDirect3DDevice9* device);
bool rendererResetBusy();
// mandatory: see compatBegin. Lost-device recovery is always mandatory.
ResetOutcome rendererResetRequest(const unsigned char* desired,
                                  const unsigned char* rollback, const char* reason,
                                  bool mandatory = false);
ResetOutcome rendererResetLastOutcome();
unsigned long rendererResetGeneration();

// D3D9 goes exclusive on the device's own adapter; the wrapper always uses the
// default one. rendererResetExclusiveTarget finds the adapter owning hwnd's
// monitor that can host an exclusive device, with its desktop mode (refresh
// snapped to its mode list when wantedRefresh != 0). Synchronized presentation
// requires at least 64 Hz. rendererResetSwapAdapter makes only
// the next request re-create the device on that adapter instead of Resetting.
bool rendererResetExclusiveTarget(HWND hwnd, unsigned long wantedRefresh,
                                  unsigned long* ordinal, DisplayMode* mode, bool immediate = false);
unsigned long rendererResetDeviceAdapter();
void rendererResetSwapAdapter(unsigned long ordinal);
extern "C" void __fastcall dmNativeResetHook(void* renderer, void* unusedEdx,
                                            D3DPRESENT_PARAMETERS* parameters);
}
