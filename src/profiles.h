#pragma once

#include <cstdint>

namespace dm {

// RVAs are relative to the loaded Ddraw.dll image base. Code anchors are
// byte-checked at runtime before patching; tools/verify_anchors.py covers the
// two 1.02j builds only (no 1.02f-i binary is on hand to check offline).
struct DdrawProfile {
    const char* name;
    unsigned char ddrawSha256[32];
    uint32_t peTimestamp;
    // Shared final StretchRect call inside Flip; the adaptation point.
    uint32_t finalStretchRva;
    // Expected 6-byte indirect call; overwritten with an E8 call + NOP padding.
    unsigned char hookBytes[6];
    // Flip entry prologue: the safe reset point (no surfaces cached in
    // registers yet; every pointer reloads from globals afterward).
    uint32_t flipEntryRva;
    unsigned char entryBytes[7];
    uint32_t entryByteCount;  // 6 (2020-gen prologue) or 7 (J-generation)
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
    // Present-failure reset helper; fully matched, then detoured.
    uint32_t resetHelperRva;
    // WndProc Alt+Enter keycode compare; the immediate is rewritten to an
    // unpressable key so our Flip-entry poll owns Alt+Enter.
    uint32_t altEnterCmpRva;
    unsigned char altEnterBytes[3];
    // Device Present call inside Flip, rewritten for the flip model
    // (hook_flip.cpp presentFrame). Push style (MSVC builds): `push 0; push edi;
    // call [eax+44h]` (the three earlier pushes stay). Otherwise (MinGW J
    // builds): `call [eax+44h]; sub esp,14h` with the arguments already stored.
    uint32_t presentRva;
    unsigned char presentBytes[6];
    bool presentPushStyle;
};

// Two 1.02j builds exist (renderer-split vs original); each is admitted on an
// exact SHA-256 + timestamp match. Hashes come from tools/verify_anchors.py
// and InGameNetplay's revival_addresses.h - compare against those when editing.

inline const DdrawProfile& splitJProfile() {
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
        0x279FCu,
        {0xFF, 0x50, 0x44, 0x83, 0xEC, 0x14},
        false,
    };
    return p;
}

// 2020-generation wrappers (814,080 bytes): same renderer-object layout and
// hook encodings as the J generation. 1.02f/g/h share the original's code
// RVAs; only 1.02i shifts.
#define DM_PROFILE_2020GEN                                                     \
    0x1FC1Fu, {0xFF, 0x90, 0x88, 0x00, 0x00, 0x00},                            \
    0x1F9E0u, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0}, 6u,                    \
    0xB6BC8u, 0xB6BCCu, 0xB6C0Cu, 0xB6BD4u, 0xB6C14u, 0xB6C34u, 0xB6C39u,      \
    0xB6C3Cu, 0x165C0u, 0x1C61Cu, {0x83, 0xFE, 0x0D},                          \
    0x1FC37u, {0x6A, 0x00, 0x57, 0xFF, 0x50, 0x44}, true

inline const DdrawProfile& rev2020Profile() {
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
        0x1FCE7u, {0x6A, 0x00, 0x57, 0xFF, 0x50, 0x44}, true,
    };
    return p;
}

inline const DdrawProfile& originalJProfile() {
    // Original J (MinGW); same anchors as the split build except the code RVAs.
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
        0x2793Cu,
        {0xFF, 0x50, 0x44, 0x83, 0xEC, 0x14},
        false,
    };
    return p;
}

// Every supported wrapper, in admission order; nullptr past the end.
inline const DdrawProfile* profileAt(int index) {
    switch (index) {
        case 0: return &splitJProfile();
        case 1: return &originalJProfile();
        case 2: return &rev2020Profile();
        case 3: return &rev102fProfile();
        case 4: return &rev102gProfile();
        case 5: return &rev102hProfile();
        case 6: return &rev102iProfile();
        default: return nullptr;
    }
}

}  // namespace dm
