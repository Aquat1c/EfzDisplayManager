// Native Win32 adapter tests. Link the production renderer_reset.cpp,
// reset_transaction.cpp and compat.cpp; no D3D runtime or GPU is used.
//
// Each case runs in a fresh process because the adapter deliberately retains
// its device guards and module ownership for process lifetime. The three
// fixture DLLs live beside this executable, separately from the manual probe.
#include "../src/renderer_reset.h"
#include "../src/compat.h"
#include "../src/logging.h"
#include "../src/reset_helper_signatures.h"
#include "../src/shaders.h"
#include "reset_participant_fixture.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

namespace {
static_assert(sizeof(void*) == 4, "The production adapter requires Win32 x86");
static_assert(sizeof(D3DPRESENT_PARAMETERS) == dm::kResetParameterBytes, "PP ABI");
unsigned g_failures = 0;
const char* g_caseName = "startup";

void fail(const char* expression, int line) {
    std::fprintf(stderr, "%s:%d: %s\n", g_caseName, line, expression);
    ++g_failures;
}
#define CHECK(expression) do { if (!(expression)) fail(#expression, __LINE__); } while (0)

std::wstring executablePath() {
    wchar_t path[32768] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, 32768);
    CHECK(length != 0 && length < 32768);
    return std::wstring(path, length < 32768 ? length : 0);
}

const wchar_t* const kFixtureNames[] = {
    L"efz_training_mode.dll", L"efz_netplay_mod.dll", L"ResetProbe.dll"
};

struct Fixture {
    HMODULE module = nullptr;
    DmResetFixtureState* state = nullptr;

    bool load(unsigned index) {
        CHECK(index < 3);
        if (index >= 3) return false;
        if (module) return true;
        std::wstring path = executablePath();
        const size_t slash = path.find_last_of(L"\\/");
        CHECK(slash != std::wstring::npos);
        if (slash == std::wstring::npos) return false;
        path.resize(slash + 1);
        path += kFixtureNames[index];
        module = LoadLibraryW(path.c_str());
        if (!module) {
            std::fprintf(stderr, "Cannot load fixture %ls (error %lu)\n",
                         path.c_str(), GetLastError());
            CHECK(module != nullptr);
            return false;
        }
        auto control = reinterpret_cast<DmResetFixtureControlFn>(
            GetProcAddress(module, "DM_ResetFixtureControl"));
        CHECK(control != nullptr);
        if (!control) return false;
        state = control();
        CHECK(state != nullptr);
        return state != nullptr;
    }

    void unload() {
        if (module) CHECK(FreeLibrary(module) != FALSE);
        module = nullptr;
        state = nullptr;
    }

    ~Fixture() { if (module) FreeLibrary(module); }
};

struct Context;
Context* g_context = nullptr;
struct FakeDevice { void** table; Context* context; };
struct FakeSurface {
    void** table = nullptr;
    Context* context = nullptr;
    D3DSURFACE_DESC description = {};
    ULONG references = 0;
    unsigned releases = 0;
    unsigned descriptions = 0;
};
struct FakeFont { void** table; Context* context; };
struct FakeTexture { void** table; Context* context; };

HRESULT STDMETHODCALLTYPE cooperativeLevel(IDirect3DDevice9*);
HRESULT STDMETHODCALLTYPE getDirect3D(IDirect3DDevice9*, IDirect3D9**);
HRESULT STDMETHODCALLTYPE creationParameters(IDirect3DDevice9*, D3DDEVICE_CREATION_PARAMETERS*);
HRESULT STDMETHODCALLTYPE resetDevice(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
HRESULT STDMETHODCALLTYPE createRenderTarget(IDirect3DDevice9*, UINT, UINT,
    D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL, IDirect3DSurface9**, HANDLE*);
HRESULT STDMETHODCALLTYPE createOffscreenSurface(IDirect3DDevice9*, UINT, UINT,
    D3DFORMAT, D3DPOOL, IDirect3DSurface9**, HANDLE*);
ULONG STDMETHODCALLTYPE surfaceAddRef(IDirect3DSurface9*);
ULONG STDMETHODCALLTYPE surfaceRelease(IDirect3DSurface9*);
HRESULT STDMETHODCALLTYPE surfaceDescription(IDirect3DSurface9*, D3DSURFACE_DESC*);
HRESULT STDMETHODCALLTYPE deviceCaps(IDirect3DDevice9*, D3DCAPS9*);
HRESULT STDMETHODCALLTYPE createTexture(IDirect3DDevice9*, UINT, UINT, UINT, DWORD,
    D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
HRESULT STDMETHODCALLTYPE textureSurface(IDirect3DTexture9*, UINT, IDirect3DSurface9**);
ULONG STDMETHODCALLTYPE textureRelease(IDirect3DTexture9*);
HRESULT STDMETHODCALLTYPE surfaceLock(IDirect3DSurface9*, D3DLOCKED_RECT*, const RECT*, DWORD);
HRESULT STDMETHODCALLTYPE surfaceUnlock(IDirect3DSurface9*);
HRESULT STDMETHODCALLTYPE fontLost(void*);
HRESULT STDMETHODCALLTYPE fontReset(void*);

struct Context {
    alignas(16) unsigned char wrapper[1024] = {};
    dm::DdrawProfile profile = {};
    void* deviceTable[119] = {};
    void* surfaceTable[17] = {};
    void* fontTable[18] = {};
    void* textureTable[22] = {};
    FakeTexture texture = {textureTable, this};
    unsigned textureReferences = 0, textureCreates = 0, textureReleases = 0;
    bool dynamicCaps = true, textureFailure = false, levelFailure = false;
    DWORD lockFlags = 0;
    HRESULT lockResult = S_OK;
    bool discardFailure = false;
    unsigned lockCalls = 0;
    IDirect3DSurface9* unlocked = nullptr;
    FakeDevice device = {deviceTable, this};
    FakeFont font = {fontTable, this};
    FakeSurface surfaces[32] = {};
    unsigned surfaceCount = 0;
    Fixture participants[3];
    HRESULT cooperative = S_OK;
    HRESULT resetResults[12] = {};
    unsigned resetResultCount = 0;
    D3DPRESENT_PARAMETERS resetInputs[12] = {};
    unsigned resetCalls = 0;
    unsigned successfulResets = 0;
    unsigned offscreenCreates = 0;
    unsigned targetCreates = 0;
    unsigned targetFailuresRemaining = 0;
    unsigned shaderReleases = 0;
    unsigned lostCalls = 0;
    unsigned fontResetCalls = 0;
    unsigned swapQueries = 0;  // device re-creation starts with these calls
    HRESULT lostResult = S_OK;
    HRESULT fontResetResult = S_OK;
    bool fontLive = true;
    bool shadersLive = false;
    bool lastResetFailed = false;
    bool allowHealthyFontCancellation = false;
    bool reenterFromReset = false;
    bool loadOnLost = false;
    bool loadOnFirstReset = false;
    unsigned lateFixtureIndex = 1;
    HRESULT lateBeforeResult = S_OK;

    Context() {
        g_context = this;
        deviceTable[3] = reinterpret_cast<void*>(&cooperativeLevel);
        deviceTable[6] = reinterpret_cast<void*>(&getDirect3D);
        deviceTable[9] = reinterpret_cast<void*>(&creationParameters);
        deviceTable[16] = reinterpret_cast<void*>(&resetDevice);
        deviceTable[7] = reinterpret_cast<void*>(&deviceCaps);
        deviceTable[23] = reinterpret_cast<void*>(&createTexture);
        textureTable[2] = reinterpret_cast<void*>(&textureRelease);
        textureTable[18] = reinterpret_cast<void*>(&textureSurface);
        surfaceTable[13] = reinterpret_cast<void*>(&surfaceLock);
        surfaceTable[14] = reinterpret_cast<void*>(&surfaceUnlock);
        deviceTable[28] = reinterpret_cast<void*>(&createRenderTarget);
        deviceTable[36] = reinterpret_cast<void*>(&createOffscreenSurface);
        surfaceTable[1] = reinterpret_cast<void*>(&surfaceAddRef);
        surfaceTable[2] = reinterpret_cast<void*>(&surfaceRelease);
        surfaceTable[12] = reinterpret_cast<void*>(&surfaceDescription);
        fontTable[16] = reinterpret_cast<void*>(&fontLost);
        fontTable[17] = reinterpret_cast<void*>(&fontReset);
        profile.name = "native reset adapter fixture";
        profile.rendererObjectRva = 0x10;
        profile.nativeSurfaceRva = 0x14;
        profile.ppRva = 0x1c;
        profile.deviceRva = 0x54;
        profile.fontRva = 0x84;
        profile.resetHelperRva = 0x200;
        profile.entryByteCount = 7;
        std::memcpy(wrapper + profile.resetHelperRva, dm_reset_audit::kHelperJ,
                    sizeof(dm_reset_audit::kHelperJ));
        staging() = makeSurface(false, 640, 480);
        native() = makeSurface(true, 640, 480);
        *reinterpret_cast<IDirect3DDevice9**>(wrapper + profile.deviceRva) = comDevice();
        *reinterpret_cast<void**>(wrapper + profile.fontRva) = &font;
        parameters() = desired(640, 480);
    }

    IDirect3DDevice9* comDevice() { return reinterpret_cast<IDirect3DDevice9*>(&device); }
    IDirect3DSurface9*& staging() {
        return *reinterpret_cast<IDirect3DSurface9**>(wrapper + profile.rendererObjectRva);
    }
    IDirect3DSurface9*& native() {
        return *reinterpret_cast<IDirect3DSurface9**>(wrapper + profile.nativeSurfaceRva);
    }
    D3DPRESENT_PARAMETERS& parameters() {
        return *reinterpret_cast<D3DPRESENT_PARAMETERS*>(wrapper + profile.ppRva);
    }

    static D3DPRESENT_PARAMETERS desired(UINT width = 960, UINT height = 720) {
        D3DPRESENT_PARAMETERS pp = {};
        pp.BackBufferWidth = width;
        pp.BackBufferHeight = height;
        pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        pp.BackBufferCount = 1;
        pp.MultiSampleType = D3DMULTISAMPLE_NONE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.Windowed = TRUE;
        pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        return pp;
    }

    IDirect3DSurface9* makeSurface(bool renderTarget, UINT width, UINT height) {
        CHECK(surfaceCount < 32);
        if (surfaceCount == 32) return nullptr;
        FakeSurface& surface = surfaces[surfaceCount++];
        surface.table = surfaceTable;
        surface.context = this;
        surface.references = 1;
        surface.description.Format = D3DFMT_X8R8G8B8;
        surface.description.Type = D3DRTYPE_SURFACE;
        surface.description.Usage = renderTarget ? D3DUSAGE_RENDERTARGET : 0;
        surface.description.Pool = D3DPOOL_DEFAULT;
        surface.description.MultiSampleType = D3DMULTISAMPLE_NONE;
        surface.description.Width = width;
        surface.description.Height = height;
        return reinterpret_cast<IDirect3DSurface9*>(&surface);
    }

    bool initialize() {
        dm::compatConfigure(true);
        dm::rendererResetConfigure(wrapper, &profile);
        dm::rendererResetEnable(true);
        const bool ready = dm::rendererResetFrame(comDevice());
        CHECK(ready);
        // The device vtable pointer must stay untouched (see bindDevice).
        CHECK(device.table == deviceTable);
        CHECK(surfaces[0].descriptions == 1 && surfaces[1].descriptions == 1);
        shaderReleases = 0;
        shadersLive = true;
        return ready;
    }

    void results(std::initializer_list<HRESULT> values) {
        CHECK(values.size() <= 12);
        resetResultCount = 0;
        for (const HRESULT value : values) resetResults[resetResultCount++] = value;
    }

    dm::ResetOutcome request(const D3DPRESENT_PARAMETERS& pp,
                             const D3DPRESENT_PARAMETERS* rollback = nullptr) {
        return dm::rendererResetRequest(reinterpret_cast<const unsigned char*>(&pp),
            reinterpret_cast<const unsigned char*>(rollback), "native adapter test");
    }

    bool frame() {
        // Real 50 ms retry throttle. 80 ms clears it despite 15.6 ms
        // GetTickCount granularity (60 ms can measure as ~47 ms).
        Sleep(80);
        return dm::rendererResetFrame(comDevice());
    }

    void loadLate() {
        if (participants[lateFixtureIndex].load(lateFixtureIndex))
            participants[lateFixtureIndex].state->beforeResult = lateBeforeResult;
    }

    void checkParticipants() {
        for (unsigned i = 0; i < 3; ++i) {
            auto* state = participants[i].state;
            if (!state) continue;
            CHECK(state->violations == 0);
            if (state->beforeCalls) CHECK(state->beforeThread == GetCurrentThreadId());
            if (state->afterReadyCalls || state->afterNotReadyCalls)
                CHECK(state->afterThread == GetCurrentThreadId());
        }
    }
};

Context& context(IDirect3DDevice9* device) {
    return *reinterpret_cast<FakeDevice*>(device)->context;
}

HRESULT STDMETHODCALLTYPE cooperativeLevel(IDirect3DDevice9* device) {
    return context(device).cooperative;
}

HRESULT STDMETHODCALLTYPE getDirect3D(IDirect3DDevice9* device, IDirect3D9** result) {
    ++context(device).swapQueries;
    *result = nullptr;
    return E_FAIL;
}

HRESULT STDMETHODCALLTYPE creationParameters(IDirect3DDevice9* device,
                                             D3DDEVICE_CREATION_PARAMETERS* result) {
    ++context(device).swapQueries;
    *result = {};
    return E_FAIL;
}

HRESULT STDMETHODCALLTYPE resetDevice(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* pp) {
    Context& c = context(device);
    CHECK(pp != nullptr);
    CHECK(c.staging() == nullptr && c.native() == nullptr);
    CHECK(!c.fontLive && !c.shadersLive);
    CHECK(c.textureReferences == 0);
    for (unsigned i = 0; i < c.surfaceCount; ++i) CHECK(c.surfaces[i].references == 0);
    for (unsigned i = 0; i < 2; ++i)
        if (c.participants[i].state) CHECK(c.participants[i].state->quiesced == 1);
    CHECK(c.resetCalls < 12);
    const unsigned index = c.resetCalls++;
    if (index < 12) c.resetInputs[index] = *pp;
    if (c.reenterFromReset) {
        CHECK(c.request(*pp) == dm::ResetOutcome::Refused);
        CHECK(!dm::rendererResetFrame(device));
    }
    const HRESULT result = index < c.resetResultCount ? c.resetResults[index] : S_OK;
    // D3D may rewrite this in/out structure even when it fails. Every retry and
    // rollback must receive a fresh copy of the canonical requested parameters.
    std::memset(pp, 0xa5, sizeof(*pp));
    c.lastResetFailed = FAILED(result);
    c.cooperative = FAILED(result) ? D3DERR_DEVICENOTRESET : S_OK;
    if (SUCCEEDED(result)) ++c.successfulResets;
    if (c.loadOnFirstReset && index == 0) c.loadLate();
    return result;
}

HRESULT STDMETHODCALLTYPE createRenderTarget(IDirect3DDevice9* device, UINT width, UINT height,
    D3DFORMAT format, D3DMULTISAMPLE_TYPE multisample, DWORD quality, BOOL lockable,
    IDirect3DSurface9** result, HANDLE* shared) {
    Context& c = context(device);
    ++c.targetCreates;
    CHECK(!c.lastResetFailed && c.successfulResets != 0 && c.cooperative == S_OK);
    CHECK(width == 640 && height == 480 && format == D3DFMT_X8R8G8B8);
    CHECK(multisample == D3DMULTISAMPLE_NONE && quality == 0 && lockable == TRUE);
    CHECK(shared == nullptr && result != nullptr);
    if (c.targetFailuresRemaining) {
        --c.targetFailuresRemaining;
        *result = nullptr;
        return E_OUTOFMEMORY;
    }
    *result = c.makeSurface(true, width, height);
    return *result ? S_OK : E_OUTOFMEMORY;
}

HRESULT STDMETHODCALLTYPE createOffscreenSurface(IDirect3DDevice9* device, UINT width, UINT height,
    D3DFORMAT format, D3DPOOL pool, IDirect3DSurface9** result, HANDLE* shared) {
    Context& c = context(device);
    ++c.offscreenCreates;
    CHECK(!c.lastResetFailed && c.successfulResets != 0 && c.cooperative == S_OK);
    CHECK(width == 640 && height == 480 && format == D3DFMT_X8R8G8B8);
    CHECK(pool == D3DPOOL_DEFAULT && shared == nullptr && result != nullptr);
    *result = c.makeSurface(false, width, height);
    return *result ? S_OK : E_OUTOFMEMORY;
}

HRESULT STDMETHODCALLTYPE deviceCaps(IDirect3DDevice9* device, D3DCAPS9* caps) {
    *caps = {};
    if (context(device).dynamicCaps) {
        caps->Caps2 = D3DCAPS2_DYNAMICTEXTURES;
        caps->DevCaps2 = D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES;
    }
    return S_OK;
}
HRESULT STDMETHODCALLTYPE createTexture(IDirect3DDevice9* device, UINT w, UINT h, UINT levels,
    DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DTexture9** out, HANDLE* shared) {
    auto& c = context(device);
    ++c.textureCreates;
    CHECK(w == 640 && h == 480 && levels == 1 && usage == D3DUSAGE_DYNAMIC);
    CHECK(format == D3DFMT_X8R8G8B8 && pool == D3DPOOL_DEFAULT && !shared);
    *out = nullptr;
    if (c.textureFailure) return E_OUTOFMEMORY;
    CHECK(c.textureReferences == 0);
    c.textureReferences = 1;
    *out = reinterpret_cast<IDirect3DTexture9*>(&c.texture);
    return S_OK;
}
HRESULT STDMETHODCALLTYPE textureSurface(IDirect3DTexture9* tex, UINT level, IDirect3DSurface9** out) {
    auto& c = *reinterpret_cast<FakeTexture*>(tex)->context;
    CHECK(level == 0);
    *out = nullptr;
    if (c.levelFailure) return E_FAIL;
    *out = c.makeSurface(false, 640, 480);
    reinterpret_cast<FakeSurface*>(*out)->description.Usage = D3DUSAGE_DYNAMIC;
    return S_OK;
}
ULONG STDMETHODCALLTYPE textureRelease(IDirect3DTexture9* tex) {
    auto& c = *reinterpret_cast<FakeTexture*>(tex)->context;
    CHECK(c.textureReferences == 1);
    ++c.textureReleases;
    return --c.textureReferences;
}
HRESULT STDMETHODCALLTYPE surfaceLock(IDirect3DSurface9* self, D3DLOCKED_RECT*, const RECT*, DWORD flags) {
    auto& c = *reinterpret_cast<FakeSurface*>(self)->context;
    c.lockFlags = flags;
    ++c.lockCalls;
    if (c.discardFailure && flags == D3DLOCK_DISCARD) return E_FAIL;
    return c.lockResult;
}
HRESULT STDMETHODCALLTYPE surfaceUnlock(IDirect3DSurface9* self) {
    reinterpret_cast<FakeSurface*>(self)->context->unlocked = self;
    return S_OK;
}

ULONG STDMETHODCALLTYPE surfaceAddRef(IDirect3DSurface9* self) {
    auto& surface = *reinterpret_cast<FakeSurface*>(self);
    CHECK(surface.references != 0);
    return ++surface.references;
}

ULONG STDMETHODCALLTYPE surfaceRelease(IDirect3DSurface9* self) {
    auto& surface = *reinterpret_cast<FakeSurface*>(self);
    CHECK(surface.references == 1);
    ++surface.releases;
    if (surface.references) --surface.references;
    return surface.references;
}

HRESULT STDMETHODCALLTYPE surfaceDescription(IDirect3DSurface9* self, D3DSURFACE_DESC* description) {
    auto& surface = *reinterpret_cast<FakeSurface*>(self);
    ++surface.descriptions;
    CHECK(surface.references != 0 && !surface.context->lastResetFailed);
    *description = surface.description;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE fontLost(void* self) {
    Context& c = *static_cast<FakeFont*>(self)->context;
    ++c.lostCalls;
    if (c.loadOnLost && c.lostCalls == 1) c.loadLate();
    if (SUCCEEDED(c.lostResult)) c.fontLive = false;
    return c.lostResult;
}

HRESULT STDMETHODCALLTYPE fontReset(void* self) {
    Context& c = *static_cast<FakeFont*>(self)->context;
    ++c.fontResetCalls;
    CHECK(!c.lastResetFailed && c.cooperative == S_OK);
    CHECK(c.successfulResets != 0 || c.allowHealthyFontCancellation);
    // A failing font restoration may still own partial DEFAULT-pool objects.
    // A later restore or Reset must first call its owner to dispose of them.
    CHECK(!c.fontLive || c.allowHealthyFontCancellation);
    c.fontLive = true;
    return c.fontResetResult;
}

bool sameParameters(const D3DPRESENT_PARAMETERS& a, const D3DPRESENT_PARAMETERS& b) {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

void checkOriginalsReleasedOnce(const Context& c) {
    CHECK(c.surfaces[0].references == 0 && c.surfaces[1].references == 0);
    CHECK(c.surfaces[0].releases == 1 && c.surfaces[1].releases == 1);
    CHECK(c.surfaces[0].descriptions == 1 && c.surfaces[1].descriptions == 1);
}

void successfulReset() {
    Context c;
    if (!c.initialize() || !c.participants[0].load(0)) return;
    c.reenterFromReset = true;
    const auto pp = Context::desired();
    const unsigned long generation = dm::rendererResetGeneration();
    CHECK(dm::rendererResetCanRequest());
    CHECK(c.request(pp) == dm::ResetOutcome::RequestedReady);
    CHECK(c.resetCalls == 1 && c.successfulResets == 1);
    CHECK(c.offscreenCreates == 1 && c.targetCreates == 1);
    CHECK(c.lostCalls == 1 && c.fontResetCalls == 1 && c.shaderReleases == 1);
    CHECK(sameParameters(c.resetInputs[0], pp) && sameParameters(c.parameters(), pp));
    CHECK(c.staging() != nullptr && c.native() != nullptr && !dm::rendererResetBusy());
    CHECK(dm::rendererResetGeneration() == generation + 1);
    CHECK(!dm::compatPending());
    CHECK(c.participants[0].state->beforeCalls == 1);
    CHECK(c.participants[0].state->afterReadyCalls == 1);
    checkOriginalsReleasedOnce(c);
    c.checkParticipants();
}

void deviceReplacedAtSameAddress() {
    Context c;
    if (!c.initialize()) return;
    CHECK(!dm::rendererResetBusy() && !dm::compatPending());
    IDirect3DDevice9* const reusedAddress = c.comDevice();
    const unsigned long generation = dm::rendererResetGeneration();

    // A fresh device at the same address is indistinguishable from the old
    // one: the frame gate passes and the description cache picks up the new
    // surfaces.
    c.staging()->Release();
    c.native()->Release();
    c.staging() = c.makeSurface(false, 640, 480);
    c.native() = c.makeSurface(true, 640, 480);
    auto* freshStaging = reinterpret_cast<FakeSurface*>(c.staging());
    auto* freshNative = reinterpret_cast<FakeSurface*>(c.native());
    CHECK(c.comDevice() == reusedAddress && c.shadersLive);

    CHECK(dm::rendererResetFrame(reusedAddress));
    CHECK(c.device.table == c.deviceTable);
    CHECK(dm::rendererResetGeneration() == generation);
    CHECK(freshStaging->descriptions == 1 && freshNative->descriptions == 1);
    CHECK(dm::rendererResetCanRequest());

    // A subsequent managed reset still works end to end.
    CHECK(c.request(Context::desired()) == dm::ResetOutcome::RequestedReady);
    CHECK(c.resetCalls == 1 && c.successfulResets == 1);
    CHECK(c.offscreenCreates == 1 && c.targetCreates == 1);
    CHECK(freshStaging->releases == 1 && freshNative->releases == 1);
    CHECK(dm::rendererResetGeneration() == generation + 1);
    checkOriginalsReleasedOnce(c);
}

void failedRollbackRetry() {
    Context c;
    if (!c.initialize() || !c.participants[0].load(0)) return;
    c.results({D3DERR_INVALIDCALL, D3DERR_INVALIDCALL, S_OK});
    const auto pp = Context::desired();
    const auto previous = c.parameters();
    CHECK(c.request(pp, &previous) == dm::ResetOutcome::Pending);
    CHECK(c.resetCalls == 2 && c.successfulResets == 0);
    CHECK(c.offscreenCreates == 0 && c.targetCreates == 0 && c.fontResetCalls == 0);
    CHECK(c.staging() == nullptr && c.native() == nullptr && dm::rendererResetBusy());
    CHECK(c.participants[0].state->afterReadyCalls == 0);
    CHECK(sameParameters(c.parameters(), previous));
    CHECK(c.frame());
    CHECK(dm::rendererResetLastOutcome() == dm::ResetOutcome::RestoredReady);
    CHECK(c.resetCalls == 3 && c.lostCalls == 1 && c.shaderReleases == 1);
    CHECK(sameParameters(c.resetInputs[0], pp));
    CHECK(sameParameters(c.resetInputs[1], previous));
    CHECK(sameParameters(c.resetInputs[2], previous));
    CHECK(sameParameters(c.parameters(), previous));
    CHECK(c.participants[0].state->beforeCalls == 1);
    CHECK(c.participants[0].state->afterReadyCalls == 1);
    checkOriginalsReleasedOnce(c);
    c.checkParticipants();
}

void repeatedFailure() {
    Context c;
    if (!c.initialize()) return;
    c.results({D3DERR_INVALIDCALL, D3DERR_INVALIDCALL, S_OK});
    const auto pp = Context::desired();
    CHECK(c.request(pp) == dm::ResetOutcome::Pending);
    c.cooperative = D3DERR_DEVICELOST;
    CHECK(!c.frame());
    CHECK(c.resetCalls == 1 && c.fontResetCalls == 0 && c.offscreenCreates == 0);
    c.cooperative = D3DERR_DEVICENOTRESET;
    CHECK(!c.frame());
    CHECK(c.resetCalls == 2 && c.fontResetCalls == 0 && c.offscreenCreates == 0);
    CHECK(c.lostCalls == 1 && c.shaderReleases == 1);
    CHECK(c.frame());
    CHECK(c.resetCalls == 3 && dm::rendererResetLastOutcome() == dm::ResetOutcome::RequestedReady);
    for (unsigned i = 0; i < 3; ++i) CHECK(sameParameters(c.resetInputs[i], pp));
    CHECK(sameParameters(c.parameters(), pp));
    checkOriginalsReleasedOnce(c);
}

void partialRecreate() {
    Context c;
    if (!c.initialize() || !c.participants[0].load(0)) return;
    c.targetFailuresRemaining = 1;
    const auto pp = Context::desired();
    const auto previous = c.parameters();
    CHECK(c.request(pp) == dm::ResetOutcome::Pending);
    CHECK(c.resetCalls == 1 && c.successfulResets == 1);
    CHECK(c.staging() != nullptr && c.native() == nullptr && c.fontResetCalls == 0);
    CHECK(c.participants[0].state->afterReadyCalls == 0);
    CHECK(sameParameters(c.parameters(), previous));
    auto* partial = c.staging();
    CHECK(c.frame());
    CHECK(c.resetCalls == 1 && c.offscreenCreates == 1 && c.targetCreates == 2);
    CHECK(c.staging() == partial && reinterpret_cast<FakeSurface*>(partial)->releases == 0);
    CHECK(c.fontResetCalls == 1 && c.lostCalls == 1);
    CHECK(c.participants[0].state->afterReadyCalls == 1);
    checkOriginalsReleasedOnce(c);
    c.checkParticipants();
}

void nativeHelper() {
    Context c;
    if (!c.initialize()) return;
    c.results({D3DERR_INVALIDCALL, S_OK});
    auto pp = Context::desired();
    const auto expected = pp;
    dm::dmNativeResetHook(c.wrapper + c.profile.rendererObjectRva + 4,
                         reinterpret_cast<void*>(0x12345678), &pp);
    CHECK(c.resetCalls == 0);
    dm::dmNativeResetHook(c.wrapper + c.profile.rendererObjectRva,
                         reinterpret_cast<void*>(0x12345678), &pp);
    CHECK(c.resetCalls == 1 && dm::rendererResetBusy());
    CHECK(c.fontResetCalls == 0 && c.offscreenCreates == 0);
    CHECK(sameParameters(pp, expected));
    dm::dmNativeResetHook(c.wrapper + c.profile.rendererObjectRva, nullptr, &pp);
    CHECK(c.resetCalls == 1);
    CHECK(c.frame());
    CHECK(c.resetCalls == 2 && sameParameters(c.parameters(), expected));
    CHECK(c.lostCalls == 1 && c.fontResetCalls == 1);
    checkOriginalsReleasedOnce(c);
}

void lateCooperativeModule() {
    Context c;
    if (!c.initialize() || !c.participants[0].load(0)) return;
    c.loadOnLost = true;
    CHECK(c.request(Context::desired()) == dm::ResetOutcome::RequestedReady);
    CHECK(c.participants[1].state != nullptr);
    if (!c.participants[1].state) return;
    CHECK(c.resetCalls == 1);
    for (unsigned i = 0; i < 2; ++i) {
        CHECK(c.participants[i].state->beforeCalls == 1);
        CHECK(c.participants[i].state->afterReadyCalls == 1);
        CHECK(c.participants[i].state->quiesced == 0);
    }
    c.checkParticipants();
}

void lateRefusalAfterFailure() {
    Context c;
    if (!c.initialize() || !c.participants[0].load(0)) return;
    c.results({D3DERR_INVALIDCALL, S_OK});
    c.loadOnFirstReset = true;
    c.lateBeforeResult = E_FAIL;
    CHECK(c.request(Context::desired()) == dm::ResetOutcome::Pending);
    CHECK(c.participants[1].state != nullptr);
    if (!c.participants[1].state) return;
    CHECK(!c.frame());
    CHECK(c.resetCalls == 1 && c.fontResetCalls == 0 && c.offscreenCreates == 0);
    CHECK(c.participants[0].state->afterReadyCalls == 0);
    CHECK(c.participants[0].state->quiesced == 1 && dm::compatPending());
    CHECK(c.participants[1].state->beforeCalls == 1);
    c.participants[1].state->beforeResult = S_OK;
    CHECK(c.frame());
    CHECK(c.resetCalls == 2 && c.lostCalls == 1 && c.shaderReleases == 1);
    CHECK(c.participants[0].state->beforeCalls == 1);
    CHECK(c.participants[0].state->afterReadyCalls == 1);
    CHECK(c.participants[1].state->beforeCalls == 2);
    CHECK(c.participants[1].state->afterReadyCalls == 1);
    checkOriginalsReleasedOnce(c);
    c.checkParticipants();
}

void lateLegacyModuleDuringLoss() {
    Context c;
    if (!c.initialize() || !c.participants[0].load(0)) return;
    c.results({D3DERR_INVALIDCALL, S_OK});
    c.loadOnFirstReset = true;
    c.lateFixtureIndex = 2;
    CHECK(c.request(Context::desired()) == dm::ResetOutcome::Pending);
    CHECK(c.participants[2].module != nullptr);
    if (!c.participants[2].module) return;
    CHECK(GetProcAddress(c.participants[2].module, "EFZ_QueryOverlayResetApiV1") == nullptr);
    // The failed Reset left the device lost: waiting for the contract-less
    // module would keep it dead, so the retry proceeds; owners still resume.
    CHECK(c.frame());
    CHECK(c.resetCalls == 2 && c.participants[0].state->afterReadyCalls == 1);
    CHECK(!dm::compatPending());
    // Voluntary resets stay blocked while it is loaded.
    CHECK(c.request(Context::desired(800, 600)) == dm::ResetOutcome::Refused);
    CHECK(c.resetCalls == 2);
    checkOriginalsReleasedOnce(c);
    c.checkParticipants();
}

void legacyModuleRecovery() {
    Context c;
    if (!c.initialize() || !c.participants[2].load(2)) return;
    CHECK(c.request(Context::desired()) == dm::ResetOutcome::Refused);
    CHECK(c.resetCalls == 0 && c.lostCalls == 0);
    // A lost device and Revival's own helper are reset regardless.
    c.cooperative = D3DERR_DEVICENOTRESET;
    CHECK(c.frame());
    CHECK(c.resetCalls == 1 && c.successfulResets == 1);
    CHECK(sameParameters(c.resetInputs[0], c.parameters()));
    auto pp = c.parameters();
    dm::dmNativeResetHook(c.wrapper + c.profile.rendererObjectRva, nullptr, &pp);
    CHECK(c.resetCalls == 2 && !dm::rendererResetBusy() && !dm::compatPending());
    // A fullscreen toggle resets like Revival's own Alt+Enter; a resize does not.
    const auto fullscreen = Context::desired(800, 600);
    CHECK(c.request(fullscreen) == dm::ResetOutcome::Refused);
    CHECK(dm::rendererResetRequest(reinterpret_cast<const unsigned char*>(&fullscreen), nullptr,
                                   "window transition", true) == dm::ResetOutcome::RequestedReady);
    CHECK(c.resetCalls == 3 && sameParameters(c.parameters(), fullscreen));
    checkOriginalsReleasedOnce(c);
}

void staleSwapArm() {
    Context c;
    if (!c.initialize() || !c.participants[0].load(0)) return;
    // A swap armed for a refused request must not fire on the next one.
    c.participants[0].state->beforeResult = E_FAIL;
    dm::rendererResetSwapAdapter(1);
    CHECK(c.request(Context::desired()) == dm::ResetOutcome::Refused);
    c.participants[0].state->beforeResult = S_OK;
    CHECK(c.request(Context::desired()) == dm::ResetOutcome::RequestedReady);
    CHECK(c.swapQueries == 0 && c.resetCalls == 1);
    // An armed swap is tried once; a retry after it fails is a plain Reset.
    dm::rendererResetSwapAdapter(1);
    CHECK(c.request(Context::desired(800, 600)) == dm::ResetOutcome::Pending);
    CHECK(c.swapQueries == 1 && c.resetCalls == 1);
    CHECK(c.frame());
    CHECK(c.swapQueries == 1 && c.resetCalls == 2);
    c.checkParticipants();
}

void flipModelReset() {
    Context c;
    if (!c.initialize()) return;
    // A D3D9Ex device keeps FLIPEX on every windowed Reset; the wrapper's
    // stored parameters stay the canonical request.
    dm::rendererResetUseFlipModel(true);
    auto pp = Context::desired();
    pp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
    CHECK(c.request(pp) == dm::ResetOutcome::RequestedReady);
    CHECK(c.resetCalls == 1);
    CHECK(c.resetInputs[0].SwapEffect == D3DSWAPEFFECT_FLIPEX && c.resetInputs[0].BackBufferCount == 2 &&
          c.resetInputs[0].Flags == 0);
    CHECK(sameParameters(c.parameters(), pp));
    auto exclusive = Context::desired(1920, 1080);
    exclusive.Windowed = FALSE;
    exclusive.SwapEffect = D3DSWAPEFFECT_FLIPEX;  // e.g. Revival's own toggle of a stored FLIPEX
    CHECK(c.request(exclusive) == dm::ResetOutcome::RequestedReady);
    CHECK(c.resetInputs[1].SwapEffect == D3DSWAPEFFECT_DISCARD);
    dm::rendererResetUseFlipModel(false);
}

void dynamicUpload() {
    Context c;
    if (!c.initialize()) return;
    auto* original = c.staging();
    auto* native = c.native();
    dm::rendererResetUseDynamicUpload(true);
    CHECK(dm::rendererResetFrame(c.comDevice()));
    CHECK(c.staging() != original && c.native() == native && c.textureCreates == 1);
    D3DLOCKED_RECT locked = {};
    CHECK(dm::rendererUploadLock(c.staging(), &locked, nullptr, 0) == S_OK);
    CHECK(c.lockFlags == D3DLOCK_DISCARD);
    CHECK(dm::rendererUploadLock(native, &locked, nullptr, D3DLOCK_READONLY) == S_OK);
    CHECK(c.lockFlags == D3DLOCK_READONLY);
    CHECK(dm::rendererResetFrame(c.comDevice()));
    CHECK(c.textureCreates == 1);
    CHECK(c.request(Context::desired()) == dm::ResetOutcome::RequestedReady);
    CHECK(c.textureReleases == 1 && c.textureReferences == 0);
    CHECK(dm::rendererResetFrame(c.comDevice()));
    CHECK(c.textureCreates == 2 && c.native() != native);
    CHECK(c.request(Context::desired()) == dm::ResetOutcome::RequestedReady);
}
void dynamicUploadFailure() {
    Context c;
    if (!c.initialize()) return;
    auto* original = c.staging();
    dm::rendererResetUseDynamicUpload(true);
    c.textureFailure = true;
    CHECK(dm::rendererResetFrame(c.comDevice()));
    CHECK(c.staging() == original && c.textureReferences == 0);
    CHECK(dm::rendererResetFrame(c.comDevice()));
    CHECK(c.textureCreates == 1); // failed allocation is not retried every frame
    CHECK(c.request(Context::desired()) == dm::ResetOutcome::RequestedReady);
    c.textureFailure = false;
    c.levelFailure = true;
    original = c.staging();
    CHECK(dm::rendererResetFrame(c.comDevice()));
    CHECK(c.staging() == original && c.textureReferences == 0 && c.textureReleases == 1);
    CHECK(c.request(Context::desired()) == dm::ResetOutcome::RequestedReady);
    c.levelFailure = false;
    c.dynamicCaps = false;
    original = c.staging();
    CHECK(dm::rendererResetFrame(c.comDevice()));
    CHECK(c.staging() == original && c.textureCreates == 2);
}

void dynamicUploadLockFailure() {
    Context c;
    if (!c.initialize()) return;
    auto* original = c.staging();
    dm::rendererResetUseDynamicUpload(true);
    CHECK(c.frame());
    auto* cached = c.staging();
    c.discardFailure = true;
    D3DLOCKED_RECT locked = {};
    CHECK(dm::rendererUploadLock(cached, &locked, nullptr, 0) == S_OK);
    CHECK(c.lockCalls == 2 && c.lockFlags == 0 && c.staging() == original);
    CHECK(!dm::rendererUploadActive());
    CHECK(dm::rendererUploadUnlock(cached) == S_OK && c.unlocked == original);
    CHECK(dm::rendererUploadSource(cached) == original);
    CHECK(dm::rendererUploadLock(original, &locked, nullptr, 0) == S_OK && c.lockCalls == 3);
    CHECK(c.request(Context::desired()) == dm::ResetOutcome::RequestedReady);
    CHECK(c.textureReferences == 0 && reinterpret_cast<FakeSurface*>(cached)->releases == 1);
    CHECK(reinterpret_cast<FakeSurface*>(original)->releases == 1);
}

void dynamicUploadDeviceReplacement() {
    Context c;
    if (!c.initialize()) return;
    auto* original = c.staging();
    dm::rendererResetUseDynamicUpload(true);
    CHECK(c.frame());
    // A wrapper-owned device replacement releases its own surface references.
    c.staging()->Release();
    c.native()->Release();
    c.staging() = c.makeSurface(false, 640, 480);
    c.native() = c.makeSurface(true, 640, 480);
    FakeDevice fresh = {c.deviceTable, &c};
    CHECK(dm::rendererResetFrame(reinterpret_cast<IDirect3DDevice9*>(&fresh)));
    CHECK(c.textureReleases == 1 && c.textureCreates == 2);
    CHECK(reinterpret_cast<FakeSurface*>(original)->releases == 1);
    CHECK(c.request(Context::desired()) == dm::ResetOutcome::RequestedReady);
    CHECK(c.textureReferences == 0);
}

void healthyPrepareRefusal() {
    Context c;
    if (!c.initialize() || !c.participants[0].load(0) || !c.participants[1].load(1)) return;
    c.participants[1].state->beforeResult = E_FAIL;
    CHECK(c.request(Context::desired()) == dm::ResetOutcome::Refused);
    CHECK(c.resetCalls == 0 && c.lostCalls == 0 && c.shaderReleases == 0);
    CHECK(c.surfaces[0].releases == 0 && c.surfaces[1].releases == 0);
    CHECK(c.participants[0].state->beforeCalls == 1);
    CHECK(c.participants[0].state->afterReadyCalls == 1);
    CHECK(c.participants[1].state->afterReadyCalls == 0);
    CHECK(!dm::rendererResetBusy() && !dm::compatPending());
    CHECK(c.frame());
    c.checkParticipants();
}

void failedFontCancellation() {
    Context c;
    if (!c.initialize() || !c.participants[0].load(0)) return;
    c.allowHealthyFontCancellation = true;
    c.lostResult = E_FAIL;
    c.fontResetResult = E_OUTOFMEMORY;
    CHECK(c.request(Context::desired()) == dm::ResetOutcome::Refused);
    CHECK(c.resetCalls == 0 && c.fontResetCalls == 1 && c.shaderReleases == 0);
    CHECK(c.surfaces[0].releases == 0 && c.surfaces[1].releases == 0);
    CHECK(c.participants[0].state->afterReadyCalls == 0 && dm::compatPending());
    CHECK(!c.frame()); // A failed cleanup must also retain the repair gate.
    CHECK(c.fontResetCalls == 1 && c.participants[0].state->afterReadyCalls == 0);
    c.lostResult = S_OK;
    c.fontResetResult = S_OK;
    CHECK(c.frame());
    CHECK(c.resetCalls == 0 && c.fontResetCalls == 2 && c.shaderReleases == 0);
    CHECK(c.participants[0].state->afterReadyCalls == 1 && !dm::compatPending());
    c.checkParticipants();
}

void partialFontRetry() {
    Context c;
    if (!c.initialize()) return;
    c.fontResetResult = E_OUTOFMEMORY;
    CHECK(c.request(Context::desired()) == dm::ResetOutcome::Pending);
    CHECK(c.resetCalls == 1 && c.lostCalls == 1 && c.fontResetCalls == 1);
    c.fontResetResult = S_OK;
    CHECK(c.frame());
    CHECK(c.resetCalls == 1 && c.lostCalls == 2 && c.fontResetCalls == 2);
    CHECK(c.offscreenCreates == 1 && c.targetCreates == 1);
    checkOriginalsReleasedOnce(c);
}

void partialFontCleanupBeforeNewLoss() {
    Context c;
    if (!c.initialize() || !c.participants[0].load(0)) return;
    c.fontResetResult = E_OUTOFMEMORY;
    CHECK(c.request(Context::desired()) == dm::ResetOutcome::Pending);
    CHECK(c.resetCalls == 1 && c.fontResetCalls == 1);
    auto* partialStaging = reinterpret_cast<FakeSurface*>(c.staging());
    auto* partialNative = reinterpret_cast<FakeSurface*>(c.native());
    CHECK(partialStaging != nullptr && partialNative != nullptr);
    c.cooperative = D3DERR_DEVICENOTRESET;
    c.lostResult = E_FAIL;
    CHECK(!c.frame());
    CHECK(c.resetCalls == 1 && c.fontResetCalls == 1);
    CHECK(c.staging() == nullptr && c.native() == nullptr);
    CHECK(c.participants[0].state->afterReadyCalls == 0);
    if (partialStaging && partialNative)
        CHECK(partialStaging->releases == 1 && partialNative->releases == 1);
    c.lostResult = S_OK;
    c.fontResetResult = S_OK;
    CHECK(c.frame());
    CHECK(c.resetCalls == 2 && c.fontResetCalls == 2);
    CHECK(c.offscreenCreates == 2 && c.targetCreates == 2);
    CHECK(c.participants[0].state->afterReadyCalls == 1);
    checkOriginalsReleasedOnce(c);
    c.checkParticipants();
}

void resumedParticipantRefusesNewLoss() {
    Context c;
    if (!c.initialize() || !c.participants[0].load(0) || !c.participants[1].load(1)) return;
    c.participants[1].state->afterReadyResult = E_OUTOFMEMORY;
    CHECK(c.request(Context::desired()) == dm::ResetOutcome::RequestedReady);
    CHECK(dm::compatPending() && !dm::rendererResetCanRequest());
    CHECK(c.participants[0].state->quiesced == 0 && c.participants[1].state->quiesced == 1);
    const unsigned aReady = c.participants[0].state->afterReadyCalls;
    const unsigned bReady = c.participants[1].state->afterReadyCalls;
    c.participants[0].state->beforeResult = E_FAIL;
    c.cooperative = D3DERR_DEVICENOTRESET;
    CHECK(!c.frame());
    CHECK(c.resetCalls == 1 && c.lostCalls == 1);
    CHECK(c.participants[0].state->afterReadyCalls == aReady);
    CHECK(c.participants[1].state->afterReadyCalls == bReady);
    c.participants[0].state->beforeResult = S_OK;
    c.participants[1].state->afterReadyResult = S_OK;
    CHECK(c.frame());
    CHECK(c.resetCalls == 2 && !dm::compatPending());
    CHECK(c.participants[0].state->beforeCalls == 3);
    CHECK(c.participants[1].state->beforeCalls == 1);
    c.checkParticipants();
}

void helperSignatures() {
    Context c;
    dm::rendererResetConfigure(c.wrapper, &c.profile);
    const unsigned char* signature = nullptr;
    size_t length = 0;
    CHECK(dm::rendererResetSignature(&signature, &length));
    CHECK(signature == dm_reset_audit::kHelperJ ||
          std::memcmp(signature, dm_reset_audit::kHelperJ, sizeof(dm_reset_audit::kHelperJ)) == 0);
    CHECK(length == sizeof(dm_reset_audit::kHelperJ));
    c.wrapper[c.profile.resetHelperRva + length - 1] ^= 1;
    CHECK(!dm::rendererResetSignature(&signature, &length));
    c.profile.entryByteCount = 6;
    std::memcpy(c.wrapper + c.profile.resetHelperRva, dm_reset_audit::kHelper2020,
                sizeof(dm_reset_audit::kHelper2020));
    CHECK(dm::rendererResetSignature(&signature, &length));
    CHECK(length == sizeof(dm_reset_audit::kHelper2020));
    c.wrapper[c.profile.resetHelperRva + length - 1] ^= 1;
    CHECK(!dm::rendererResetSignature(&signature, &length));
}

struct TestCase { const char* name; void (*run)(); };
const TestCase kCases[] = {
    {"success", successfulReset},
    {"same_address_device_replacement", deviceReplacedAtSameAddress},
    {"failed_rollback_retry", failedRollbackRetry},
    {"repeated_failure", repeatedFailure},
    {"partial_recreate", partialRecreate},
    {"native_helper", nativeHelper},
    {"late_cooperative", lateCooperativeModule},
    {"late_refusal_after_failure", lateRefusalAfterFailure},
    {"late_legacy_during_loss", lateLegacyModuleDuringLoss},
    {"legacy_recovery", legacyModuleRecovery},
    {"stale_swap", staleSwapArm},
    {"flip_model", flipModelReset},
    {"dynamic_upload", dynamicUpload},
    {"dynamic_upload_failure", dynamicUploadFailure},
    {"dynamic_upload_lock_failure", dynamicUploadLockFailure},
    {"dynamic_upload_device_replacement", dynamicUploadDeviceReplacement},
    {"healthy_prepare_refusal", healthyPrepareRefusal},
    {"failed_font_cancellation", failedFontCancellation},
    {"partial_font_retry", partialFontRetry},
    {"partial_font_cleanup_new_loss", partialFontCleanupBeforeNewLoss},
    {"resumed_participant_new_loss", resumedParticipantRefusesNewLoss},
    {"helper_signatures", helperSignatures},
};

int runChildren() {
    const std::wstring executable = executablePath();
    for (const auto& test : kCases) {
        std::wstring command = L"\"" + executable + L"\" --case ";
        for (const char* c = test.name; *c; ++c) command.push_back(static_cast<wchar_t>(*c));
        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child = {};
        if (!CreateProcessW(executable.c_str(), &command[0], nullptr, nullptr, TRUE,
                            0, nullptr, nullptr, &startup, &child)) {
            std::fprintf(stderr, "Cannot start case %s (error %lu)\n", test.name, GetLastError());
            ++g_failures;
            continue;
        }
        const DWORD waited = WaitForSingleObject(child.hProcess, 30000);
        DWORD result = 1;
        if (waited == WAIT_OBJECT_0) {
            if (!GetExitCodeProcess(child.hProcess, &result)) result = 1;
        } else {
            std::fprintf(stderr, "Case %s did not finish (wait result %lu)\n", test.name, waited);
            TerminateProcess(child.hProcess, 1);
            ++g_failures;
        }
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
        if (result != 0) {
            std::fprintf(stderr, "Case %s failed (exit %lu)\n", test.name, result);
            ++g_failures;
        }
    }
    std::printf("renderer reset adapter: %u failure(s), %zu isolated cases\n",
                g_failures, sizeof(kCases) / sizeof(kCases[0]));
    return g_failures ? 1 : 0;
}
}

// Only rendering implementation details are stubbed. Compatibility discovery,
// callback state, retry policy, native helper ABI and COM ownership all use the
// production adapter and bridge linked into this executable.
namespace dm {
void shadersReleaseResources() {
    CHECK(g_context != nullptr);
    if (!g_context) return;
    ++g_context->shaderReleases;
    g_context->shadersLive = false;
}
namespace log {
void write(const char*, ...) {}
}
}

int main(int argc, char** argv) {
    if (argc == 1) return runChildren();
    if (argc == 3 && std::strcmp(argv[1], "--case") == 0) {
        for (const auto& test : kCases) {
            if (std::strcmp(argv[2], test.name) != 0) continue;
            g_caseName = test.name;
            test.run();
            std::printf("%s: %u failure(s)\n", test.name, g_failures);
            return g_failures ? 1 : 0;
        }
    }
    std::fprintf(stderr, "Usage: %s [--case name]\n", argv[0]);
    return 2;
}
