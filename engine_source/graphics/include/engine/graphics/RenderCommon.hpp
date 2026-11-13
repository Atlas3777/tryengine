#pragma once

#include <daScript/misc/vectypes.h>
#include <daScript/simulate/runtime_matrices.h>

#include "RuntimeTypes.hpp"

namespace tryengine::graphics {

struct CameraData {
    das::float4x4 view;
    das::float4x4 proj;
    das::float3 position;
};

struct alignas(16) CameraGPU {
    das::float4x4 view;
    das::float4x4 proj;
};

struct AmbientSettings {
    das::float4 ambient_color{0.05f, 0.05f, 0.08f, 1.0f};
    das::float4 clear_color{0.1f, 0.1f, 0.12f, 1.0f};
};

struct GlobalLight {
    das::float4 ambient_color;
    das::float4 view_pos;
};

}  // namespace tryengine::graphics
