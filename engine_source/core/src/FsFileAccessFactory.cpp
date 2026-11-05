#include "engine/core/FsFileAccessFactory.hpp"

#include "engine/core/MakeError.hpp"

namespace tryengine::core {

das::smart_ptr<das::FsFileAccess> MakeFsFileAccess(const char* project_file, const das::CodeOfPolicies& policies) {
    if (project_file && *project_file) {
        das::TextPrinter tout;
        das::ModuleGroup group;

        auto base_access = das::make_smart<das::FsFileAccess>();

        auto project_program = das::compileDaScript(project_file, base_access, tout, group, policies);

        if (project_program && !project_program->failed())
            return das::make_smart<das::FsFileAccess>(project_file, project_program);

        LogWarn(LogCategory::Script, "Не удалось загрузить '{}'. Используется стандартный FsFileAccess.",
                project_file);
    }
    return das::make_smart<das::FsFileAccess>();
}

}  // namespace tryengine::core