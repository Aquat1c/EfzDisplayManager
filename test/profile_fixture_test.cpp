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

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) return usage();
    unsigned char expected[32];
    const char* path = nullptr;
    if (std::strcmp(argv[1], "--file") == 0) {
        path = argv[2];
        if (!parseDigest(argv[3], expected)) return usage();
    } else if (std::strcmp(argv[1], "--profile") == 0) {
        const dm::DdrawProfile* profile = profileFor(argv[2]);
        if (!profile) return usage();
        path = argv[3];
        std::memcpy(expected, profile->ddrawSha256, sizeof(expected));
    } else {
        return usage();
    }

    unsigned char actual[32];
    if (!dm::sha256File(path, actual)) {
        std::fprintf(stderr, "FAIL cannot read %s\n", path);
        return 1;
    }
    if (std::memcmp(actual, expected, sizeof(actual)) != 0) {
        std::fprintf(stderr, "FAIL SHA-256 mismatch: %s\n", path);
        return 1;
    }
    std::printf("PASS SHA-256: %s\n", path);
    return 0;
}
