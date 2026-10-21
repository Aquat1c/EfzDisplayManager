#include "shaders.h"

#include "logging.h"
#include "shader_sharp.h"
#include "shader_xbr.h"

namespace dm {

namespace {

ShaderConfig g_cfg;
bool g_available = false;
int g_srcW = 0, g_srcH = 0;
IDirect3DTexture9* g_captureTex = nullptr;
IDirect3DSurface9* g_captureSurf = nullptr;  // same object, level 0
IDirect3DSurface9* g_guard = nullptr;        // 64x64 scene guard
IDirect3DPixelShader9* g_sharp = nullptr;
IDirect3DPixelShader9* g_xbr = nullptr;
IDirect3DStateBlock9* g_state = nullptr;

struct Vtx {
    float x, y, z, rhw, u, v;
};

}  // namespace

void shadersConfigure(const ShaderConfig& cfg) {
    g_cfg = cfg;
}

bool shadersActive() {
    return g_available;
}

bool shadersCreateResources(IDirect3DDevice9* dev, int srcW, int srcH) {
    shadersReleaseResources();
    if (!dev || srcW <= 0 || srcH <= 0) return false;
    if (g_cfg.filter == ShaderFilter::None) return false;

    if (FAILED(dev->CreateTexture(srcW, srcH, 1, D3DUSAGE_RENDERTARGET,
                                  D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT,
                                  &g_captureTex, nullptr))) {
        DM_LOG("shaders: capture texture creation failed; filter disabled");
        return false;
    }
    if (FAILED(g_captureTex->GetSurfaceLevel(0, &g_captureSurf)) || !g_captureSurf ||
        FAILED(dev->CreateRenderTarget(64, 64, D3DFMT_X8R8G8B8,
            D3DMULTISAMPLE_NONE, 0, FALSE, &g_guard, nullptr)) || !g_guard) {
        DM_LOG("shaders: capture surface/overlay guard unavailable; using StretchRect");
        shadersReleaseResources();
        return false;
    }

    if (FAILED(dev->CreatePixelShader(
            reinterpret_cast<const DWORD*>(g_sharpBilinearPS), &g_sharp))) {
        DM_LOG("shaders: sharp-bilinear shader failed (ps_2_0 unavailable?)");
    }
    if (FAILED(dev->CreatePixelShader(
            reinterpret_cast<const DWORD*>(g_xbrPS2b), &g_xbr))) {
        DM_LOG("shaders: xBR shader failed (ps_2_b unavailable?)");
    }
    if ((g_cfg.filter == ShaderFilter::Sharp && !g_sharp) ||
        (g_cfg.filter == ShaderFilter::Xbr && !g_xbr)) {
        shadersReleaseResources();
        return false;
    }
    if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &g_state))) {
        shadersReleaseResources();
        return false;
    }
    g_srcW = srcW;
    g_srcH = srcH;
    g_available = true;
    DM_LOG("shaders: pipeline ready (filter=%d sharp=%d xbr=%d)",
           int(g_cfg.filter), g_sharp ? 1 : 0, g_xbr ? 1 : 0);
    return true;
}

void shadersReleaseResources() {
    if (g_captureSurf) {
        g_captureSurf->Release();
        g_captureSurf = nullptr;
    }
    if (g_captureTex) {
        g_captureTex->Release();
        g_captureTex = nullptr;
    }
    if (g_guard) {
        g_guard->Release();
        g_guard = nullptr;
    }
    if (g_sharp) {
        g_sharp->Release();
        g_sharp = nullptr;
    }
    if (g_xbr) {
        g_xbr->Release();
        g_xbr = nullptr;
    }
    if (g_state) {
        g_state->Release();
        g_state = nullptr;
    }
    g_available = false;
}

static bool drawQuad(IDirect3DDevice9* dev, IDirect3DSurface9* bb,
                     IDirect3DSurface9* target, const RECT* dstRect,
                     IDirect3DPixelShader9* ps, const float (*consts)[4],
                     UINT nConsts, D3DTEXTUREFILTERTYPE samplerFilter) {
    if (!g_state || !g_guard || FAILED(g_state->Capture())) return false;

    // Scene brackets run with the guard bound: co-loaded mods hooking the
    // d3d9 EndScene body draw their overlays into the throwaway guard.
    IDirect3DSurface9* guard = g_guard;
    if (FAILED(dev->SetRenderTarget(0, guard))) { g_state->Apply(); return false; }
    HRESULT hrScene = dev->BeginScene();
    if (FAILED(hrScene)) {
        dev->SetRenderTarget(0, bb);
        g_state->Apply();
        return false;
    }

    dev->SetRenderTarget(0, target);
    dev->SetPixelShader(ps);
    dev->SetVertexShader(nullptr);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    dev->SetTexture(0, g_captureTex);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, samplerFilter);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, samplerFilter);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0F);
    dev->SetPixelShaderConstantF(0, &consts[0][0], nConsts);

    // The -0.5 half-pixel offset is required: without it output pixel
    // centers sample exactly k/N and sharp-bilinear degenerates at integer
    // scales (no visible effect).
    const float L = float(dstRect->left) - 0.5f;
    const float T = float(dstRect->top) - 0.5f;
    const float R = float(dstRect->right) - 0.5f;
    const float B = float(dstRect->bottom) - 0.5f;
    Vtx q[4] = {
        {L, T, 0.0f, 1.0f, 0.0f, 0.0f},
        {R, T, 0.0f, 1.0f, 1.0f, 0.0f},
        {L, B, 0.0f, 1.0f, 0.0f, 1.0f},
        {R, B, 0.0f, 1.0f, 1.0f, 1.0f},
    };
    HRESULT hr = dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(Vtx));

    const HRESULT guardHr = dev->SetRenderTarget(0, guard);
    // Balance BeginScene even on failure so the caller can clear and
    // overwrite any accidental extra overlay draw with the native frame.
    const HRESULT endHr = dev->EndScene();
    const HRESULT targetHr = dev->SetRenderTarget(0, bb);
    const HRESULT stateHr = g_state->Apply();
    return SUCCEEDED(hr) && SUCCEEDED(guardHr) && SUCCEEDED(endHr) &&
           SUCCEEDED(targetHr) && SUCCEEDED(stateHr);
}

bool shadersDrawUpscaled(IDirect3DDevice9* dev, IDirect3DSurface9* srcSurface,
                         IDirect3DSurface9* backbuffer, const RECT* dstRect) {
    if (!g_available || !dev || !srcSurface || !backbuffer || !dstRect) {
        return false;
    }
    // 1:1 grab of the composed native frame into the sampleable texture.
    HRESULT grab = dev->StretchRect(srcSurface, nullptr, g_captureSurf, nullptr,
                                    D3DTEXF_NONE);
    if (FAILED(grab)) {
        DM_LOG("shaders: capture StretchRect hr=%08lx", long(grab));
        return false;
    }

    if (g_cfg.filter == ShaderFilter::Sharp && g_sharp) {
        static bool s_logged = false;
        if (!s_logged) {
            s_logged = true;
            DM_LOG("shaders: first sharp-bilinear draw");
        }
        const float c[1][4] = {
            {float(g_srcW), float(g_srcH), g_cfg.sharpness, 0.0f}};
        return drawQuad(dev, backbuffer, backbuffer, dstRect, g_sharp, c, 1,
                        D3DTEXF_LINEAR);
    }
    if (g_cfg.filter == ShaderFilter::Xbr && g_xbr) {
        const float scale =
            float(dstRect->right - dstRect->left) / float(g_srcW);
        const float c[2][4] = {
            {float(g_srcW), float(g_srcH), 1.0f / float(g_srcW), 1.0f / float(g_srcH)},
            {g_cfg.xbrWidth / (scale > 0.0f ? scale : 1.0f), g_cfg.xbrStrength,
             1.0f, 1.0f}};
        return drawQuad(dev, backbuffer, backbuffer, dstRect, g_xbr, c, 2,
                        D3DTEXF_POINT);
    }
    return false;
}

}  // namespace dm
