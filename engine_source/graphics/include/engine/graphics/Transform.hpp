#pragma once

#include <daScript/misc/vectypes.h>

namespace tryengine {

// Локальный трансформ ноды в терминах daslang: rotation — кватернион (x, y, z, w).
struct Transform {
    das::float3 position = das::float3(0.0f, 0.0f, 0.0f);
    das::float4 rotation = das::float4(0.0f, 0.0f, 0.0f, 1.0f);
    das::float3 scale = das::float3(1.0f, 1.0f, 1.0f);
};

}  // namespace tryengine
