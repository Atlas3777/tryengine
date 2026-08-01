#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include "UiFile.hpp"

namespace tryeditor {

void LoadResourcesAndBuildUi(eastl::string_view root_dir, eastl::vector<eastl::string>& out_assets,
                             eastl::vector<eastl::string>& out_metas, eastl::vector<eastl::string>& orphan_assets,
                             eastl::vector<eastl::string>& orphan_metas, eastl::vector<UiFolder>& out_folders);
}  // namespace tryeditor