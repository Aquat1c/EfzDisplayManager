#include "renderer_reset.h"
#include "compat.h"
#include "logging.h"
#include "reset_helper_signatures.h"
#include "shaders.h"
#include <cstddef>
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
bool g_mandatory = false;
bool g_flipModel = false;

// Adapter reacquisition for exclusive fullscreen (see header).
bool g_swapPending = false;
unsigned long g_swapOrdinal = 0;
// d3dx9core.h D3DXFONT_DESCW. The wrapper creates its font with
// D3DXCreateFontW (a Japanese face on XP), so only the W pair round-trips it.
struct DmFontDescW {
    INT Height; UINT Width; UINT Weight; UINT MipLevels; BOOL Italic;
    BYTE CharSet, OutputPrecision, Quality, PitchAndFamily;
    WCHAR FaceName[32];
};
static_assert(sizeof(DmFontDescW) == 88 && offsetof(DmFontDescW, FaceName) == 24,
              "D3DXFONT_DESCW layout");
using FontDescFn = HRESULT (STDMETHODCALLTYPE*)(void*, DmFontDescW*);
using CreateFontIndirectFn = HRESULT (WINAPI*)(IDirect3DDevice9*, const DmFontDescW*, void**);

IDirect3DSurface9*& staging() {
    return *reinterpret_cast<IDirect3DSurface9**>(g_base + g_profile->rendererObjectRva);
}
IDirect3DSurface9*& native() {
    return *reinterpret_cast<IDirect3DSurface9**>(g_base + g_profile->nativeSurfaceRva);
}
unsigned char* parameters() { return g_base + g_profile->ppRva; }

// Never replace the device vtable pointer, even with an identical copy: it
// wedges the wrapper's Flip (Clear through the copy never returns, black
// window). Reset is intercepted at the reset helper instead.
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
// ID3DXFont vtable slots (d3dx9core.h order, A/W in separate slots):
// 2 Release, 3 GetDevice, 4 GetDescA, 5 GetDescW, 16 OnLostDevice,
// 17 OnResetDevice (16/17 match the wrapper's own reset helper).
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

// Only Reset, TestCooperativeLevel and Release are legal on a lost device, so
// waiting for a contract-less overlay would leave it dead for good.
bool mandatory() { return g_mandatory || g_device->TestCooperativeLevel() != S_OK; }

bool prepare(void*) {
    if (!describe()) { DM_LOG("reset: missing wrapper surface descriptions; refusing destructive work"); return false; }
    ++g_epoch;
    if (!g_epoch) ++g_epoch;
    if (!compatBegin(g_device, g_epoch, mandatory())) {
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

// Re-creates the wrapper's device on the armed adapter. Runs inside actualReset
// after prepare() (font lost, wrapper surfaces released). The old device stays
// untouched until the new one exists.
HRESULT swapDevice(D3DPRESENT_PARAMETERS* pp) {
    D3DDEVICE_CREATION_PARAMETERS creation = {};
    IDirect3D9* d3d = nullptr;
    if (FAILED(g_device->GetCreationParameters(&creation)) ||
        FAILED(g_device->GetDirect3D(&d3d)) || !d3d) return E_FAIL;
    DmFontDescW fontDesc = {};
    const bool carryFont = g_font &&
        SUCCEEDED(reinterpret_cast<FontDescFn>((*reinterpret_cast<void***>(g_font))[5])(g_font, &fontDesc));
    // The wrapper's own IDirect3D9: an exclusive device from a second object
    // would lock the wrapper's object out of fullscreen and video memory.
    IDirect3DDevice9* fresh = nullptr;
    HRESULT hr = d3d->CreateDevice(g_swapOrdinal, creation.DeviceType, creation.hFocusWindow,
                                   creation.BehaviorFlags, pp, &fresh);
    d3d->Release();
    if (FAILED(hr) || !fresh) {
        if (fresh) fresh->Release();
        DM_LOG("reset: device reacquire on adapter %lu failed %08lx",
               g_swapOrdinal, static_cast<long>(hr));
        return FAILED(hr) ? hr : E_FAIL;
    }
    void* newFont = nullptr;
    if (carryFont) {
        HMODULE d3dx = GetModuleHandleA("D3DX9_43.dll");
        auto create = d3dx ? reinterpret_cast<CreateFontIndirectFn>(
            GetProcAddress(d3dx, "D3DXCreateFontIndirectW")) : nullptr;
        if (!create || FAILED(create(fresh, &fontDesc, &newFont))) newFont = nullptr;
    }
    if (g_font) {
        if (!newFont) DM_LOG("reset: font recreation on the new device failed; dropping font");
        fontCall(2);  // release the old font before its device
    }
    g_font = newFont;
    g_fontLost = g_fontRepair = g_fontResetAttempted = false;
    *reinterpret_cast<void**>(g_base + g_profile->fontRva) = newFont;
    *reinterpret_cast<IDirect3DDevice9**>(g_base + g_profile->deviceRva) = fresh;
    IDirect3DDevice9* old = g_device;
    g_device = fresh;
    g_described = false;
    ++g_generation;
    // Non-zero means something else still holds the old device.
    const ULONG oldRefs = old->Release();
    DM_LOG("reset: device reacquired on adapter %lu (old %p -> new %p, old refs left %lu)",
           g_swapOrdinal, old, fresh, oldRefs);
    return S_OK;
}

int32_t actualReset(void*, unsigned char* mutableParameters) {
    // Recheck late-loaded known modules / any participants that partially
    // resumed before a second loss. Never release a foreign resource ourselves.
    if (!compatBegin(g_device, g_epoch, mandatory())) return static_cast<int32_t>(D3DERR_INVALIDCALL);
    if (g_fontResetAttempted) {
        const HRESULT cleanup = fontCall(16);
        if (FAILED(cleanup)) return static_cast<int32_t>(cleanup);
        g_fontResetAttempted = false;
        g_fontLost = true;
    }
    if (g_flipModel) ppUseFlipModel(mutableParameters);
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
    HRESULT hr = S_OK;
    if (g_swapPending) {
        hr = swapDevice(pp);
        g_swapPending = false;
    } else {
        hr = g_device->Reset(pp);
    }
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
void rendererResetUseFlipModel(bool enabled) { g_flipModel = enabled; }
bool rendererResetBusy() { return g_tx.phase != ResetPhase::Idle || g_tx.executing; }
ResetOutcome rendererResetLastOutcome() { return g_outcome; }
unsigned long rendererResetGeneration() { return g_generation; }
bool rendererResetCanRequest() {
    return g_enabled && g_device && GetCurrentThreadId() == g_ownerThread &&
           !rendererResetBusy() && !compatPending();
}

ResetOutcome rendererResetRequest(const unsigned char* desired, const unsigned char* rollback,
                                  const char* reason, bool mandatoryReset) {
    ResetOutcome outcome = ResetOutcome::Refused;
    if (g_enabled && g_device && GetCurrentThreadId() == g_ownerThread && !rendererResetBusy()) {
        g_reason = reason;
        g_mandatory = mandatoryReset;
        g_attempts = 0;
        g_lastAttempt = GetTickCount();
        outcome = g_outcome = resetStart(&g_tx, operations(), desired, rollback);
    }
    // A swap belongs to the request armed right before it; a refused request
    // must not leave it for the next resize or recovery.
    g_swapPending = false;
    return outcome;
}

// D3D9 refuses an independent exclusive device on a subordinate head of an
// adapter group (NumberOfAdaptersInGroup == 0): one GPU driving several
// monitors reports its secondary outputs that way (verified on a two-output
// NVIDIA WDDM system for every parameter combination).
bool rendererResetExclusiveTarget(HWND hwnd, unsigned long wantedRefresh,
                                  unsigned long* ordinal, DisplayMode* mode) {
    if (!g_device || !hwnd) return false;
    IDirect3D9* d3d = nullptr;
    if (FAILED(g_device->GetDirect3D(&d3d)) || !d3d) return false;
    const HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    bool found = false;
    for (UINT i = 0, count = d3d->GetAdapterCount(); i < count; ++i) {
        if (d3d->GetAdapterMonitor(i) != mon) continue;
        D3DCAPS9 caps = {};
        D3DDISPLAYMODE desktop = {};
        if (FAILED(d3d->GetDeviceCaps(i, D3DDEVTYPE_HAL, &caps)) ||
            FAILED(d3d->GetAdapterDisplayMode(i, &desktop)) || !desktop.Width || !desktop.Height) break;
        if (caps.NumberOfAdaptersInGroup == 0) {
            DM_LOG("fullscreen: adapter %u is a subordinate head of adapter %u (no exclusive device possible there)",
                   i, caps.MasterAdapterOrdinal);
            break;
        }
        unsigned long rates[64];
        unsigned rateCount = 0;
        for (UINT k = 0, modes = d3d->GetAdapterModeCount(i, desktop.Format); k < modes && rateCount < 64; ++k) {
            D3DDISPLAYMODE listed = {};
            if (SUCCEEDED(d3d->EnumAdapterModes(i, desktop.Format, k, &listed)) &&
                listed.Width == desktop.Width && listed.Height == desktop.Height)
                rates[rateCount++] = listed.RefreshRate;
        }
        const unsigned long refresh = pickExclusiveRefresh(rates, rateCount, wantedRefresh, desktop.RefreshRate);
        if (!refresh) {
            DM_LOG("fullscreen: no mode of at least %d Hz on adapter %u; exclusive would slow the game",
                   int(kGameFps), i);
            break;
        }
        *ordinal = i;
        mode->w = long(desktop.Width);
        mode->h = long(desktop.Height);
        mode->format = desktop.Format;
        mode->refresh = long(refresh);
        found = true;
        break;
    }
    d3d->Release();
    return found;
}

unsigned long rendererResetDeviceAdapter() {
    D3DDEVICE_CREATION_PARAMETERS creation = {};
    return g_device && SUCCEEDED(g_device->GetCreationParameters(&creation)) ? creation.AdapterOrdinal : 0;
}

void rendererResetSwapAdapter(unsigned long ordinal) {
    g_swapOrdinal = ordinal;
    g_swapPending = true;
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
        // A Reset that keeps failing (e.g. a foreign DEFAULT-pool object) is
        // retried about once a second instead of spinning.
        const DWORD now = GetTickCount();
        if (now - g_lastAttempt < (g_attempts > 20 ? 1000u : 50u)) return false;
        g_lastAttempt = now;
        g_outcome = resetContinue(&g_tx, operations());
        return g_tx.phase == ResetPhase::Idle && staging() && native();
    }
    if (tcl == D3DERR_DEVICENOTRESET) {
        rendererResetRequest(parameters(), nullptr, "device recovery", true);
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
    // A pending transaction is safe: the caller returns from Flip at once.
    // Mandatory: a contract-less overlay never blocks Revival's own resets.
    rendererResetRequest(reinterpret_cast<unsigned char*>(pp), nullptr, "Revival helper", true);
}
}
