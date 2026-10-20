#pragma once

#include <cstddef>

namespace dm {

// Maximum simultaneously installed code hooks (final blit + overlay reset
// point).
enum { kMaxDetourSites = 2 };

// Installs a 5-byte detour at `site` (plus NOP padding up to
// `expectedBytes` length), after byte-comparing the site against
// `expectedBytes`. With useCall=true the encoding is E8 (call rel32) so the
// hook receives a normal return address that lands on the NOP padding -
// required where the replaced instruction was itself a call. All other
// threads are suspended while the patch is written so no thread can execute
// into a half-written instruction. Returns false without patching when the
// site bytes do not match.
bool installDetour(unsigned char* site, const unsigned char* expectedBytes,
                   size_t byteCount, const void* target, bool useCall);

// Entry-point detour with trampoline: replaces the first `byteCount` bytes
// of a function (which must be complete instructions) with an E9 jmp to
// `target`, and builds a trampoline in `trampolineBuf` that re-executes the
// original instructions and jumps back to site+byteCount. The hook target is
// responsible for tail-jumping into the trampoline (see hook_flip.cpp's
// naked shim). Used for the Flip-entry resize point.
bool installEntryDetour(unsigned char* site, const unsigned char* expectedBytes,
                        size_t byteCount, const void* target,
                        unsigned char* trampolineBuf, size_t trampolineCap);

// Restores the bytes saved by a successful installDetour on the given site.
bool removeDetour(unsigned char* site);

}  // namespace dm
