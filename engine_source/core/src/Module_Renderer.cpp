#include <daScript/daScript.h>
#include <daScript/simulate/aot.h>
#include <vector>
#include "engine/core/SpawnPoint.hpp"

// Вспомогательная функция для вставки в вектор
inline void spawn_point_vector_push(std::vector<tryengine::core::SpawnPoint>& vec, const tryengine::core::SpawnPoint& value) {
    vec.push_back(value);
}

MAKE_TYPE_FACTORY(SpawnPoint, tryengine::core::SpawnPoint);
MAKE_TYPE_FACTORY(SpawnPointVector, std::vector<tryengine::core::SpawnPoint>);

using namespace das;

struct SpawnPointAnnotation : ManagedStructureAnnotation<tryengine::core::SpawnPoint, false> {
    SpawnPointAnnotation(ModuleLibrary& ml) : ManagedStructureAnnotation("SpawnPoint", ml) {
        addField<DAS_BIND_MANAGED_FIELD(id)>("id", "id");
        addField<DAS_BIND_MANAGED_FIELD(x)>("x", "x");
        addField<DAS_BIND_MANAGED_FIELD(y)>("y", "y");
    }
};

class Module_Renderer : public Module {
public:
    Module_Renderer() : Module("tryRenderer") {
        ModuleLibrary lib(this);
        lib.addBuiltInModule();

        addAnnotation(new SpawnPointAnnotation(lib));
        addAnnotation(new ManagedVectorAnnotation<std::vector<tryengine::core::SpawnPoint>>("SpawnPointVector", lib));

        addExtern<DAS_BIND_FUN(spawn_point_vector_push)>(
            *this, lib, "push", SideEffects::modifyArgument, "spawn_point_vector_push"
        )->args({"vec", "value"});

        addExtern<DAS_BIND_FUN(tryengine::core::make_spawn_point), SimNode_ExtFuncCallAndCopyOrMove>(
            *this, lib, "make_spawn_point", SideEffects::none, "tryengine::core::make_spawn_point")
            ->args({"id", "x", "y"});
    }
};

REGISTER_DYN_MODULE(Module_Renderer, Module_Renderer);
REGISTER_MODULE(Module_Renderer);