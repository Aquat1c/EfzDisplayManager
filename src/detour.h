#pragma once

#include <cstddef>
#include <cstdint>

namespace dm {

// Encodes `opcode rel32` at out[0..4] for an instruction that starts at
// address `at` and transfers to `target` (rel32 wraps over the 32-bit space).
inline void encodeRel32(unsigned char* out, unsigned char opcode, uint32_t at, uint32_t target) {
    const uint32_t displacement = target - (at + 5);
    out[0] = opcode;
    for (int i = 0; i < 4; ++i) out[1 + i] = static_cast<unsigned char>(displacement >> (8 * i));
}

// Writes `replacement` over `length` bytes at `site` with every other thread
// suspended, after checking the site (and up to guardedLength bytes) against
// `expectedBytes`. Returns false without patching on any mismatch.
bool installPatch(unsigned char* site, const unsigned char* expectedBytes, size_t length,
                  const unsigned char* replacement, size_t guardedLength = 0);

// installPatch with a 5-byte E9 jmp (or E8 call with useCall=true, for
// replacing a call: the hook returns onto the NOP padding) padded to byteCount.
// If guardedLength exceeds byteCount, expectedBytes must span that whole range;
// it is all rechecked while suspended, but only byteCount bytes are written.
bool installDetour(unsigned char* site, const unsigned char* expectedBytes,
                   size_t byteCount, const void* target, bool useCall,
                   size_t guardedLength = 0);

// Replaces the first `byteCount` bytes of a function (complete instructions
// only) with an E9 jmp to `target`, and builds a trampoline in `trampolineBuf`
// that re-executes them and jumps back to site+byteCount. The target must
// tail-jump into the trampoline (see hook_flip.cpp's naked shim).
bool installEntryDetour(unsigned char* site, const unsigned char* expectedBytes,
                        size_t byteCount, const void* target,
                        unsigned char* trampolineBuf, size_t trampolineCap);

}  // namespace dm
