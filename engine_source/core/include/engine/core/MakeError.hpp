#pragma once

#include "FormatUtils.h"
#include "Error.hpp"
#include "Log.hpp"

namespace tryengine::core {

template <class... Args>
[[nodiscard]]
Error LogAndMakeError(ErrorCode code, LocFmt<std::type_identity_t<Args>...> locFmt, Args&&... args) {
    Error err(code, fmt::format(locFmt.fmt, std::forward<Args>(args)...));

    // Локация — точка вызова MakeError (её поймал LocFmt), а не точка
    // внутри этой функции.
    detail::log_dispatch(LogLevel::Error, TRY_LOG_DEFAULT_CATEGORY_Q, locFmt.loc, err.Message());

    return err;
}

template <class... Args>
[[nodiscard]]
Error LogAndMakeError(LocFmt<std::type_identity_t<Args>...> locFmt, Args&&... args) {
    return LogAndMakeError(ErrorCode::Unknown, locFmt, std::forward<Args>(args)...);
}

}  // namespace tryengine::core

using tryengine::core::LogAndMakeError;