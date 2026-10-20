#pragma once
#include <windows.h>
#include <d3d9.h>
#include "profiles.h"
#include "reset_transaction.h"

namespace dm {
void rendererResetConfigure(unsigned char* base, const DdrawProfile* profile);
bool rendererResetSignature(const unsigned char** bytes, size_t* length);
void rendererResetEnable(bool enabled);
bool rendererResetCanRequest();
bool rendererResetFrame(IDirect3DDevice9* device);
bool rendererResetBusy();
ResetOutcome rendererResetRequest(const unsigned char* desired,
                                  const unsigned char* rollback, const char* reason);
ResetOutcome rendererResetLastOutcome();
unsigned long rendererResetGeneration();
extern "C" void __fastcall dmNativeResetHook(void* renderer, void* unusedEdx,
                                            D3DPRESENT_PARAMETERS* parameters);
}
