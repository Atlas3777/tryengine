#include <daScript/daScript.h>
#include <daScript/simulate/aot.h>

#include "engine/core/ScriptSystem.hpp"
#include "engine/graphics/RenderAdapter.hpp"
#include "engine/graphics/RenderCommon.hpp"
#include "engine/graphics/RuntimeTypes.hpp"
#include "engine/resources/AsyncFileManager.hpp"
#include "engine/resources/ResourceManager.hpp"

#include "engine/core/InputService.hpp"
#include "engine/core/InputState.hpp"

using namespace tryengine::core;

// Биндинг enum'ов — ДО using namespace das (иначе коллизии имён)
DAS_BASE_BIND_ENUM(Key, Key,
    Unknown,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9, Num0,
    Return, Escape, Backspace, Tab, Space,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Right, Left, Down, Up,
    LCtrl, LShift, LAlt, LGui, RCtrl, RShift, RAlt, RGui,
    Count
)

DAS_BASE_BIND_ENUM(Mouse, Mouse,
    Left, Middle, Right, X1, X2, Count
)

using namespace das;

MAKE_TYPE_FACTORY(InputState, InputState)

struct InputStateAnnotation
    : ManagedStructureAnnotation<InputState, false /*canNew*/, false /*canDelete*/>
{
    InputStateAnnotation(ModuleLibrary & ml)
        : ManagedStructureAnnotation("InputState", ml)
    {
        // Только сырые данные. IsDown/Pressed/Released НЕ биндим —
        // они реализуются прямо в daslang через индексацию по массиву.
        addField<DAS_BIND_MANAGED_FIELD(isDown)>("isDown", "isDown");
        addField<DAS_BIND_MANAGED_FIELD(justPressed)>("justPressed", "justPressed");
        addField<DAS_BIND_MANAGED_FIELD(justReleased)>("justReleased", "justReleased");

        addField<DAS_BIND_MANAGED_FIELD(mouseX)>("mouseX", "mouseX");
        addField<DAS_BIND_MANAGED_FIELD(mouseY)>("mouseY", "mouseY");
        addField<DAS_BIND_MANAGED_FIELD(mouseDeltaX)>("mouseDeltaX", "mouseDeltaX");
        addField<DAS_BIND_MANAGED_FIELD(mouseDeltaY)>("mouseDeltaY", "mouseDeltaY");

        addField<DAS_BIND_MANAGED_FIELD(mouseButtons)>("mouseButtons", "mouseButtons");
        addField<DAS_BIND_MANAGED_FIELD(mouseJustPressed)>("mouseJustPressed", "mouseJustPressed");
        addField<DAS_BIND_MANAGED_FIELD(mouseJustReleased)>("mouseJustReleased", "mouseJustReleased");
    }
};

InputState* get_input_state(Context * ctx) {
    auto * try_ctx = static_cast<tryengine::core::TryengineContext *>(ctx);
    auto * input = try_ctx->engine.TryGet<tryengine::core::InputService>();
    if (!input) {
        LogError("InputState not found");
        return nullptr;
    }
    LogInfo("InputState found!");
    return &input->GetInputState();
}

class Module_Input : public Module {
public:
    Module_Input() : Module("tryInput") {
        ModuleLibrary lib(this);
        lib.addBuiltInModule();

        addAnnotation(new InputStateAnnotation(lib));

        addEnumeration(new EnumerationKey());
        addEnumeration(new EnumerationMouse());

        addExtern<DAS_BIND_FUN(get_input_state)>(
            *this, lib, "get_input_state",
            SideEffects::accessGlobal, "get_input_state");

        verifyAotReady();
    }
};

REGISTER_DYN_MODULE(Module_Input, Module_Input);
REGISTER_MODULE(Module_Input);


uint64_t GetSlotMapValueMesh(uint64_t guid, das::Context* ctx) {
    tryengine::core::TryengineContext* try_ctx = static_cast<tryengine::core::TryengineContext*>(ctx);
    auto* res_manager = try_ctx->engine.TryGet<tryengine::resources::ResourceManager>();

    if (!res_manager)
        LogError("Resource Manager not found");

    if (auto* render_adapter = try_ctx->engine.TryGet<tryengine::graphics::RenderAdapter>()) {
        auto resource_handle = res_manager->Get<tryengine::graphics::Mesh>(guid);

        if (!resource_handle.has_value())
            LogError(resource_handle.error().Message());

        // emplace возвращает Key, который автоматически приводится к uint64_t!
        return render_adapter->slot_map_mesh.emplace(*resource_handle);
    }

    LogError("RenderAdapter not found");
    return 0;
}

uint64_t GetSlotMapValueMaterial(uint64_t guid, das::Context* ctx) {
    tryengine::core::TryengineContext* try_ctx = static_cast<tryengine::core::TryengineContext*>(ctx);
    auto* res_manager = try_ctx->engine.TryGet<tryengine::resources::ResourceManager>();

    if (!res_manager)
        LogError("Resource Manager not found");

    if (auto* render_adapter = try_ctx->engine.TryGet<tryengine::graphics::RenderAdapter>()) {
        auto resource_handle = res_manager->Get<tryengine::graphics::Material>(guid);

        if (!resource_handle.has_value())
            LogError(resource_handle.error().Message());

        // emplace сохраняет хэндл материала в slot_map_material
        return render_adapter->slot_map_material.emplace(*resource_handle);
    }

    LogError("RenderAdapter not found");
    return 0;
}

class Module_Resources : public das::Module {
public:
    Module_Resources() : Module("tryResources") {
        das::ModuleLibrary lib(this);
        lib.addBuiltInModule();

        das::addExtern<DAS_BIND_FUN(GetSlotMapValueMesh)>(*this, lib, "GetSlotMapValueMesh",
                                                       das::SideEffects::accessGlobal, "GetSlotMapValueMesh");

        das::addExtern<DAS_BIND_FUN(GetSlotMapValueMaterial)>(*this, lib, "GetSlotMapValueMaterial",
                                                       das::SideEffects::accessGlobal, "GetSlotMapValueMaterial");

        verifyAotReady();
    } // das::SimNode_ExtFuncCallAndCopyOrMove
};

REGISTER_DYN_MODULE(Module_Resources, Module_Resources);
REGISTER_MODULE(Module_Resources);

class Module_Renderer : public das::Module {
public:
    Module_Renderer() : Module("tryRenderer") {
        das::ModuleLibrary lib(this);
        lib.addBuiltInModule();
    }
};

REGISTER_DYN_MODULE(Module_Renderer, Module_Renderer);
REGISTER_MODULE(Module_Renderer);