// Self-tests for SHA-256 (FIPS 180-2 vectors), geometry, resize, fullscreen
// parameters and formatting. Normal-CRT console app; only the DLL is CRT-free.

#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include "../src/detour.h"
#include "../src/format.h"
#include "../src/fullscreen.h"
#include "../src/geometry.h"
#include "../src/resize.h"
#include "../src/sha256.h"

static int failures = 0;

static void expectSize(const char* label, long wantW, long wantH, int srcW,
                       int srcH, int bbW, int bbH, dm::ScaleMode mode,
                       int intScale, int customW, int customH) {
    long w = -1, h = -1;
    bool ok = dm::computeOutputSize(srcW, srcH, bbW, bbH, mode, intScale,
                                    customW, customH, &w, &h);
    if (ok && w == wantW && h == wantH) {
        std::printf("PASS geo %s -> %ldx%ld\n", label, w, h);
    } else {
        std::printf("FAIL geo %s: got %ldx%ld want %ldx%ld (ok=%d)\n", label, w,
                    h, wantW, wantH, int(ok));
        ++failures;
    }
}

static void runGeometryTests() {
    using dm::ScaleMode;
    expectSize("fit-4:3", 1280, 960, 640, 480, 1280, 960, ScaleMode::Fit, 0, 0, 0);
    expectSize("fit-16:9", 1440, 1080, 640, 480, 1920, 1080, ScaleMode::Fit, 0, 0, 0);
    expectSize("fit-16:10", 1400, 1050, 640, 480, 1680, 1050, ScaleMode::Fit, 0, 0, 0);
    expectSize("int-auto-1080p", 1280, 960, 640, 480, 1920, 1080,
               ScaleMode::Integer, 0, 0, 0);
    expectSize("int-auto-800x450", 640, 450, 640, 480, 800, 450,
               ScaleMode::Integer, 0, 0, 0);
    expectSize("int-1", 640, 480, 640, 480, 1920, 1080, ScaleMode::Integer, 1, 0, 0);
    expectSize("int-3-clamped", 1280, 960, 640, 480, 1920, 1080,
               ScaleMode::Integer, 3, 0, 0);
    expectSize("int-2", 1280, 960, 640, 480, 1280, 960, ScaleMode::Integer, 2, 0, 0);
    expectSize("custom-exact", 1024, 768, 640, 480, 1280, 960, ScaleMode::Custom,
               0, 1024, 768);
    expectSize("custom-clamped", 1280, 960, 640, 480, 1280, 960, ScaleMode::Custom,
               0, 4000, 2000);
    expectSize("custom-default", 640, 480, 640, 480, 1920, 1080,
               ScaleMode::Custom, 0, 0, 0);

    long l = -7, t = -7;
    dm::centerRect(1920, 1080, 1440, 1080, &l, &t);
    if (l == 240 && t == 0) {
        std::printf("PASS geo center-16:9\n");
    } else {
        std::printf("FAIL geo center-16:9: (%ld,%ld)\n", l, t);
        ++failures;
    }
    dm::centerRect(1280, 960, 640, 480, &l, &t);
    if (l == 320 && t == 240) {
        std::printf("PASS geo center-int1\n");
    } else {
        std::printf("FAIL geo center-int1: (%ld,%ld)\n", l, t);
        ++failures;
    }
}

static void runResizeTests() {
    using dm::ResizeAction;
    dm::ResizeState st;
    dm::ResizeInput in;
    long w = 0, h = 0;
    int resets = 0;

    // Fresh state: capture, no reset. (Revival: 640x480 window, 1280x960 bb.)
    in = {640, 480, 1280, 960, 0};
    if (dm::evaluateResize(&st, in, &w, &h) == ResizeAction::None)
        std::printf("PASS rz init-capture\n");
    else { std::printf("FAIL rz init-capture\n"); ++failures; }

    // Steady state: quiet.
    bool quiet = true;
    for (int i = 0; i < 30; ++i) {
        if (dm::evaluateResize(&st, in, &w, &h) != ResizeAction::None) quiet = false;
    }
    if (quiet) std::printf("PASS rz steady-quiet\n");
    else { std::printf("FAIL rz steady-quiet\n"); ++failures; }

    // Maximize to 1920x1040: needs the stability window, then resets to 1:1.
    in = {1920, 1040, 1280, 960, 0};
    bool sawPending = false; bool reset = false;
    for (int i = 0; i < 12; ++i) {
        if (dm::evaluateResize(&st, in, &w, &h) == ResizeAction::Reset) {
            reset = true; ++resets; break;
        }
        sawPending = true;
    }
    if (reset && w == 1920 && h == 1040)
        std::printf("PASS rz maximize-1to1 (needed stability window: %d)\n", int(sawPending));
    else { std::printf("FAIL rz maximize-1to1: reset=%d %ldx%ld\n", int(reset), w, h); ++failures; }

    // Cooldown suppresses immediately-following churn.
    in = {1600, 900, 1920, 1040, 0};
    bool suppressed = true;
    for (int i = 0; i < dm::resizeCooldownFlips(); ++i) {
        if (dm::evaluateResize(&st, in, &w, &h) != ResizeAction::None) suppressed = false;
    }
    if (suppressed) std::printf("PASS rz cooldown\n");
    else { std::printf("FAIL rz cooldown\n"); ++failures; }

    // Restore to the original 640x480 client: backbuffer returns to Revival's
    // configured 1280x960 (not 1:1 640x480).
    in = {640, 480, 1600, 900, 0};
    for (int i = 0; i < 40; ++i) {
        if (dm::evaluateResize(&st, in, &w, &h) == ResizeAction::Reset) { ++resets; break; }
    }
    if (w == 1280 && h == 960)
        std::printf("PASS rz restore-revival-config\n");
    else { std::printf("FAIL rz restore-revival-config: %ldx%ld\n", w, h); ++failures; }

    // Nothing triggers during the cooldown after a resize (the first 20 flips
    // here; Revival's borderless window is followed once it ends), and a
    // minimized window never triggers.
    in = {1920, 1040, 1280, 960, 1};
    bool none = true;
    for (int i = 0; i < 20; ++i)
        if (dm::evaluateResize(&st, in, &w, &h) != ResizeAction::None) none = false;
    in = {0, 0, 1280, 960, 0};
    for (int i = 0; i < 20; ++i)
        if (dm::evaluateResize(&st, in, &w, &h) != ResizeAction::None) none = false;
    if (none) std::printf("PASS rz borderless-minimize-quiet\n");
    else { std::printf("FAIL rz borderless-minimize-quiet\n"); ++failures; }
    (void)resets;
}


static void runFullscreenTests() {
    // Build a fake x86 D3DPRESENT_PARAMETERS: 1280x960, swap COPY (3),
    // windowed TRUE, interval DEFAULT (0). IMMEDIATE is 0x80000000.
    unsigned char pp[dm::kPp_Size];
    memset(pp, 0, sizeof(pp));
    auto w32 = [](unsigned char* p, int off, uint32_t v) {
        std::memcpy(p + off, &v, sizeof(v));
    };
    auto r32 = [](const unsigned char* p, int off) {
        uint32_t v = 0;
        std::memcpy(&v, p + off, sizeof(v));
        return v;
    };
    w32(pp, dm::kPp_BackBufferWidth, 1280);
    w32(pp, dm::kPp_BackBufferHeight, 960);
    w32(pp, dm::kPp_SwapEffect, 3);
    w32(pp, dm::kPp_Windowed, 1);

    unsigned char saved[dm::kPp_Size];
    dm::ppSave(saved, pp);
    dm::DisplayMode m{2560, 1440, 144, 22};  // 22 = D3DFMT_X8R8G8B8
    dm::ppEnterExclusive(pp, m, 0x12345678);
    bool ok = r32(pp, dm::kPp_BackBufferWidth) == 2560 &&
              r32(pp, dm::kPp_BackBufferHeight) == 1440 &&
              r32(pp, dm::kPp_SwapEffect) == dm::kSwapEffectDiscard &&
              r32(pp, dm::kPp_DeviceWindow) == 0x12345678 &&
              r32(pp, dm::kPp_Windowed) == 0 &&
              r32(pp, dm::kPp_RefreshRate) == 144 &&
              r32(pp, dm::kPp_BackBufferFormat) == 22 &&
              r32(pp, dm::kPp_PresentationInterval) == 0x80000000u;  // IMMEDIATE
    if (ok) std::printf("PASS fs enter-exclusive fields\n");
    else { std::printf("FAIL fs enter-exclusive fields\n"); ++failures; }

    dm::ppApplyVSync(pp, 1);
    ok = r32(pp, dm::kPp_PresentationInterval) == 1;
    dm::ppApplyVSync(pp, -1);
    ok = ok && r32(pp, dm::kPp_PresentationInterval) == 1;  // -1 keeps
    dm::ppApplyVSync(pp, 0);
    ok = ok && r32(pp, dm::kPp_PresentationInterval) == 0x80000000u;
    if (ok) std::printf("PASS fs vsync choices\n");
    else { std::printf("FAIL fs vsync choices\n"); ++failures; }

    dm::ppRestoreWindowed(pp, saved);
    ok = r32(pp, dm::kPp_Windowed) == 1 && dm::ppWidth(pp) == 1280 &&
         dm::ppHeight(pp) == 960 && r32(pp, dm::kPp_SwapEffect) == 3 &&
         std::memcmp(pp, saved, sizeof(pp)) == 0;
    if (ok) std::printf("PASS fs restore-windowed roundtrip\n");
    else { std::printf("FAIL fs restore-windowed roundtrip\n"); ++failures; }

    // The 165 Hz panel lists 165/120/59/60 at its desktop size.
    const unsigned long rates[] = {165, 120, 59, 60};
    ok = dm::nearestRefresh(rates, 4, 0, 165) == 165 &&      // 0 = desktop rate
         dm::nearestRefresh(rates, 4, 120, 165) == 120 &&
         dm::nearestRefresh(rates, 4, 144, 165) == 165 &&    // |144-165| < |144-120|
         dm::nearestRefresh(rates, 4, 130, 165) == 120 &&
         dm::nearestRefresh(rates, 4, 61, 165) == 60 &&
         dm::nearestRefresh(rates, 4, 240, 165) == 165 &&
         dm::nearestRefresh(rates, 0, 120, 165) == 165;
    const unsigned long tie[] = {100, 120};
    ok = ok && dm::nearestRefresh(tie, 2, 110, 60) == 120;  // ties take the higher
    if (ok) std::printf("PASS fs refresh snapping\n");
    else { std::printf("FAIL fs refresh snapping\n"); ++failures; }

    // Exclusive never runs below 64 Hz, even when asked for explicitly.
    const unsigned long laptop[] = {59, 60};
    const unsigned long tv[] = {50, 59, 60, 100, 120};
    ok = dm::pickExclusiveRefresh(rates, 4, 0, 165) == 165 &&   // desktop fast enough
         dm::pickExclusiveRefresh(laptop, 2, 0, 60) == 0 &&     // nothing >= 64: borderless
         dm::pickExclusiveRefresh(laptop, 2, 60, 60) == 0 &&    // explicit 60 refused too
         dm::pickExclusiveRefresh(tv, 5, 0, 60) == 100 &&       // lowest rate >= 64
         dm::pickExclusiveRefresh(tv, 5, 60, 60) == 100 &&      // explicit 60 -> nearest >= 64
         dm::pickExclusiveRefresh(rates, 4, 120, 165) == 120;   // explicit choice honoured
    if (ok) std::printf("PASS fs exclusive never below 64 Hz\n");
    else { std::printf("FAIL fs exclusive never below 64 Hz\n"); ++failures; }

    // Flip model: windowed parameters only; 2+ buffers, no MSAA, not lockable.
    unsigned char fp[dm::kPp_Size] = {};
    w32(fp, dm::kPp_Windowed, 1); w32(fp, dm::kPp_SwapEffect, 1); w32(fp, dm::kPp_BackBufferCount, 1);
    w32(fp, dm::kPp_MultiSampleType, 4); w32(fp, dm::kPp_Flags, 0x21); w32(fp, dm::kPp_PresentationInterval, 1);
    dm::ppUseFlipModel(fp);
    ok = r32(fp, dm::kPp_SwapEffect) == 5 && r32(fp, dm::kPp_BackBufferCount) == 2 &&
         r32(fp, dm::kPp_MultiSampleType) == 0 && r32(fp, dm::kPp_Flags) == 0x20 &&
         r32(fp, dm::kPp_PresentationInterval) == 0x80000000u;
    w32(fp, dm::kPp_Windowed, 0); w32(fp, dm::kPp_PresentationInterval, 1);
    dm::ppUseFlipModel(fp);
    ok = ok && r32(fp, dm::kPp_SwapEffect) == 1 && r32(fp, dm::kPp_PresentationInterval) == 1;  // exclusive: DISCARD
    w32(fp, dm::kPp_SwapEffect, 3);
    dm::ppUseFlipModel(fp);
    ok = ok && r32(fp, dm::kPp_SwapEffect) == 3;  // other exclusive swap effects untouched
    if (ok) std::printf("PASS fs flip model parameters\n");
    else { std::printf("FAIL fs flip model parameters\n"); ++failures; }

    // Present call-site patches: rel32 is measured from the end of the call.
    unsigned char call[5];
    dm::encodeRel32(call, 0xE8, 0x10001000u, 0x10002000u);
    ok = call[0] == 0xE8 && call[1] == 0xFB && call[2] == 0x0F && call[3] == 0 && call[4] == 0;
    dm::encodeRel32(call, 0xE9, 0x20000000u, 0x10000000u);  // backwards wraps
    ok = ok && call[0] == 0xE9 && call[1] == 0xFB && call[2] == 0xFF && call[3] == 0xFF && call[4] == 0xEF;
    if (ok) std::printf("PASS patch rel32 encoding\n");
    else { std::printf("FAIL patch rel32 encoding\n"); ++failures; }
}

static void runBarTests() {
    dm::Box bars[4];
    // Pillarbox: 1440x1080 image at x=240 in 1920x1080.
    int n = dm::letterboxBars(1920, 1080, {240, 0, 1680, 1080}, bars);
    bool ok = n == 2 && bars[0].left == 0 && bars[0].right == 240 &&
              bars[0].top == 0 && bars[0].bottom == 1080 &&
              bars[1].left == 1680 && bars[1].right == 1920;
    // Integer 1x inside 1280x960: four bars that tile without overlap.
    n = dm::letterboxBars(1280, 960, {320, 240, 960, 720}, bars);
    long area = 0;
    for (int i = 0; i < n; ++i)
        area += (bars[i].right - bars[i].left) * (bars[i].bottom - bars[i].top);
    ok = ok && n == 4 && area == 1280L * 960 - 640L * 480;
    ok = ok && dm::letterboxBars(1280, 960, {0, 0, 1280, 960}, bars) == 0;
    if (ok) std::printf("PASS geo letterbox bars\n");
    else { std::printf("FAIL geo letterbox bars (n=%d)\n", n); ++failures; }
}


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
    // Observation-log style formats (an older hook_flip.cpp layout).
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
                 1u, srcDesc, dstDesc, "NULL(full)", 2ul, 1920ul, 1080ul, 0ul,
                 " <changed>");
    expectFormat("ptrchain", "native 00112233->00445566",
                 "native %p->%p", (void*)0x112233, (void*)0x445566);
    // Validate the variadic wrapper and truncation at each buffer boundary.
    static const unsigned kProbes[] = {1u, 5u, 10u, 14u, 15u, 16u, 17u, 18u,
                                       20u, 31u, 32u, 33u, 40u, 100u, 599u};
    for (unsigned pi = 0; pi < sizeof(kProbes) / sizeof(kProbes[0]); ++pi) {
        unsigned probe = kProbes[pi];
        char probeBuf[600];
        std::memset(probeBuf, 'x', probe);
        probeBuf[probe] = '\0';
        char dst[16];
        int n = dm::formatString(dst, sizeof(dst), "%s", probeBuf);
        const size_t kept = probe < sizeof(dst) - 1 ? probe : sizeof(dst) - 1;
        if (n != static_cast<int>(probe) || std::strlen(dst) != kept ||
            std::memcmp(dst, probeBuf, kept) != 0) {
            std::printf("FAIL fmt truncation boundary %u: n=%d\n", probe, n);
            ++failures;
        }
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

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

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

    runFullscreenTests();
    runResizeTests();
    runGeometryTests();
    runBarTests();
    runFormatTests();

    if (failures == 0) {
        std::printf("ALL PASS\n");
        return 0;
    }
    return 1;
}
