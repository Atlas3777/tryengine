#include <daScript/daScript.h>
#include <daScript/simulate/aot.h>

#include "engine/core/Profiler.hpp"

using namespace das;

#if TRY_PROFILE_ENABLED

namespace {

tryengine::core::Profiler& GetProfiler() {
    return tryengine::core::Profiler::Instance();
}

const tryengine::core::ProfileEvent* GetEvent(const uint32_t frame, const uint32_t index) {
    return GetProfiler().FrameEvent(frame, index);
}

}  // namespace

//! true, if the profiler is compiled into the engine (TRY_PROFILE_ENABLED). The daslang
//! [profile] macro reads it at compile time and emits no instrumentation when false.
bool ProfileIsEnabled() {
    return true;
}

//! Scope token: the script keeps it as uint64 and returns it to ProfileEnd.
uint64_t ProfileBegin(const char* name) {
    return GetProfiler().BeginScope(name);
}

void ProfileEnd(uint64_t token) {
    GetProfiler().EndScope(token);
}

uint32_t ProfileScopeCount() {
    return GetProfiler().ScopeCount();
}

char* ProfileScopeName(uint32_t index, Context* ctx, LineInfoArg* at) {
    const auto& entry = GetProfiler().Scope(index);
    return ctx->allocateString(entry.name.c_str(), entry.name.size(), at);
}

int ProfileScopeKind(uint32_t index) {
    return static_cast<int>(GetProfiler().Scope(index).kind);
}

uint32_t ProfileScopeDepth(uint32_t index) {
    return GetProfiler().Scope(index).depth;
}

uint32_t ProfileScopeCalls(uint32_t index) {
    return GetProfiler().Scope(index).calls;
}

uint64_t ProfileScopeTotalCalls(uint32_t index) {
    return GetProfiler().ScopeTotalCalls(index);
}

float ProfileScopeTotalMs(uint32_t index) {
    return GetProfiler().ScopeTotalMs(index);
}

float ProfileScopeMinMs(uint32_t index) {
    return GetProfiler().ScopeMinMs(index);
}

float ProfileScopeMaxMs(uint32_t index) {
    return GetProfiler().ScopeMaxMs(index);
}

uint32_t ProfileScopeHistoryCount(uint32_t scope) {
    return GetProfiler().ScopeHistoryCount(scope);
}

float ProfileScopeHistoryAt(uint32_t scope, uint32_t frame) {
    return GetProfiler().ScopeHistoryAt(scope, frame);
}

uint32_t ProfileFrameCount() {
    return GetProfiler().FrameCount();
}

float ProfileFrameMs(uint32_t frame) {
    return GetProfiler().FrameDeltaMs(frame);
}

uint32_t ProfileFrameEventCount(uint32_t frame) {
    return GetProfiler().FrameEventCount(frame);
}

uint32_t ProfileEventScope(uint32_t frame, uint32_t index) {
    const auto* event = GetEvent(frame, index);
    return event ? event->scope : 0u;
}

float ProfileEventStart(uint32_t frame, uint32_t index) {
    const auto* event = GetEvent(frame, index);
    return event ? event->start_ms : 0.0f;
}

float ProfileEventDuration(uint32_t frame, uint32_t index) {
    const auto* event = GetEvent(frame, index);
    return event ? event->duration_ms : 0.0f;
}

uint32_t ProfileEventDepth(uint32_t frame, uint32_t index) {
    const auto* event = GetEvent(frame, index);
    return event ? event->depth : 0u;
}

int ProfileEventKind(uint32_t frame, uint32_t index) {
    const auto* event = GetEvent(frame, index);
    return event ? static_cast<int>(event->kind) : 0;
}

int32_t ProfileEventParent(uint32_t frame, uint32_t index) {
    return GetProfiler().EventParent(frame, index);
}

char* ProfileExportToLogs(Context* ctx, LineInfoArg* at) {
    const eastl::string path = GetProfiler().ExportToLogs();
    return ctx->allocateString(path.c_str(), path.size(), at);
}

//! Starts the on-disk capture (compact frames + one status line per second) under ./logs/.
bool ProfileStartCapture(uint64_t max_bytes) {
    return GetProfiler().StartCapture(max_bytes);
}

void ProfileStopCapture() {
    GetProfiler().StopCapture();
}

bool ProfileIsCapturing() {
    return GetProfiler().IsCapturing();
}

char* ProfileCapturePath(Context* ctx, LineInfoArg* at) {
    const eastl::string path = GetProfiler().CapturePath();
    return ctx->allocateString(path.c_str(), path.size(), at);
}

char* ProfileStatusPath(Context* ctx, LineInfoArg* at) {
    const eastl::string path = GetProfiler().StatusPath();
    return ctx->allocateString(path.c_str(), path.size(), at);
}

uint64_t ProfileCaptureBytes() {
    return GetProfiler().CaptureBytes();
}

uint32_t ProfileCapturedFrames() {
    return GetProfiler().CapturedFrames();
}

bool ProfileCaptureCapped() {
    return GetProfiler().IsCaptureCapped();
}

//! Loads the newest *.tryprof under ./logs/ over the current history. False when there is none.
bool ProfileImportLatestLog() {
    const eastl::string path = GetProfiler().LatestLogPath();
    if (path.empty())
        return false;
    return GetProfiler().ImportLog(path);
}

//! Aggregates over an absolute frame range, so the zoomed-out timeline makes a few hundred
//! calls instead of one per visible call.
uint64_t ProfileRangeEventCount(uint32_t first, uint32_t last) {
    return GetProfiler().RangeEventCount(first, last);
}

float ProfileRangeFrameMsMax(uint32_t first, uint32_t last) {
    return GetProfiler().RangeFrameMsMax(first, last);
}

float ProfileRangeFrameMsAvg(uint32_t first, uint32_t last) {
    return GetProfiler().RangeFrameMsAvg(first, last);
}

float ProfileRangeScopeMs(uint32_t scope, uint32_t first, uint32_t last) {
    return GetProfiler().RangeScopeMs(scope, first, last);
}

bool ProfileIsRecording() {
    return GetProfiler().IsRecording();
}

void ProfileSetRecording(bool enabled) {
    GetProfiler().SetRecording(enabled);
}

void ProfileReset() {
    GetProfiler().Reset();
}

#else  // TRY_PROFILE_ENABLED

// Profiling is compiled out: the bindings stay so the script panel and the .das gate keep
// compiling, but they always answer "empty".
bool ProfileIsEnabled() {
    return false;
}

uint64_t ProfileBegin(const char*) {
    return 0;
}

void ProfileEnd(uint64_t) {}

uint32_t ProfileScopeCount() {
    return 0;
}

char* ProfileScopeName(uint32_t, Context*, LineInfoArg*) {
    return nullptr;
}

int ProfileScopeKind(uint32_t) {
    return 0;
}

uint32_t ProfileScopeDepth(uint32_t) {
    return 0;
}

uint32_t ProfileScopeCalls(uint32_t) {
    return 0;
}

uint64_t ProfileScopeTotalCalls(uint32_t) {
    return 0;
}

float ProfileScopeTotalMs(uint32_t) {
    return 0.0f;
}

float ProfileScopeMinMs(uint32_t) {
    return 0.0f;
}

float ProfileScopeMaxMs(uint32_t) {
    return 0.0f;
}

uint32_t ProfileScopeHistoryCount(uint32_t) {
    return 0;
}

float ProfileScopeHistoryAt(uint32_t, uint32_t) {
    return 0.0f;
}

uint32_t ProfileFrameCount() {
    return 0;
}

float ProfileFrameMs(uint32_t) {
    return 0.0f;
}

uint32_t ProfileFrameEventCount(uint32_t) {
    return 0;
}

uint32_t ProfileEventScope(uint32_t, uint32_t) {
    return 0;
}

float ProfileEventStart(uint32_t, uint32_t) {
    return 0.0f;
}

float ProfileEventDuration(uint32_t, uint32_t) {
    return 0.0f;
}

uint32_t ProfileEventDepth(uint32_t, uint32_t) {
    return 0;
}

int ProfileEventKind(uint32_t, uint32_t) {
    return 0;
}

int32_t ProfileEventParent(uint32_t, uint32_t) {
    return -1;
}

char* ProfileExportToLogs(Context* ctx, LineInfoArg* at) {
    return ctx->allocateString("", 0, at);
}

bool ProfileStartCapture(uint64_t) {
    return false;
}

void ProfileStopCapture() {}

bool ProfileIsCapturing() {
    return false;
}

char* ProfileCapturePath(Context* ctx, LineInfoArg* at) {
    return ctx->allocateString("", 0, at);
}

char* ProfileStatusPath(Context* ctx, LineInfoArg* at) {
    return ctx->allocateString("", 0, at);
}

uint64_t ProfileCaptureBytes() {
    return 0;
}

uint32_t ProfileCapturedFrames() {
    return 0;
}

bool ProfileCaptureCapped() {
    return false;
}

bool ProfileImportLatestLog() {
    return false;
}

uint64_t ProfileRangeEventCount(uint32_t, uint32_t) {
    return 0;
}

float ProfileRangeFrameMsMax(uint32_t, uint32_t) {
    return 0.0f;
}

float ProfileRangeFrameMsAvg(uint32_t, uint32_t) {
    return 0.0f;
}

float ProfileRangeScopeMs(uint32_t, uint32_t, uint32_t) {
    return 0.0f;
}

bool ProfileIsRecording() {
    return false;
}

void ProfileSetRecording(bool) {}

void ProfileReset() {}

#endif  // TRY_PROFILE_ENABLED

class Module_Renderer : public das::Module {
public:
    Module_Renderer() : Module("tryRenderer") {
        das::ModuleLibrary lib(this);
        lib.addBuiltInModule();

        das::addExtern<DAS_BIND_FUN(ProfileIsEnabled)>(*this, lib, "ProfileIsEnabled", das::SideEffects::none,
                                                       "ProfileIsEnabled");
        das::addExtern<DAS_BIND_FUN(ProfileBegin)>(*this, lib, "ProfileBegin", das::SideEffects::accessGlobal,
                                                   "ProfileBegin");
        das::addExtern<DAS_BIND_FUN(ProfileEnd)>(*this, lib, "ProfileEnd", das::SideEffects::accessGlobal,
                                                 "ProfileEnd");
        das::addExtern<DAS_BIND_FUN(ProfileScopeCount)>(*this, lib, "ProfileScopeCount", das::SideEffects::accessGlobal,
                                                        "ProfileScopeCount");
        das::addExtern<DAS_BIND_FUN(ProfileScopeName)>(*this, lib, "ProfileScopeName", das::SideEffects::accessGlobal,
                                                       "ProfileScopeName");
        das::addExtern<DAS_BIND_FUN(ProfileScopeKind)>(*this, lib, "ProfileScopeKind", das::SideEffects::accessGlobal,
                                                       "ProfileScopeKind");
        das::addExtern<DAS_BIND_FUN(ProfileScopeDepth)>(*this, lib, "ProfileScopeDepth", das::SideEffects::accessGlobal,
                                                        "ProfileScopeDepth");
        das::addExtern<DAS_BIND_FUN(ProfileScopeCalls)>(*this, lib, "ProfileScopeCalls", das::SideEffects::accessGlobal,
                                                        "ProfileScopeCalls");
        das::addExtern<DAS_BIND_FUN(ProfileScopeTotalCalls)>(*this, lib, "ProfileScopeTotalCalls",
                                                             das::SideEffects::accessGlobal, "ProfileScopeTotalCalls");
        das::addExtern<DAS_BIND_FUN(ProfileScopeTotalMs)>(*this, lib, "ProfileScopeTotalMs",
                                                          das::SideEffects::accessGlobal, "ProfileScopeTotalMs");
        das::addExtern<DAS_BIND_FUN(ProfileScopeMinMs)>(*this, lib, "ProfileScopeMinMs", das::SideEffects::accessGlobal,
                                                        "ProfileScopeMinMs");
        das::addExtern<DAS_BIND_FUN(ProfileScopeMaxMs)>(*this, lib, "ProfileScopeMaxMs", das::SideEffects::accessGlobal,
                                                        "ProfileScopeMaxMs");
        das::addExtern<DAS_BIND_FUN(ProfileScopeHistoryCount)>(
            *this, lib, "ProfileScopeHistoryCount", das::SideEffects::accessGlobal, "ProfileScopeHistoryCount");
        das::addExtern<DAS_BIND_FUN(ProfileScopeHistoryAt)>(*this, lib, "ProfileScopeHistoryAt",
                                                            das::SideEffects::accessGlobal, "ProfileScopeHistoryAt");
        das::addExtern<DAS_BIND_FUN(ProfileFrameCount)>(*this, lib, "ProfileFrameCount", das::SideEffects::accessGlobal,
                                                        "ProfileFrameCount");
        das::addExtern<DAS_BIND_FUN(ProfileFrameMs)>(*this, lib, "ProfileFrameMs", das::SideEffects::accessGlobal,
                                                     "ProfileFrameMs");
        das::addExtern<DAS_BIND_FUN(ProfileFrameEventCount)>(*this, lib, "ProfileFrameEventCount",
                                                             das::SideEffects::accessGlobal, "ProfileFrameEventCount");
        das::addExtern<DAS_BIND_FUN(ProfileEventScope)>(*this, lib, "ProfileEventScope", das::SideEffects::accessGlobal,
                                                        "ProfileEventScope");
        das::addExtern<DAS_BIND_FUN(ProfileEventStart)>(*this, lib, "ProfileEventStart", das::SideEffects::accessGlobal,
                                                        "ProfileEventStart");
        das::addExtern<DAS_BIND_FUN(ProfileEventDuration)>(*this, lib, "ProfileEventDuration",
                                                           das::SideEffects::accessGlobal, "ProfileEventDuration");
        das::addExtern<DAS_BIND_FUN(ProfileEventDepth)>(*this, lib, "ProfileEventDepth", das::SideEffects::accessGlobal,
                                                        "ProfileEventDepth");
        das::addExtern<DAS_BIND_FUN(ProfileEventKind)>(*this, lib, "ProfileEventKind", das::SideEffects::accessGlobal,
                                                       "ProfileEventKind");
        das::addExtern<DAS_BIND_FUN(ProfileEventParent)>(*this, lib, "ProfileEventParent",
                                                         das::SideEffects::accessGlobal, "ProfileEventParent");
        das::addExtern<DAS_BIND_FUN(ProfileExportToLogs)>(*this, lib, "ProfileExportToLogs",
                                                          das::SideEffects::accessGlobal, "ProfileExportToLogs");
        das::addExtern<DAS_BIND_FUN(ProfileStartCapture)>(*this, lib, "ProfileStartCapture",
                                                          das::SideEffects::accessGlobal, "ProfileStartCapture");
        das::addExtern<DAS_BIND_FUN(ProfileStopCapture)>(*this, lib, "ProfileStopCapture",
                                                         das::SideEffects::accessGlobal, "ProfileStopCapture");
        das::addExtern<DAS_BIND_FUN(ProfileIsCapturing)>(*this, lib, "ProfileIsCapturing",
                                                         das::SideEffects::accessGlobal, "ProfileIsCapturing");
        das::addExtern<DAS_BIND_FUN(ProfileCapturePath)>(*this, lib, "ProfileCapturePath",
                                                         das::SideEffects::accessGlobal, "ProfileCapturePath");
        das::addExtern<DAS_BIND_FUN(ProfileStatusPath)>(*this, lib, "ProfileStatusPath", das::SideEffects::accessGlobal,
                                                        "ProfileStatusPath");
        das::addExtern<DAS_BIND_FUN(ProfileCaptureBytes)>(*this, lib, "ProfileCaptureBytes",
                                                          das::SideEffects::accessGlobal, "ProfileCaptureBytes");
        das::addExtern<DAS_BIND_FUN(ProfileCapturedFrames)>(*this, lib, "ProfileCapturedFrames",
                                                            das::SideEffects::accessGlobal, "ProfileCapturedFrames");
        das::addExtern<DAS_BIND_FUN(ProfileCaptureCapped)>(*this, lib, "ProfileCaptureCapped",
                                                           das::SideEffects::accessGlobal, "ProfileCaptureCapped");
        das::addExtern<DAS_BIND_FUN(ProfileImportLatestLog)>(*this, lib, "ProfileImportLatestLog",
                                                             das::SideEffects::accessGlobal, "ProfileImportLatestLog");
        das::addExtern<DAS_BIND_FUN(ProfileRangeEventCount)>(*this, lib, "ProfileRangeEventCount",
                                                             das::SideEffects::accessGlobal, "ProfileRangeEventCount");
        das::addExtern<DAS_BIND_FUN(ProfileRangeFrameMsMax)>(*this, lib, "ProfileRangeFrameMsMax",
                                                             das::SideEffects::accessGlobal, "ProfileRangeFrameMsMax");
        das::addExtern<DAS_BIND_FUN(ProfileRangeFrameMsAvg)>(*this, lib, "ProfileRangeFrameMsAvg",
                                                             das::SideEffects::accessGlobal, "ProfileRangeFrameMsAvg");
        das::addExtern<DAS_BIND_FUN(ProfileRangeScopeMs)>(*this, lib, "ProfileRangeScopeMs",
                                                          das::SideEffects::accessGlobal, "ProfileRangeScopeMs");
        das::addExtern<DAS_BIND_FUN(ProfileIsRecording)>(*this, lib, "ProfileIsRecording",
                                                         das::SideEffects::accessGlobal, "ProfileIsRecording");
        das::addExtern<DAS_BIND_FUN(ProfileSetRecording)>(*this, lib, "ProfileSetRecording",
                                                          das::SideEffects::accessGlobal, "ProfileSetRecording");
        das::addExtern<DAS_BIND_FUN(ProfileReset)>(*this, lib, "ProfileReset", das::SideEffects::accessGlobal,
                                                   "ProfileReset");

        verifyAotReady();
    }
};

REGISTER_DYN_MODULE(Module_Renderer, Module_Renderer);
REGISTER_MODULE(Module_Renderer);
