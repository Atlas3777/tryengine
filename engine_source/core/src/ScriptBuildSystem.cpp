// engine/core/ScriptBuildSystem.cpp
#include "engine/core/ScriptBuildSystem.hpp"

#include "engine/core/Assert.hpp"
#include "engine/core/FsFileAccessFactory.hpp"
#include "engine/core/MakeError.hpp"
#include "engine/core/ScriptPolicies.hpp"

namespace tryengine::core {

ScriptBuildSystem::ScriptBuildSystem(ScriptBuildConfig config) : config_(eastl::move(config)) {
    TRY_ASSERT(config_.create_context, "Фабрика контекста должна быть задана!");
}

ScriptBuildSystem::~ScriptBuildSystem() {
    if (das_ctx_) {
        delete das_ctx_;
        das_ctx_ = nullptr;
    }
}

Result<void> ScriptBuildSystem::TestCompile(eastl::span<const uint8_t> data) {
    const das::CodeOfPolicies policies = CreatePakPolicies();

    auto pak = DeserializeScriptPak(data);
    if (!pak.has_value())
        return LogAndMakeError("Битый или несовместимый скрипт-пак");

    auto access = das::make_smart<PackedFileAccess>(eastl::move(*pak));
    pak_entry_ = access->EntryPath();

    das::TextPrinter tout;
    das::ModuleGroup dummy_group;

    const auto program = das::compileDaScript(pak_entry_.c_str(), access, tout, dummy_group, policies);

    if (!program)
        return LogAndMakeError("Ошибка компиляции скрипта: {}", pak_entry_.c_str());
    if (program->failed()) {
        for (const auto& error : program->errors)
            LogError(das::reportError(error.at, error.what, error.extra, error.fixme, error.cerr).c_str());
        return LogAndMakeError("Ошибка компиляции скрипта: {}", pak_entry_.c_str());
    }

    auto* new_ctx = config_.create_context(program->getContextStackSize());

    if (!program->simulate(*new_ctx, tout)) {
        delete new_ctx;
        return LogAndMakeError("Ошибка симуляции контекста! {}", tout.c_str());
    }

    if (das_ctx_)
        delete das_ctx_;
    das_ctx_ = new_ctx;

    LogInfo(LogCategory::Script, "TestCompile {} успешно скомпилирован", pak_entry_.c_str());

    auto* function = das_ctx_->findFunction("TestTestTest");
    if (!function) {
        LogError(LogCategory::Script, "Function not found: TestTestTest");
        return {};
    }

    das::Func custom_func(function);
    das::das_invoke_function<void>::invoke(das_ctx_, nullptr, custom_func);
    return {};
}

Result<eastl::vector<uint8_t>> ScriptBuildSystem::BuildPak(eastl::string_view path) {
    das::TextPrinter tout;
    das::ModuleGroup group;

    const das::CodeOfPolicies cop = CreatePakPolicies();

    auto build_file_access = MakeFsFileAccess(config_.project_file.c_str());

    const das::string entry(path.data(), path.size());
    const auto program = das::compileDaScript(entry, build_file_access, tout, group, cop);

    if (!program)
        return LogAndMakeError("BuildPak: ошибка компиляции {}", entry.c_str());
    if (program->failed()) {
        for (const auto& error : program->errors)
            LogError(das::reportError(error.at, error.what, error.extra, error.fixme, error.cerr).c_str());
        return LogAndMakeError("BuildPak: ошибка компиляции {}", entry.c_str());
    }

    for (auto* mod : program->thisModuleGroup->getModules())
        LogInfo(LogCategory::Script, "pak module: {} [{}]", mod->name.c_str(), mod->fileName.c_str());
    eastl::vector<das::string> extra_files;
    {
        const das::string emitter = das::getDasRoot() + "/modules/dasLLVM/daslib/llvm_macro.das";
        das::TextPrinter etout;
        das::ModuleGroup emitter_group;
        auto emitter_prog = das::compileDaScript(emitter, build_file_access, etout, emitter_group, cop);
        if (!emitter_prog || emitter_prog->failed()) {
            for (const auto& error : emitter_prog->errors)
                LogError(das::reportError(error.at, error.what, error.extra, error.fixme, error.cerr).c_str());
            return LogAndMakeError("BuildPak: не удалось скомпилировать llvm_macro");
        }
        extra_files.push_back(emitter);
        for (auto* mod : emitter_group.getModules())
            if (!mod->fileName.empty())
                extra_files.push_back(mod->fileName);
    }

    auto pak = CollectScriptPak(*build_file_access, *program->thisModuleGroup, entry, extra_files);
    if (!pak.has_value())
        return LogAndMakeError("BuildPak: не удалось собрать файлы");

    return SerializeScriptPak(*pak);
}

}  // namespace tryengine::core