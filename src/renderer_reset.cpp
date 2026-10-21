#include "renderer_reset.h"
#include "compat.h"
#include "logging.h"
#include "reset_helper_signatures.h"
#include "shaders.h"
#include <cstring>

namespace dm {
namespace {
static_assert(sizeof(D3DPRESENT_PARAMETERS) == kResetParameterBytes, "Win32 PP layout");
using FontFn = HRESULT (STDMETHODCALLTYPE*)(void*);
unsigned char* g_base = nullptr;
const DdrawProfile* g_profile = nullptr;
IDirect3DDevice9* g_device = nullptr;
DWORD g_ownerThread = 0;
bool g_enabled = false;
bool g_described = false;
IDirect3DSurface9* g_describedStaging = nullptr;
IDirect3DSurface9* g_describedNative = nullptr;
D3DSURFACE_DESC g_stagingDesc = {}, g_nativeDesc = {};
void* g_font = nullptr;
bool g_fontLost = false;
bool g_fontRepair = false;
bool g_fontResetAttempted = false;
ResetTransaction g_tx = {};
ResetOutcome g_outcome = ResetOutcome::RequestedReady;
uint32_t g_epoch = 0;
unsigned long g_generation = 0;
DWORD g_lastAttempt = 0;
unsigned long g_attempts = 0;
HRESULT g_lastResetResult = S_OK;
const char* g_reason = "recovery";

IDirect3DSurface9*& staging() {
    return *reinterpret_cast<IDirect3DSurface9**>(g_base + g_profile->rendererObjectRva);
}
IDirect3DSurface9*& native() {
    return *reinterpret_cast<IDirect3DSurface9**>(g_base + g_profile->nativeSurfaceRva);
}
unsigned char* parameters() { return g_base + g_profile->ppRva; }

// Never replace the device vtable pointer, even with a byte-identical copy:
// live testing proved that wedges the wrapper's Flip (Clear entered through
// the copy never returned - no Present, black window). Reset interception
// lives in the reset-helper detour plus the participant contract; Reset
// itself is called through the device's real vtable.
bool bindDevice(IDirect3DDevice9* device) {
    if (!g_enabled || !device) return false;
    const DWORD thread = GetCurrentThreadId();
    if (!g_ownerThread) g_ownerThread = thread;
    if (thread != g_ownerThread) {
        static bool warnedOwner = false;
        if (!warnedOwner) {
            warnedOwner = true;
            DM_LOG("reset: bind refused thread=%lu owner=%lu", thread, g_ownerThread);
        }
        return false;
    }
    if (g_device != device) {
        if (g_tx.phase != ResetPhase::Idle || compatPending()) return false;
        shadersReleaseResources();
        g_device = device;
        g_described = false;
        ++g_generation;
    }
    return true;
}

bool describe() {
    if (!staging() || !native()) {
        static bool warnedNull = false;
        if (!warnedNull) {
            warnedNull = true;
            DM_LOG("reset: describe refusal - surfaces null (staging=%p native=%p)",
                   staging(), native());
        }
        return false;
    }
    if (g_described && staging() == g_describedStaging && native() == g_describedNative) return true;
    D3DSURFACE_DESC s = {}, n = {};
    if (FAILED(staging()->GetDesc(&s)) || FAILED(native()->GetDesc(&n)) ||
        !s.Width || !s.Height || !n.Width || !n.Height) {
        static bool warnedDesc = false;
        if (!warnedDesc) {
            warnedDesc = true;
            DM_LOG("reset: describe refusal - GetDesc failed or zero dims");
        }
        return false;
    }
    // Only the wrapper's own two DEFAULT-pool surfaces are managed here.
    if (s.Pool != D3DPOOL_DEFAULT || n.Pool != D3DPOOL_DEFAULT) {
        static bool warnedPool = false;
        if (!warnedPool) {
            warnedPool = true;
            DM_LOG("reset: describe refusal - pool mismatch staging pool=%u native pool=%u",
                   static_cast<unsigned>(s.Pool), static_cast<unsigned>(n.Pool));
        }
        return false;
    }
    g_stagingDesc = s;
    g_nativeDesc = n;
    g_describedStaging = staging();
    g_describedNative = native();
    g_described = true;
    return true;
}

void releaseSurfaces() {
    IDirect3DSurface9* s = staging();
    IDirect3DSurface9* n = native();
    staging() = nullptr;
    native() = nullptr;
    if (s) s->Release();
    if (n) n->Release();
}
HRESULT fontCall(unsigned index) {
    return reinterpret_cast<FontFn>((*reinterpret_cast<void***>(g_font))[index])(g_font);
}
bool restoreFont() {
    if (!g_font) return true;
    // A failed OnResetDevice may have allocated a partial set. Let its owner
    // dispose of that set before another recreation or a new device Reset.
    if (g_fontResetAttempted) {
        if (FAILED(fontCall(16))) return false;
        g_fontResetAttempted = false;
        g_fontLost = true;
    }
    if (!g_fontLost && !g_fontRepair) return true;
    g_fontResetAttempted = true;
    const HRESULT hr = fontCall(17);
    if (FAILED(hr)) {
        DM_LOG("reset: font OnResetDevice failed %08lx", static_cast<long>(hr));
        return false;
    }
    g_fontLost = g_fontRepair = g_fontResetAttempted = false;
    return true;
}

bool prepare(void*) {
    if (!describe()) { DM_LOG("reset: missing wrapper surface descriptions; refusing destructive work"); return false; }
    ++g_epoch;
    if (!g_epoch) ++g_epoch;
    if (!compatBegin(g_device, g_epoch)) {
        compatEnd(g_device->TestCooperativeLevel() == S_OK);
        return false;
    }
    g_font = *reinterpret_cast<void**>(g_base + g_profile->fontRva);
    if (g_font) {
        const HRESULT hr = fontCall(16);
        if (FAILED(hr)) {
            DM_LOG("reset: font OnLostDevice failed %08lx; aborting before wrapper release", static_cast<long>(hr));
            // D3DX failure does not authorize releasing resources it may own.
            const bool healthy = g_device->TestCooperativeLevel() == S_OK;
            g_fontRepair = true;
            g_fontResetAttempted = false;
            const bool restored = healthy && restoreFont();
            compatEnd(restored);
            return false;
        }
        g_fontLost = true;
        g_fontRepair = false;
        g_fontResetAttempted = false;
    }
    shadersReleaseResources();
    releaseSurfaces();
    compatEnd(false);
    return true;
}

int32_t actualReset(void*, unsigned char* mutableParameters) {
    // Recheck late-loaded known modules / any participants that partially
    // resumed before a second loss. Never release a foreign resource ourselves.
    if (!compatBegin(g_device, g_epoch)) return static_cast<int32_t>(D3DERR_INVALIDCALL);
    if (g_fontResetAttempted) {
        const HRESULT cleanup = fontCall(16);
        if (FAILED(cleanup)) return static_cast<int32_t>(cleanup);
        g_fontResetAttempted = false;
        g_fontLost = true;
    }
    auto* pp = reinterpret_cast<D3DPRESENT_PARAMETERS*>(mutableParameters);
    ++g_attempts;
    const bool logAttempt = g_attempts <= 3 || g_attempts % 60 == 0;
    if (logAttempt) {
        DM_LOG("reset: %s epoch=%lu attempt=%lu thread=%lu windowed=%u size=%ux%u swap=%u interval=%08lx",
               g_reason, static_cast<unsigned long>(g_epoch), g_attempts, GetCurrentThreadId(),
               static_cast<unsigned>(pp->Windowed), pp->BackBufferWidth, pp->BackBufferHeight,
               static_cast<unsigned>(pp->SwapEffect), pp->PresentationInterval);
        DM_LOG("reset: format=%u count=%u ms=%u quality=%lu depth=%u depthfmt=%u flags=%08lx refresh=%u",
               static_cast<unsigned>(pp->BackBufferFormat), pp->BackBufferCount,
               static_cast<unsigned>(pp->MultiSampleType), pp->MultiSampleQuality,
               static_cast<unsigned>(pp->EnableAutoDepthStencil),
               static_cast<unsigned>(pp->AutoDepthStencilFormat), pp->Flags, pp->FullScreen_RefreshRateInHz);
        DM_LOG("reset: device=%p font=%p fontLost=%u hwnd=%p windowThread=%lu foreground=%p staging=%ux%u/%u native=%ux%u/%u",
               g_device, g_font, g_fontLost ? 1u : 0u, pp->hDeviceWindow,
               GetWindowThreadProcessId(pp->hDeviceWindow, nullptr), GetForegroundWindow(),
               g_stagingDesc.Width, g_stagingDesc.Height, static_cast<unsigned>(g_stagingDesc.Format),
               g_nativeDesc.Width, g_nativeDesc.Height, static_cast<unsigned>(g_nativeDesc.Format));
    }
    const HRESULT hr = g_device->Reset(pp);
    if (logAttempt || hr != g_lastResetResult) DM_LOG("reset: result=%08lx", static_cast<long>(hr));
    g_lastResetResult = hr;
    return static_cast<int32_t>(hr);
}

IDirect3DSurface9* createSurface(const D3DSURFACE_DESC& d) {
    IDirect3DSurface9* result = nullptr;
    HRESULT hr;
    if (d.Usage & D3DUSAGE_RENDERTARGET) {
        // Native helper creates a lockable non-MSAA render target.
        hr = g_device->CreateRenderTarget(d.Width, d.Height, d.Format,
            d.MultiSampleType, d.MultiSampleQuality, TRUE, &result, nullptr);
    } else {
        hr = g_device->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format,
                                                  d.Pool, &result, nullptr);
    }
    if (FAILED(hr) || !result) {
        if (result) result->Release();
        DM_LOG("reset: required surface recreation failed %08lx", static_cast<long>(hr));
        return nullptr;
    }
    return result;
}

bool rebuild(void*) {
    // Called only after an observed successful Reset. Preserve successes when
    // the other allocation fails; retry only the missing resource next frame.
    if (!staging()) staging() = createSurface(g_stagingDesc);
    if (!native()) native() = createSurface(g_nativeDesc);
    if (!staging() || !native()) return false;
    return restoreFont();
}

bool finish(void*) {
    std::memcpy(parameters(), g_tx.committed, kResetParameterBytes);
    g_describedStaging = staging();
    g_describedNative = native();
    ++g_generation;
    // An overlay may fail to recreate its own backend; it keeps its gate
    // closed and we retry its resume on later healthy frames.
    compatEnd(true);
    DM_LOG("reset: wrapper ready epoch=%lu generation=%lu", static_cast<unsigned long>(g_epoch), g_generation);
    return true;
}

void losePartial(void*) {
    // Participants that already resumed must be quiesced again before Reset.
    // actualReset refuses if that cannot be completed.
    if (g_font && (!g_fontLost || g_fontResetAttempted)) {
        const HRESULT hr = fontCall(16);
        g_fontLost = true;
        // A failed cleanup must be retried before the next real Reset.
        g_fontResetAttempted = FAILED(hr);
    }
    shadersReleaseResources();
    releaseSurfaces();
    compatEnd(false);
}
ResetOps operations() { return {nullptr, prepare, actualReset, rebuild, finish, losePartial}; }
}

void rendererResetConfigure(unsigned char* base, const DdrawProfile* profile) { g_base = base; g_profile = profile; }
bool rendererResetSignature(const unsigned char** bytes, size_t* length) {
    if (!g_base || !g_profile) return false;
    // Entry byte count is already pinned by each complete wrapper identity.
    const bool j = g_profile->entryByteCount == 7;
    *bytes = j ? dm_reset_audit::kHelperJ : dm_reset_audit::kHelper2020;
    *length = j ? sizeof(dm_reset_audit::kHelperJ) : sizeof(dm_reset_audit::kHelper2020);
    return std::memcmp(g_base + g_profile->resetHelperRva, *bytes, *length) == 0;
}
void rendererResetEnable(bool enabled) { g_enabled = enabled; }
bool rendererResetBusy() { return g_tx.phase != ResetPhase::Idle || g_tx.executing; }
ResetOutcome rendererResetLastOutcome() { return g_outcome; }
unsigned long rendererResetGeneration() { return g_generation; }
bool rendererResetCanRequest() {
    return g_enabled && g_device && GetCurrentThreadId() == g_ownerThread &&
           !rendererResetBusy() && compatCanReset();
}

ResetOutcome rendererResetRequest(const unsigned char* desired, const unsigned char* rollback,
                                  const char* reason) {
    if (!g_enabled || !g_device || GetCurrentThreadId() != g_ownerThread ||
        rendererResetBusy()) return ResetOutcome::Refused;
    g_reason = reason;
    g_attempts = 0;
    g_lastAttempt = GetTickCount();
    g_outcome = resetStart(&g_tx, operations(), desired, rollback);
    return g_outcome;
}

bool rendererResetFrame(IDirect3DDevice9* device) {
    if (!bindDevice(device) || g_tx.executing) return false;
    const HRESULT tcl = device->TestCooperativeLevel();
    if (tcl == D3DERR_DEVICELOST || tcl == D3DERR_DEVICENOTRESET) {
        resetLostAgain(&g_tx, operations());
    }
    if (tcl != S_OK && tcl != D3DERR_DEVICENOTRESET) {
        static bool warnedTcl = false;
        if (!warnedTcl) {
            warnedTcl = true;
            DM_LOG("reset: frame refusal - TCL=%08lx", static_cast<long>(tcl));
        }
        return false;
    }
    if (g_tx.phase != ResetPhase::Idle) {
        const DWORD now = GetTickCount();
        if (now - g_lastAttempt < 50) return false;
        g_lastAttempt = now;
        g_outcome = resetContinue(&g_tx, operations());
        return g_tx.phase == ResetPhase::Idle && staging() && native();
    }
    if (tcl == D3DERR_DEVICENOTRESET) {
        rendererResetRequest(parameters(), nullptr, "device recovery");
        return g_tx.phase == ResetPhase::Idle && g_outcome != ResetOutcome::Refused && staging() && native();
    }
    if (!describe()) return false;
    if (g_fontRepair) {
        if (!restoreFont()) return false;
    }
    if (compatPending()) compatEnd(true);
    return true;
}

extern "C" void __fastcall dmNativeResetHook(void* renderer, void*, D3DPRESENT_PARAMETERS* pp) {
    if (renderer != g_base + g_profile->rendererObjectRva || !pp) return;
    auto* device = *reinterpret_cast<IDirect3DDevice9**>(g_base + g_profile->deviceRva);
    if (!bindDevice(device) || rendererResetBusy()) return;
    const HRESULT tcl = device->TestCooperativeLevel();
    if (tcl != S_OK && tcl != D3DERR_DEVICENOTRESET) return;
    // The old helper ignored Reset's HRESULT and rebuilt unconditionally; a
    // pending transaction is safe because its caller returns from Flip at once.
    rendererResetRequest(reinterpret_cast<unsigned char*>(pp), nullptr, "Revival helper");
}
}
