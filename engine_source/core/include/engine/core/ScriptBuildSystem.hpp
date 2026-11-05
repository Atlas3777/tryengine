#pragma once

#include <EASTL/functional.h>
#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <daScript/daScript.h>

#include "engine/core/GameScriptFileAccess.hpp"
#include "engine/core/Result.hpp"

namespace tryengine::core {

// Конфиг сборщика: никаких register_modules / file_access / policies.
// Модули уже зарегистрированы ScriptSystem-ом редактора.
struct ScriptBuildConfig {
    eastl::string project_file;
    eastl::function<das::Context*(uint32_t stack_size)> create_context;
};

class ScriptBuildSystem {
public:
    explicit ScriptBuildSystem(ScriptBuildConfig config);
    ~ScriptBuildSystem();

    ScriptBuildSystem(const ScriptBuildSystem&) = delete;
    ScriptBuildSystem& operator=(const ScriptBuildSystem&) = delete;

    Result<void> TestCompile(eastl::span<const uint8_t> data);

    Result<eastl::vector<uint8_t>> BuildPak(eastl::string_view path);

    das::Context* GetContext() const { return das_ctx_; }

private:
    ScriptBuildConfig config_;
    das::Context* das_ctx_ = nullptr;
    eastl::string pak_entry_;
};

}  // namespace tryengine::core