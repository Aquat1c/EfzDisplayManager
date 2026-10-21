#pragma once

// Shader-assisted upscaling ported from SokuDisplayManager (MIT; creator
// permission granted). Managed lazily per device generation: capture texture
// (native-size render-target texture), pixel shaders, full state block, and
// a 64x64 "scene guard" render target bound while the extra scene brackets
// run so co-loaded mods' inline EndScene hooks draw into the throwaway guard
// instead of the finished frame.

#include <windows.h>
#include <d3d9.h>

namespace dm {

enum class ShaderFilter { None = 0, Sharp = 1, Xbr = 2 };

struct ShaderConfig {
    ShaderFilter filter = ShaderFilter::None;
    float sharpness = 2.0f;      // 1.0..4.0 (Sharp)
    float xbrStrength = 0.5f;    // 0..1 blend (xBR)
    float xbrWidth = 1.0f;       // edge width scale (xBR)
};

// Selects the filter and parameters (call once at bootstrap).
void shadersConfigure(const ShaderConfig& cfg);

// Recreates all shader-pipeline resources for the current device. Returns
// false (and leaves the pipeline disabled) when anything fails - callers
// then keep using the plain StretchRect path.
bool shadersCreateResources(IDirect3DDevice9* dev, int srcW, int srcH);

// Releases everything (before Reset / on teardown).
void shadersReleaseResources();

// Draws the native-size source surface into dstRect on the backbuffer using
// the configured filter. Returns false when the pipeline is unavailable or
// the draw fails; the caller must then fall back to StretchRect.
bool shadersDrawUpscaled(IDirect3DDevice9* dev, IDirect3DSurface9* srcSurface,
                         IDirect3DSurface9* backbuffer, const RECT* dstRect);

bool shadersActive();

}  // namespace dm
