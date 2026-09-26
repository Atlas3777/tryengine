#pragma once

#include <hlsl++.h>

#include "RuntimeTypes.hpp"

namespace tryengine::graphics {

struct CameraData {
    hlslpp::float4x4 view;
    hlslpp::float4x4 proj;
    hlslpp::float3 position;
};

struct AmbientSettings {
    hlslpp::float4 ambient_color{0.05f, 0.05f, 0.08f, 1.0f};
    hlslpp::float4 clear_color{0.1f, 0.1f, 0.12f, 1.0f};
};

struct GlobalLight {
    hlslpp::float4 ambient_color;
    hlslpp::float4 view_pos;
};

}  // namespace tryengine::graphics