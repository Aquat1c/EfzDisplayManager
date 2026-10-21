#include "reset_transaction.h"
#include <cstring>

namespace dm {
namespace {
bool attempt(ResetTransaction* tx, const ResetOps& ops) {
    const unsigned char* canonical = tx->restoring ? tx->previous : tx->requested;
    // Reset mutates its argument even on some failures. Never retry that buffer.
    alignas(4) unsigned char parameters[kResetParameterBytes];
    std::memcpy(parameters, canonical, sizeof(parameters));
    tx->lastResult = ops.reset(ops.context, parameters);
    if (tx->lastResult < 0) return false;
    std::memcpy(tx->committed, canonical, sizeof(tx->committed));
    tx->phase = ResetPhase::NeedResources;
    return true;
}
ResetOutcome rebuild(ResetTransaction* tx, const ResetOps& ops) {
    if (!ops.rebuild(ops.context) || !ops.finish(ops.context)) return ResetOutcome::Pending;
    tx->phase = ResetPhase::Idle;
    return tx->restoring ? ResetOutcome::RestoredReady : ResetOutcome::RequestedReady;
}
}

ResetOutcome resetStart(ResetTransaction* tx, const ResetOps& ops,
                        const unsigned char* requested, const unsigned char* previous) {
    if (!tx || !requested || tx->executing || tx->phase != ResetPhase::Idle)
        return ResetOutcome::Refused;
    tx->executing = true;
    tx->phase = ResetPhase::Preparing;
    tx->restoring = false;
    tx->canRestore = previous != nullptr;
    std::memcpy(tx->requested, requested, sizeof(tx->requested));
    if (previous) std::memcpy(tx->previous, previous, sizeof(tx->previous));
    if (!ops.prepare(ops.context)) {
        tx->phase = ResetPhase::Idle;
        tx->executing = false;
        return ResetOutcome::Refused;
    }
    tx->phase = ResetPhase::NeedReset;
    bool ok = attempt(tx, ops);
    if (!ok && tx->canRestore) {
        tx->restoring = true;
        ok = attempt(tx, ops);
    }
    const ResetOutcome result = ok ? rebuild(tx, ops) : ResetOutcome::Pending;
    tx->executing = false;
    return result;
}

ResetOutcome resetContinue(ResetTransaction* tx, const ResetOps& ops) {
    if (!tx || tx->executing || tx->phase == ResetPhase::Idle ||
        tx->phase == ResetPhase::Preparing) return ResetOutcome::Refused;
    tx->executing = true;
    ResetOutcome result = ResetOutcome::Pending;
    if (tx->phase == ResetPhase::NeedResources || attempt(tx, ops)) result = rebuild(tx, ops);
    tx->executing = false;
    return result;
}

void resetLostAgain(ResetTransaction* tx, const ResetOps& ops) {
    if (!tx || tx->executing || tx->phase != ResetPhase::NeedResources) return;
    tx->executing = true;
    ops.losePartial(ops.context);
    tx->phase = ResetPhase::NeedReset;
    tx->executing = false;
}
}
