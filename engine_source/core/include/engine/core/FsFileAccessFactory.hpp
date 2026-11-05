#pragma once

#include <daScript/daScript.h>

namespace tryengine::core {

// Создаёт FsFileAccess. Если project_file задан и успешно компилируется,
// используется он, иначе откат на обычный FsFileAccess.
// Project-файл компилируется с переданными policies.
das::smart_ptr<das::FsFileAccess> MakeFsFileAccess(const char* project_file,
                                                   const das::CodeOfPolicies& policies = das::CodeOfPolicies());

}  // namespace tryengine::core