#include <cstring>
#include <daScript/daScript.h>
#include <daScript/simulate/aot.h>
#include <daScript/simulate/cast.h>
#include <glm/glm.hpp>

#include "engine/graphics/RenderCommon.hpp"

namespace das {

//---------------------------------------------------------
// glm::vec4 <-> float4
//---------------------------------------------------------

template <>
struct typeFactory<glm::vec4> {
    static __forceinline TypeDeclPtr make(const ModuleLibrary& lib) { return typeFactory<float4>::make(lib); }
};

template <>
struct cast<glm::vec4> {
    static __forceinline glm::vec4 to(vec4f value) {
        static_assert(sizeof(glm::vec4) == sizeof(vec4f));
        return prune<glm::vec4, vec4f>::from(value);
    }

    static __forceinline vec4f from(const glm::vec4& value) {
        static_assert(sizeof(glm::vec4) == sizeof(vec4f));
        return prune<vec4f, glm::vec4>::from(value);
    }
};

template <>
struct cast<const glm::vec4> : cast<glm::vec4> {};

}  // namespace das

//---------------------------------------------------------
// C++ API
//---------------------------------------------------------

inline tryengine::graphics::PointLightGPU make_point_light_gpu(glm::vec4 position_radius, glm::vec4 color_intensity) {
    return {position_radius, color_intensity};
}

using PointLightGPUVector = eastl::vector<tryengine::graphics::PointLightGPU>;

MAKE_TYPE_FACTORY(PointLightGPU, tryengine::graphics::PointLightGPU);
MAKE_TYPE_FACTORY(PointLightGPUVector, PointLightGPUVector);

using namespace das;

//---------------------------------------------------------
// Annotation
//---------------------------------------------------------

struct PointLightGPUAnnotation : ManagedStructureAnnotation<tryengine::graphics::PointLightGPU, false> {
    PointLightGPUAnnotation(ModuleLibrary& ml) : ManagedStructureAnnotation("PointLightGPU", ml) {
        addField<DAS_BIND_MANAGED_FIELD(position_radius)>("position_radius", "position_radius");
        addField<DAS_BIND_MANAGED_FIELD(color_intensity)>("color_intensity", "color_intensity");
    }
};

//---------------------------------------------------------
// Bulk copy: array<PointLightGPU> (daslang) -> std::vector<PointLightGPU> (C++)
//---------------------------------------------------------

vec4f copy_lights_interop(Context & ctx, SimNode_CallBase * call, vec4f * args) {
    Array * src = cast<Array *>::to(args[0]);            // array<PointLightGPU> -- any
    PointLightGPUVector * dst = cast<PointLightGPUVector *>::to(args[1]); // конкретный ref-тип

    dst->resize(src->size);
    if (src->size > 0) {
        memcpy(dst->data(), src->data,
               size_t(src->size) * sizeof(tryengine::graphics::PointLightGPU));
    }

    return v_zero();
}

//---------------------------------------------------------
// Module
//---------------------------------------------------------

class Module_Renderer : public Module {
public:
    Module_Renderer() : Module("tryRenderer") {
        ModuleLibrary lib(this);
        lib.addBuiltInModule();

        addAnnotation(new PointLightGPUAnnotation(lib));
        addVectorAnnotation<PointLightGPUVector>(this, lib, "PointLightGPUVector");

        addExtern<DAS_BIND_FUN(make_point_light_gpu), SimNode_ExtFuncCallAndCopyOrMove>(
            *this, lib, "make_point_light", SideEffects::none, "make_point_light_gpu")
            ->args({"position_radius", "color_intensity"});

        // arg0 = vec4f ("any" array<T> — берём array<PointLightGPU>)
        // arg1 = PointLightGPUVector& — конкретный mutable ref, отсюда и modifyArgument проходит валидацию
        addInterop<copy_lights_interop, void, vec4f, PointLightGPUVector &>(
            *this, lib, "copy_lights",
            SideEffects::modifyArgument, "copy_lights_interop")
            ->args({"arr", "outVec"});
    }
};

REGISTER_DYN_MODULE(Module_Renderer, Module_Renderer);
REGISTER_MODULE(Module_Renderer);