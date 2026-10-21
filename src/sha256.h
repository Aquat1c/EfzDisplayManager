#pragma once

#include <cstddef>
#include <cstdint>

namespace dm {

// Self-contained SHA-256. Used instead of CryptoAPI so the DLL keeps an
// XP-clean import table (advapi32 SHA-256 support on XP is patch-dependent).
class Sha256 {
public:
    Sha256();
    void update(const void* data, size_t len);
    void finish(unsigned char out[32]);

private:
    void processBlock(const unsigned char* block);
    uint32_t state_[8];
    uint64_t bitCount_;
    unsigned char buffer_[64];
    size_t bufferLen_;
};

// Hashes an entire file. Returns false when the file cannot be opened/read.
// The path is ANSI (module paths from GetModuleFileNameA are ASCII here).
bool sha256File(const char* path, unsigned char out[32]);

}  // namespace dm
