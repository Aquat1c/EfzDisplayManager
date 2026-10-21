#include "reset_transaction.h"
#include <cstdio>
#include <cstring>
#include <cstdint>

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)
struct Fake {
    dm::ResetTransaction tx{};
    int prepareCalls = 0, resetCalls = 0, rebuildCalls = 0, finishCalls = 0, loseCalls = 0;
    bool allowPrepare = true, allowRebuild = true, allowFinish = true;
    int32_t results[8]{};
    unsigned char inputs[8][56]{};
    bool testReentry = false;
    dm::ResetOps ops() { return {this, prepare, reset, rebuild, finish, lost}; }
    static bool prepare(void* p) {
        auto& f = *static_cast<Fake*>(p); ++f.prepareCalls;
        if (f.testReentry) {
            unsigned char params[56]{};
            CHECK(dm::resetStart(&f.tx, f.ops(), params, nullptr) == dm::ResetOutcome::Refused);
            CHECK(dm::resetContinue(&f.tx, f.ops()) == dm::ResetOutcome::Refused);
        }
        return f.allowPrepare;
    }
    static int32_t reset(void* p, unsigned char* parameters) {
        auto& f = *static_cast<Fake*>(p);
        const int n = f.resetCalls++;
        std::memcpy(f.inputs[n], parameters, 56);
        // Real Reset is an in/out API. It is allowed to zero/mutate fields.
        std::memset(parameters, 0xcc, 56);
        return f.results[n];
    }
    static bool rebuild(void* p) { auto& f = *static_cast<Fake*>(p); ++f.rebuildCalls; return f.allowRebuild; }
    static bool finish(void* p) { auto& f = *static_cast<Fake*>(p); ++f.finishCalls; return f.allowFinish; }
    static void lost(void* p) { ++static_cast<Fake*>(p)->loseCalls; }
};
constexpr int32_t invalid = static_cast<int32_t>(0x8876086cu);
}
int main() {
    unsigned char request[56], previous[56];
    std::memset(request, 0x19, 56); std::memset(previous, 0x42, 56);
    {
        Fake f; f.allowPrepare = false;
        CHECK(dm::resetStart(&f.tx, f.ops(), request, previous) == dm::ResetOutcome::Refused);
        CHECK(f.resetCalls == 0 && f.rebuildCalls == 0 && f.finishCalls == 0);
        CHECK(f.tx.phase == dm::ResetPhase::Idle);
    }
    {
        Fake f; f.results[0] = invalid; f.results[1] = invalid;
        CHECK(dm::resetStart(&f.tx, f.ops(), request, previous) == dm::ResetOutcome::Pending);
        CHECK(f.prepareCalls == 1 && f.resetCalls == 2);
        CHECK(f.rebuildCalls == 0 && f.finishCalls == 0);
        CHECK(f.tx.phase == dm::ResetPhase::NeedReset);
        CHECK(std::memcmp(f.inputs[0], request, 56) == 0);
        CHECK(std::memcmp(f.inputs[1], previous, 56) == 0);
        CHECK(dm::resetContinue(&f.tx, f.ops()) == dm::ResetOutcome::RestoredReady);
        CHECK(f.prepareCalls == 1 && f.resetCalls == 3 && f.rebuildCalls == 1 && f.finishCalls == 1);
        CHECK(std::memcmp(f.inputs[2], previous, 56) == 0);
        CHECK(std::memcmp(f.tx.committed, previous, 56) == 0);
    }
    {
        Fake f; f.results[0] = invalid;
        CHECK(dm::resetStart(&f.tx, f.ops(), request, previous) == dm::ResetOutcome::RestoredReady);
        CHECK(f.resetCalls == 2 && f.rebuildCalls == 1 && f.finishCalls == 1);
    }
    {
        Fake f; f.allowRebuild = false;
        CHECK(dm::resetStart(&f.tx, f.ops(), request, previous) == dm::ResetOutcome::Pending);
        CHECK(f.resetCalls == 1 && f.finishCalls == 0);
        CHECK(f.tx.phase == dm::ResetPhase::NeedResources);
        CHECK(std::memcmp(f.tx.committed, request, 56) == 0);
        CHECK(dm::resetStart(&f.tx, f.ops(), previous, nullptr) == dm::ResetOutcome::Refused);
        f.allowRebuild = true;
        CHECK(dm::resetContinue(&f.tx, f.ops()) == dm::ResetOutcome::RequestedReady);
        CHECK(f.prepareCalls == 1 && f.resetCalls == 1 && f.finishCalls == 1);
    }
    {
        Fake f; f.allowRebuild = false;
        CHECK(dm::resetStart(&f.tx, f.ops(), request, previous) == dm::ResetOutcome::Pending);
        dm::resetLostAgain(&f.tx, f.ops());
        dm::resetLostAgain(&f.tx, f.ops());
        CHECK(f.loseCalls == 1 && f.tx.phase == dm::ResetPhase::NeedReset);
        f.allowRebuild = true;
        CHECK(dm::resetContinue(&f.tx, f.ops()) == dm::ResetOutcome::RequestedReady);
        CHECK(f.prepareCalls == 1 && f.resetCalls == 2);
        CHECK(std::memcmp(f.inputs[1], request, 56) == 0);
    }
    {
        Fake f; f.testReentry = true;
        CHECK(dm::resetStart(&f.tx, f.ops(), request, nullptr) == dm::ResetOutcome::RequestedReady);
        CHECK(f.prepareCalls == 1 && f.resetCalls == 1);
    }
    {
        Fake f; f.results[0] = invalid; f.results[1] = invalid;
        CHECK(dm::resetStart(&f.tx, f.ops(), request, nullptr) == dm::ResetOutcome::Pending);
        CHECK(f.resetCalls == 1 && f.rebuildCalls == 0);
        CHECK(dm::resetContinue(&f.tx, f.ops()) == dm::ResetOutcome::Pending);
        CHECK(std::memcmp(f.inputs[1], request, 56) == 0 && f.prepareCalls == 1);
        CHECK(dm::resetContinue(&f.tx, f.ops()) == dm::ResetOutcome::RequestedReady);
    }
    {
        Fake f; f.allowFinish = false;
        CHECK(dm::resetStart(&f.tx, f.ops(), request, nullptr) == dm::ResetOutcome::Pending);
        f.allowFinish = true;
        CHECK(dm::resetContinue(&f.tx, f.ops()) == dm::ResetOutcome::RequestedReady);
        CHECK(f.resetCalls == 1 && f.prepareCalls == 1);
    }
    std::printf("reset transaction: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
