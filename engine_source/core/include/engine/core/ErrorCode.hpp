#pragma once

#include <EASTL/string_view.h>

namespace tryengine::core
{

enum class ErrorCode
{
    Unknown,

    FileNotFound,
    AccessDenied,
    InvalidPath,
    InvalidData,

    SerializationFailed,
    DeserializationFailed,

    OperationCancelled,

    EngineInternalError
};

[[nodiscard]]
constexpr eastl::string_view ToString(ErrorCode code) noexcept
{
    using enum ErrorCode;

    switch (code)
    {
        case FileNotFound:
            return "File Not Found";

        case AccessDenied:
            return "Access Denied";

        case InvalidPath:
            return "Invalid Path";

        case InvalidData:
            return "Invalid Data";

        case SerializationFailed:
            return "Serialization Failed";

        case DeserializationFailed:
            return "Deserialization Failed";

        case OperationCancelled:
            return "Operation Cancelled";

        case EngineInternalError:
            return "Engine Internal Error";

        default:
            return "Unknown Error";
    }
}

} // namespace tryengine::core