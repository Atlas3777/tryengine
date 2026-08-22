#pragma once

#include <hlsl++/matrix_float.h>
#include <hlsl++/quaternion.h>

namespace tryengine {

struct Transform {
    hlslpp::float3 position = hlslpp::float3(0, 0, 0);
    hlslpp::quaternion rotation = hlslpp::quaternion::identity();
    hlslpp::float3 scale = hlslpp::float3(1, 1, 1);

    hlslpp::float4x4 GetLocalMatrix() const {
        return hlslpp::float4x4::translation(position) * hlslpp::float4x4(rotation) * hlslpp::float4x4::scale(scale);
    }
};

}  // namespace tryengine