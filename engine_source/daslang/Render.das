options gen2

require tryengine/Types
require tryRenderer
require daslib/decs

[export]
def GatherLights(var outVec : PointLightGPUVector) {
    var arr : array<PointLightGPU>

    arr.push(make_point_light(float4(1, 1, 1, 20), float4(0.5, 0.5, 0.5, 1)))


    // единственный переход в C++ за весь вызов
    copy_lights(arr, outVec)
}