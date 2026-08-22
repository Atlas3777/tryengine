#pragma once

#include <EASTL/optional.h>
#include <EASTL/string.h>

#include "engine/core/FormatUtils.h"

namespace tryeditor {

struct EditorArtifactLocator {
    eastl::string artifacts_dir;  // "game/artifacts" или "engine_content/artifacts"
    uint32_t slot = 0;            // "may" — фиксированный вручную, не автоинкрементный type_id

    eastl::optional<eastl::string> operator()(uint64_t guid) const {
        return tryengine::fmt::format("{}/editor/{}/{}", artifacts_dir.c_str(), guid, slot);
    }
};
}  // namespace tryeditor