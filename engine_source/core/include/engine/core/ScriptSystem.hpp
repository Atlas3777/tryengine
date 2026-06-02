#pragma once

#include <string>

namespace das {
class Context;
class SimFunction;
}

namespace tryengine::core {

class ScriptSystem {
public:
    ScriptSystem();
    ~ScriptSystem();

    bool LoadMainScript(const std::string& path);
    void InvokeStart();
    void InvokeUpdate(float dt);

    das::Context* GetContext();

private:
    das::Context* das_ctx = nullptr;
    das::SimFunction* fn_start = nullptr;
    das::SimFunction* fn_update = nullptr;
};

}  // namespace tryengine::core