#pragma once
#include <cstdint>

namespace dm {

// Independent of COM so the production failure/rollback ordering is testable.
// D3DPRESENT_PARAMETERS has 56 bytes in the supported Win32 build.
enum { kResetParameterBytes = 56 };
enum class ResetPhase { Idle, Preparing, NeedReset, NeedResources };
enum class ResetOutcome { Refused, Pending, RequestedReady, RestoredReady };

struct ResetOps {
    void* context;
    // A refusal must leave wrapper/manager resources intact. Participants
    // already quiesced by a refused prepare must be cancelled by the adapter.
    bool (*prepare)(void*);
    int32_t (*reset)(void*, unsigned char* mutableParameters);
    bool (*rebuild)(void*);
    bool (*finish)(void*);
    void (*losePartial)(void*);
};

struct ResetTransaction {
    ResetPhase phase;
    bool executing;
    bool restoring;
    bool canRestore;
    int32_t lastResult;
    unsigned char requested[kResetParameterBytes];
    unsigned char previous[kResetParameterBytes];
    unsigned char committed[kResetParameterBytes];
};

ResetOutcome resetStart(ResetTransaction* tx, const ResetOps& ops,
                        const unsigned char* requested,
                        const unsigned char* previous);
ResetOutcome resetContinue(ResetTransaction* tx, const ResetOps& ops);
// A second device loss while rebuilding must release only partially rebuilt
// resources before the next Reset; it must not repeat participant prepare.
void resetLostAgain(ResetTransaction* tx, const ResetOps& ops);

}  // namespace dm
