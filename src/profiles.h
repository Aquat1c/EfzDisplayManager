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
    uint32_t entryByteCount;  // 6 (2020-gen prologue) or 7 (J-generation)
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
    // The wrapper's OWN Present-failure reset helper (thiscall: self =
    // renderer object, one stack arg = &PP). Null-safe for its surfaces;
    // dereferences its font unconditionally (we guard on font != null).
    // Used by the recovery path when our own sequence cannot restore the
    // wrapper's state.
    uint32_t resetHelperRva;
    // The wrapper WndProc's Alt+Enter keycode compare (cmp ebx/esi, 0xD).
    // We rewrite the immediate to an unpressable key so OUR Flip-entry poll
    // owns Alt+Enter (configurable exclusive/borderless), and restore the
    // byte on detach.
    uint32_t altEnterCmpRva;
    unsigned char altEnterBytes[3];
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
        {0x8D, 0x4C, 0x24, 0x04, 0x83, 0xE4, 0xF8},
        7u,  // lea ecx,[esp+4]; and esp,-8
        0xDF280u,
        0xDF284u,
        0xDF2C4u,
        0xDF28Cu,
        0xDF2CCu,
        0xDF2ECu,
        0xDF2F1u,
        0xDF2F4u,
        0x363D0u,
        0x24990u,
        {0x83, 0xFB, 0x0D},
    };
    return p;
}

// 2020-generation wrappers (814,080 bytes): identical renderer-object
// layout and hook encodings to the J generation (verified: staging/native at
// renderer+0/+4, PP +0x0C, device +0x44, hwnd +0x4C, filter +0x6C,
// borderless +0x71 - confirmed via the exported ?isFullscreen@EfzRender -
// font +0x74 via ?initTextRender; Flip ret 0xC; final blit bytes
// ff 90 88 00 00 00). Code RVAs: 1.02f/g/h match the 2020 original;
// only 1.02i shifts. Their own Present-failure reset helper sits at
// 0x165c0 (documented; we use our own font-safe sequence regardless).
#define DM_PROFILE_2020GEN                                                     \
    0x1FC1Fu, {0xFF, 0x90, 0x88, 0x00, 0x00, 0x00},                            \
    0x1F9E0u, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0}, 6u,                    \
    0xB6BC8u, 0xB6BCCu, 0xB6C0Cu, 0xB6BD4u, 0xB6C14u, 0xB6C34u, 0xB6C39u,      \
    0xB6C3Cu, 0x165C0u, 0x1C61Cu, {0x83, 0xFE, 0x0D}

inline const DdrawProfile& rev2020Profile() {
    // 2020-era original: ships with EFZ_clean, EFZ_RELAY, F:/EFZ,
    // F:/EFZ - Copy (1.02e), F:/EFZ - Copy (2), and the capture_diag copy.
    static const DdrawProfile p = {
        "Revival 2020-generation wrapper (1.02e-era)",
        {
         0xc4, 0x86, 0x90, 0x3e, 0x82, 0x03, 0x7e, 0xa6,
         0xeb, 0x64, 0x8f, 0xce, 0x66, 0x2a, 0x9e, 0xbc,
         0x8c, 0x60, 0x53, 0x76, 0x29, 0xa8, 0x4f, 0xdb,
         0xed, 0x57, 0x3b, 0x7e, 0xd9, 0x04, 0x5f, 0x7a,
    },
        0x5EA72E7Fu,
        DM_PROFILE_2020GEN,
    };
    return p;
}

inline const DdrawProfile& rev102fProfile() {
    static const DdrawProfile p = {
        "Revival 1.02f wrapper",
        {
         0x71, 0x2a, 0x45, 0xd3, 0x11, 0xc8, 0x2a, 0x63,
         0x05, 0xe1, 0x91, 0x88, 0x69, 0x23, 0xd0, 0x58,
         0x8a, 0xdb, 0x05, 0xfc, 0xa6, 0x92, 0xe7, 0x8a,
         0x09, 0x5a, 0x80, 0x09, 0x4b, 0x41, 0x5d, 0x30,
    },
        0x5F8C5897u,
        DM_PROFILE_2020GEN,
    };
    return p;
}

inline const DdrawProfile& rev102gProfile() {
    static const DdrawProfile p = {
        "Revival 1.02g wrapper",
        {
         0x29, 0x5b, 0x3f, 0x3f, 0x73, 0x30, 0x02, 0x81,
         0xec, 0xf2, 0xb2, 0x2c, 0x67, 0x85, 0xd3, 0x61,
         0x4b, 0xcb, 0xe1, 0xbe, 0xb3, 0xe7, 0x8e, 0x4f,
         0x84, 0xa7, 0x44, 0xc3, 0x51, 0x18, 0xa9, 0x49,
    },
        0x623F8621u,
        DM_PROFILE_2020GEN,
    };
    return p;
}

inline const DdrawProfile& rev102hProfile() {
    static const DdrawProfile p = {
        "Revival 1.02h wrapper",
        {
         0x4d, 0x83, 0x36, 0x9b, 0xd4, 0x0f, 0xca, 0x91,
         0x5b, 0xa8, 0xbb, 0xe8, 0xb4, 0x6a, 0xfa, 0xbd,
         0xcd, 0x62, 0x6a, 0x0e, 0x97, 0xc4, 0xa1, 0x13,
         0x5f, 0x97, 0xa2, 0x92, 0xdb, 0x0c, 0xa7, 0x2c,
    },
        0x625C3202u,
        DM_PROFILE_2020GEN,
    };
    return p;
}

inline const DdrawProfile& rev102iProfile() {
    // Same 2020-gen layout; all Flip code shifted by +0xB0 vs the others.
    static const DdrawProfile p = {
        "Revival 1.02i wrapper",
        {
         0x60, 0x66, 0x01, 0xcc, 0x07, 0x07, 0xcd, 0xa0,
         0xfd, 0x6f, 0xd6, 0xb8, 0xc2, 0x7b, 0xa2, 0xeb,
         0x1f, 0x7f, 0xaf, 0xe2, 0xc5, 0x60, 0xb5, 0x09,
         0xfb, 0x3c, 0xbf, 0x14, 0x6a, 0x90, 0xfa, 0xe6,
    },
        0x63BF27C3u,
        0x1FCCFu,
        {0xFF, 0x90, 0x88, 0x00, 0x00, 0x00},
        0x1FA90u,
        {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0},
        6u,
        0xB6BC8u, 0xB6BCCu, 0xB6C0Cu, 0xB6BD4u, 0xB6C14u, 0xB6C34u, 0xB6C39u,
        0xB6C3Cu, 0x16670u, 0x1C6DCu, {0x83, 0xFE, 0x0D},
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
        {0x8D, 0x4C, 0x24, 0x04, 0x83, 0xE4, 0xF8},
        7u,  // lea ecx,[esp+4]; and esp,-8
        0xDF280u,
        0xDF284u,
        0xDF2C4u,
        0xDF28Cu,
        0xDF2CCu,
        0xDF2ECu,
        0xDF2F1u,
        0xDF2F4u,
        0x36220u,
        0x246F0u,
        {0x83, 0xFB, 0x0D},
    };
    return p;
}

}  // namespace dm
