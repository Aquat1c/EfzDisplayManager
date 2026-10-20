#pragma once

#include <cstdint>

namespace dm {

// Anchors from docs/STAGE_A_RESEARCH_2026-10-20.md sections 2-3, verified by
// tools/verify_anchors.py against the exact pinned binaries. RVAs are relative
// to the loaded Ddraw.dll image base (preferred base 0x64C00000).
struct DdrawProfile {
    const char* name;
    unsigned char ddrawSha256[32];
    uint32_t peTimestamp;
    // RVA of the shared final StretchRect call inside Flip - the
    // observation/adaptation point. Both geometry branches converge here.
    uint32_t finalStretchRva;
    // Expected bytes at the hook site (a 6-byte indirect call; we overwrite
    // all 6 with an E8 call detour + NOP and forward via a direct COM call).
    unsigned char hookBytes[6];
    // Flip entry prologue (first two instructions, 7 bytes): the safe point
    // for a voluntary device reset. At entry no surfaces are cached in
    // registers yet, the previous frame released the backbuffer, and every
    // pointer is reloaded from the globals afterward - a reset here lets the
    // current frame render normally at the new size.
    uint32_t flipEntryRva;
    unsigned char entryBytes[7];
    // The wrapper's own reset helper (0x363D0/0x36220) is NOT used: it
    // dereferences the D3DX font at +0x74 without a null check and crashes
    // when Revival's text render was never initialized (direct efz.exe
    // launches). Kept here as a documented anchor only.
    // Renderer object globals (the thiscall object begins at
    // rendererObjectRva; fields below are its offsets).
    uint32_t rendererObjectRva;  // +0x00: object base (staging surface field)
    uint32_t nativeSurfaceRva;    // +0x04: native 640x480 target surface
    uint32_t deviceRva;           // +0x44: IDirect3DDevice9*
    uint32_t ppRva;               // +0x0C: D3DPRESENT_PARAMETERS (0x38 bytes)
    uint32_t hwndRva;             // +0x4C: render window handle
    uint32_t filterRva;           // +0x6C: D3DTEXTUREFILTERTYPE for both blits
    uint32_t borderlessRva;       // +0x71: borderless mode flag byte
    uint32_t fontRva;             // +0x74: D3DX font (may be null)
};

// Both audited Revival 1.02j Ddraw builds. Two different 1.02j wrappers
// exist (e.g. the F: and D: FULLPACK copies carry the renderer-split build
// while the F: _profile_backups copy carries the original); each is admitted
// only on an exact SHA-256 + PE-timestamp match with its own anchor set.
// Hash bytes are transcribed from tools/verify_anchors.py output and from
// InGameNetplay's revival_addresses.h profile table - compare against those
// sources when editing.

inline const DdrawProfile& splitJProfile() {
    // Renderer-split J (2026-08-02).
    // Ddraw.dll SHA-256 0e072342d87d240e18969bc3663032e0e370eaba24d1fbb8e6c1f6216ad9f603.
    static const DdrawProfile p = {
        "renderer-split Revival 1.02j (2026-08-02)",
        {0x0e, 0x07, 0x23, 0x42, 0xd8, 0x7d, 0x24, 0x0e, 0x18, 0x96, 0x9b,
         0xc3, 0x66, 0x30, 0x32, 0xe0, 0xe3, 0x70, 0xea, 0xba, 0x24, 0xd1,
         0xfb, 0xb8, 0xe6, 0xc1, 0xf6, 0x21, 0x6a, 0xd9, 0xf6, 0x03},
        0x6A6F1795u,
        0x279C0u,
        {0xFF, 0x90, 0x88, 0x00, 0x00, 0x00},
        0x27860u,
        {0x8D, 0x4C, 0x24, 0x04, 0x83, 0xE4, 0xF8},  // lea ecx,[esp+4]; and esp,-8
        0xDF280u,
        0xDF284u,
        0xDF2C4u,
        0xDF28Cu,
        0xDF2CCu,
        0xDF2ECu,
        0xDF2F1u,
        0xDF2F4u,
    };
    return p;
}

inline const DdrawProfile& originalJProfile() {
    // Original J (MinGW). Anchors verified statically (same call encoding,
    // same renderer-object globals; final blit at 0x27900 per
    // docs/STAGE_A_RESEARCH_2026-10-20.md section 10 Q4).
    // Ddraw.dll SHA-256 e18abe007fbb7c48b63ab56027754e0aee196dc753af189949ba2dc46f366f9d.
    static const DdrawProfile p = {
        "original Revival 1.02j (MinGW)",
        {0xe1, 0x8a, 0xbe, 0x00, 0x7f, 0xbb, 0x7c, 0x48, 0xb6, 0x3a, 0xb5,
         0x60, 0x27, 0x75, 0x4e, 0x0a, 0xee, 0x19, 0x6d, 0xc7, 0x53, 0xaf,
         0x18, 0x99, 0x49, 0xba, 0x2d, 0xc4, 0x6f, 0x36, 0x6f, 0x9d},
        0x6A04FEC7u,
        0x27900u,
        {0xFF, 0x90, 0x88, 0x00, 0x00, 0x00},
        0x277A0u,
        {0x8D, 0x4C, 0x24, 0x04, 0x83, 0xE4, 0xF8},  // lea ecx,[esp+4]; and esp,-8
        0xDF280u,
        0xDF284u,
        0xDF2C4u,
        0xDF28Cu,
        0xDF2CCu,
        0xDF2ECu,
        0xDF2F1u,
        0xDF2F4u,
    };
    return p;
}

}  // namespace dm
