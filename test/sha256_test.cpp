// SHA-256 self-test (FIPS 180-2 vectors). Built as a normal-CRT console app;
// only the shipped DLL itself is CRT-free. Run before deployment so a silent
// hash bug can never fail package admission at runtime.

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "../src/format.h"
#include "../src/sha256.h"

static int failures = 0;

static void expectFormat(const char* label, const char* expected, const char* fmt, ...) {
    char out[512];
    va_list args;
    va_start(args, fmt);
    dm::formatStringV(out, sizeof(out), fmt, args);
    va_end(args);
    if (std::strcmp(out, expected) == 0) {
        std::printf("PASS fmt %s -> \"%s\"\n", label, out);
    } else {
        std::printf("FAIL fmt %s: got \"%s\" want \"%s\"\n", label, out, expected);
        ++failures;
    }
}

static void runFormatTests() {
    expectFormat("plain", "hello", "hello");
    expectFormat("percent", "100%", "100%%");
    expectFormat("prefix", "[07:08:09.010] ", "[%02u:%02u:%02u.%03u] ", 7, 8, 9, 10);
    expectFormat("dec", "u=42 0u=0042", "u=%u 0u=%04u", 42u, 42u);
    expectFormat("long", "lu=1000000", "lu=%lu", 1000000UL);
    expectFormat("hex", "08lx=00a0b6c1", "08lx=%08lx", 0xA0B6C1UL);
    expectFormat("ptr", "p=deadbeef", "p=%p", (void*)0xDEADBEEF);
    expectFormat("str", "s=abc(null)", "s=%s%s", "abc", (const char*)nullptr);
    // The exact observation-log formats from hook_flip.cpp.
    char srcDesc[64] = "?";
    dm::formatString(srcDesc, sizeof(srcDesc), "%ux%u pool=%u fmt=%u", 640u, 480u,
                     0u, 21u);
    expectFormat("srclike", "src=[640x480 pool=0 fmt=21]", "src=[%s]", srcDesc);
    char dstDesc[64] = "?";
    dm::formatString(dstDesc, sizeof(dstDesc), "%ux%u pool=%u fmt=%u", 1920u,
                     1080u, 1u, 22u);
    expectFormat("flip",
                 "flip #1: src=[640x480 pool=0 fmt=21] dst=[1920x1080 pool=1 "
                 "fmt=22] dstRect=NULL(full) filter=2 pp=1920x1080 "
                 "borderless=0 <changed>",
                 "flip #%u: src=[%s] dst=[%s] dstRect=%s filter=%lu pp=%lux%lu "
                 "borderless=%lu%s",
                 1ul, srcDesc, dstDesc, "NULL(full)", 2ul, 1920ul, 1080ul, 0ul,
                 " <changed>");
    expectFormat("ptrchain", "native 00112233->00445566",
                 "native %p->%p", (void*)0x112233, (void*)0x445566);
    // Long strings and truncation behavior - probe the length threshold.
    {
        char tiny[2] = "x";
        char dst[16];
        std::printf("step: manual va_list route\n");
        va_list manual;
        va_start(manual, tiny);  // dummy: prove va machinery works here
        va_end(manual);
        char* tinyPtr = tiny;
        dm::formatString(dst, sizeof(dst), "%s", tinyPtr);  // variadic wrapper
        std::printf("step: wrapper route ok: %s\n", dst);
    }
    static const unsigned kProbes[] = {1u, 5u, 10u, 14u, 15u, 16u, 17u, 18u,
                                       20u, 31u, 32u, 33u, 40u, 100u, 599u};
    for (unsigned pi = 0; pi < sizeof(kProbes) / sizeof(kProbes[0]); ++pi) {
        unsigned probe = kProbes[pi];
        char probeBuf[600];
        std::memset(probeBuf, 'x', probe);
        probeBuf[probe] = '\0';
        char dst[16];
        std::printf("step: probe len=%u\n", probe);
        int n = dm::formatString(dst, sizeof(dst), "%s", probeBuf);
        std::printf("step: probe len=%u ok n=%d\n", probe, n);
    }
    char big[600];
    std::memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    char small[16];
    int n = dm::formatString(small, sizeof(small), "%s", big);
    if (n == int(std::strlen(big)) && std::strlen(small) == 15) {
        std::printf("PASS fmt truncation\n");
    } else {
        std::printf("FAIL fmt truncation: n=%d len=%u\n", n,
                    unsigned(std::strlen(small)));
        ++failures;
    }
}

static void expect(const char* label, const char* input, size_t len,
                   const char* expectedHex) {
    unsigned char out[32];
    dm::Sha256 h;
    h.update(input, len);
    h.finish(out);
    char hex[65];
    for (int i = 0; i < 32; ++i) {
        std::sprintf(hex + 2 * i, "%02x", out[i]);
    }
    hex[64] = '\0';
    if (std::strcmp(hex, expectedHex) == 0) {
        std::printf("PASS %s\n", label);
    } else {
        std::printf("FAIL %s: got %s want %s\n", label, hex, expectedHex);
        ++failures;
    }
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc == 3) {
        // File-pin mode: dm_sha256_test <file> <expected-hex> - validates the
        // file-hashing path end to end against ground truth before the
        // embedded profile pins are trusted at runtime.
        unsigned char out[32];
        if (!dm::sha256File(argv[1], out)) {
            std::printf("FAIL cannot read %s\n", argv[1]);
            return 1;
        }
        char hex[65];
        for (int i = 0; i < 32; ++i) {
            std::sprintf(hex + 2 * i, "%02x", out[i]);
        }
        hex[64] = '\0';
        if (std::strcmp(hex, argv[2]) == 0) {
            std::printf("PASS file %s\n", argv[1]);
            return 0;
        }
        std::printf("FAIL file %s: got %s want %s\n", argv[1], hex, argv[2]);
        return 1;
    }

    expect("empty", "", 0,
           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    expect("abc", "abc", 3,
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    expect("two-block", "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
           "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    // Exactly 55/56/64-byte messages exercise the padding paths.
    char buf[1024];
    std::memset(buf, 'a', sizeof(buf));
    expect("64x'a", buf, 64,
           "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    expect("55x'a", buf, 55,
           "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    expect("56x'a", buf, 56,
           "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");

    runFormatTests();

    if (failures == 0) {
        std::printf("ALL PASS\n");
        return 0;
    }
    return 1;
}
