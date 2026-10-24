#pragma once
#include <windows.h>
#include <d3d9.h>
#include "profiles.h"

namespace dm {
enum TracePoint { TraceEntry, SourceLockBegin, SourceLockEnd, UploadLockBegin, UploadLockEnd,
    UploadUnlockBegin, UploadUnlockEnd, SourceUnlockBegin, SourceUnlockEnd,
    UploadCopyBegin, UploadCopyEnd, FinalCopyBegin, PresentBegin, PresentEnd, TracePointCount };
enum TraceResult { SourceLockResult, UploadLockResult, UploadUnlockResult,
    SourceUnlockResult, UploadCopyResult, PresentResult, FinalCopyResult, TraceResultCount };
// File layout is shared with tools/read_frame_trace.py; times are raw QPC ticks.
struct FrameTraceRecord {
    unsigned long iteration, nativeCompleted, generation, windowed, dynamicUpload;
    unsigned long uploadedIteration, submittedIteration, presentAttempts, source, destination;
    long result[TraceResultCount];
    LARGE_INTEGER tick[TracePointCount];
};
void uploadTraceConfigure(HMODULE module, unsigned char* base, const DdrawProfile* profile,
                          bool dynamicUpload, unsigned frames);
void uploadTraceFrameStart();
void uploadTraceMark(TracePoint point);
void uploadTracePresent(HRESULT result);
void uploadTraceFinalCopy(HRESULT result);
}
