// engine_source/core/include/engine/core/Expected.hpp
#pragma once

#include <expected>

#include "engine/core/Error.hpp"

namespace tryengine {
template <typename T>
using Result = std::expected<T, core::Error>;

}  // namespace tryengine