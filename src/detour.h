#pragma once

#include <cstddef>

namespace dm {

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

// Restores the bytes saved by a successful installDetour on the same site.
bool removeDetour(unsigned char* site);

}  // namespace dm
