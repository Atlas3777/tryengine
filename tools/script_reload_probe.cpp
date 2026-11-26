// Headless probe for the script generation swap: @live state, reload hooks and clocks.
//
// Uses the real ScriptSystem (compile, before hooks, new context, after hooks) without the
// editor, a window or a GPU. The probe script comes from tools/agent/script-reload-probe.sh.
#include <chrono>
#include <cstdio>
#include <daScript/daScript.h>
#include <thread>

#include "engine/core/FsFileAccessFactory.hpp"
#include "engine/core/LiveHost.hpp"
#include "engine/core/ScriptSystem.hpp"

DECLARE_MODULE(Module_LiveHost);

namespace {

//! The host-side start of a generation. The editor calls EngineStart/EditorStart here; the
//! probe script has its own `ProbeSetup` entry point instead (the engine's EngineStart pulls
//! just_in_time and needs the LLVM module, which this probe does not load).
void RegisterProbeModules() {
    das::setDasRoot(DAS_ROOT_DIR);

    NEED_ALL_DEFAULT_MODULES;
    PULL_MODULE(Module_LiveHost);
}

struct ProbeReloadExtension final : tryengine::core::IScriptReloadExtension {
    void ReinitializeContext(tryengine::core::ScriptSystem&) override {
        // Same contract the editor provides: the new generation is a reload, not a cold start.
        live_host_set_is_reload(true);
    }

    void OnReloadFinished(bool success, eastl::string_view error) override {
        std::printf("[PROBE] reload finished: success=%d error=%.*s\n", success ? 1 : 0, static_cast<int>(error.size()),
                    error.data());
    }
};

bool Check(bool ok, const char* what) {
    if (!ok)
        std::printf("FAIL: %s\n", what);
    return ok;
}

void PrintError(const char* what, const tryengine::core::Error& error) {
    const eastl::string_view message = error.Message();
    std::printf("FAIL: %s: %.*s\n", what, static_cast<int>(message.size()), message.data());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: script-reload-probe <probe.das>\n");
        return 2;
    }

    tryengine::core::ScriptSystemConfig config{
        .register_modules = &RegisterProbeModules,
        .create_file_access = []() -> das::smart_ptr<das::FileAccess> {
            return tryengine::core::MakeFsFileAccess("project.das_project");
        },
        .create_context = [](uint32_t stack_size) -> das::Context* { return new das::Context(stack_size); },
        // Interpreted simulation is enough for the probe: no LLVM module, no JIT cache.
        .policies =
            []() {
                das::CodeOfPolicies policies;
                policies.jit_enabled = false;
                return policies;
            }(),
    };

    tryengine::core::ScriptSystem scripts(config);
    ProbeReloadExtension extension;
    scripts.SetReloadExtension(&extension);

    auto loaded = scripts.LoadMainScript(argv[1]);
    if (!loaded.has_value()) {
        PrintError("first load", loaded.error());
        return 1;
    }

    bool ok = true;
    ok = Check(scripts.InvokeFunctionSafe<"ProbeSetup">(), "ProbeSetup did not run") && ok;

    // Two ticks 300 ms apart give the root clock a non-zero total to preserve.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    ok = Check(scripts.InvokeFunctionSafe<"ProbeTick">(), "the first ProbeTick did not run") && ok;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    ok = Check(scripts.InvokeFunctionSafe<"ProbeTick">(), "the second ProbeTick did not run") && ok;
    ok = Check(scripts.InvokeFunctionSafe<"ProbeSet">(), "ProbeSet did not run") && ok;

    auto reloaded = scripts.ReloadMainScript();
    if (!reloaded.has_value()) {
        PrintError("reload", reloaded.error());
        return 1;
    }

    // A hook failure is logged but the reload itself still succeeds; the script's own checks
    // decide whether the state actually came back.
    ok = Check(scripts.InvokeFunctionSafe<"ProbeCheck">(), "ProbeCheck did not run") && ok;

    auto restored = scripts.SimpleReturnUnsafe<"ProbeOk", bool>();
    if (!restored.has_value()) {
        std::printf("FAIL: ProbeOk was not called\n");
        return 1;
    }
    if (!*restored) {
        std::printf("FAIL: @live state or the root clock did not survive the reload\n");
        ok = false;
    }

    if (!ok)
        return 1;
    std::printf("script reload: ok\n");
    return 0;
}
