// Explicit, optional Windows file/profile hash checks. No machine-local paths
// are part of the default portable suite. Build with DM_BUILD_PROFILE_FIXTURE_TEST.
#include <cstdio>
#include <cstring>

#include "profiles.h"
#include "sha256.h"

namespace {

const dm::DdrawProfile* profileFor(const char* key) {
    if (std::strcmp(key, "2020") == 0) return &dm::rev2020Profile();
    if (std::strcmp(key, "1.02f") == 0) return &dm::rev102fProfile();
    if (std::strcmp(key, "1.02g") == 0) return &dm::rev102gProfile();
    if (std::strcmp(key, "1.02h") == 0) return &dm::rev102hProfile();
    if (std::strcmp(key, "1.02i") == 0) return &dm::rev102iProfile();
    if (std::strcmp(key, "1.02j-split") == 0) return &dm::splitJProfile();
    if (std::strcmp(key, "1.02j-original") == 0) return &dm::originalJProfile();
    return nullptr;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parseDigest(const char* hex, unsigned char out[32]) {
    if (std::strlen(hex) != 64) return false;
    for (int i = 0; i < 32; ++i) {
        const int high = hexDigit(hex[2 * i]);
        const int low = hexDigit(hex[2 * i + 1]);
        if (high < 0 || low < 0) return false;
        out[i] = static_cast<unsigned char>((high << 4) | low);
    }
    return true;
}

int usage() {
    std::fprintf(stderr,
        "Usage: dm_profile_fixture_test --file PATH EXPECTED_SHA256\n"
        "       dm_profile_fixture_test --profile KEY PATH\n"
        "Keys: 2020, 1.02f, 1.02g, 1.02h, 1.02i, 1.02j-split, 1.02j-original\n");
    return 2;
}

bool narrow(const wchar_t* text, char* out, size_t cap) {
    size_t i = 0;
    for (; text[i] && i + 1 < cap; ++i) {
        if (text[i] > 0x7F) return false;
        out[i] = static_cast<char>(text[i]);
    }
    out[i] = '\0';
    return true;
}

}  // namespace

// wmain: the path may contain characters outside the ANSI code page, which
// is exactly what the wide file API must handle.
int wmain(int argc, wchar_t** argv) {
    if (argc != 4) return usage();
    char mode[16], key[32], digest[80];
    if (!narrow(argv[1], mode, sizeof(mode))) return usage();
    unsigned char expected[32];
    const wchar_t* path = nullptr;
    if (std::strcmp(mode, "--file") == 0) {
        path = argv[2];
        if (!narrow(argv[3], digest, sizeof(digest)) || !parseDigest(digest, expected)) return usage();
    } else if (std::strcmp(mode, "--profile") == 0) {
        if (!narrow(argv[2], key, sizeof(key))) return usage();
        const dm::DdrawProfile* profile = profileFor(key);
        if (!profile) return usage();
        path = argv[3];
        std::memcpy(expected, profile->ddrawSha256, sizeof(expected));
    } else {
        return usage();
    }

    unsigned char actual[32];
    if (!dm::sha256File(path, actual)) {
        std::fprintf(stderr, "FAIL cannot read %ls\n", path);
        return 1;
    }
    if (std::memcmp(actual, expected, sizeof(actual)) != 0) {
        std::fprintf(stderr, "FAIL SHA-256 mismatch: %ls\n", path);
        return 1;
    }
    std::printf("PASS SHA-256 (wide path)\n");
    return 0;
}
