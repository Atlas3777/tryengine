#include <daScript/daScript.h>
#include <daScript/simulate/aot.h>

#include "engine/core/TryengineContext.hpp"
#include "engine/platform/InputService.hpp"
#include "engine/platform/InputState.hpp"

using namespace tryengine::core;
using namespace tryengine::platform;

// Биндинг enum'ов — ДО using namespace das (иначе коллизии имён)
DAS_BASE_BIND_ENUM(Key, Key, Unknown, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
                   Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9, Num0, Return, Escape, Backspace, Tab, Space,
                   F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, Right, Left, Down, Up, LCtrl, LShift, LAlt, LGui,
                   RCtrl, RShift, RAlt, RGui, Count)

DAS_BASE_BIND_ENUM(Mouse, Mouse, Left, Middle, Right, X1, X2, Count)

using namespace das;

MAKE_TYPE_FACTORY(InputState, InputState)

struct InputStateAnnotation : ManagedStructureAnnotation<InputState, false /*canNew*/, false /*canDelete*/> {
    InputStateAnnotation(ModuleLibrary& ml) : ManagedStructureAnnotation("InputState", ml) {
        addField<DAS_BIND_MANAGED_FIELD(is_down)>("isDown", "is_down");
        addField<DAS_BIND_MANAGED_FIELD(just_pressed)>("justPressed", "just_pressed");
        addField<DAS_BIND_MANAGED_FIELD(just_released)>("justReleased", "just_released");

        addField<DAS_BIND_MANAGED_FIELD(mouse_x)>("mouseX", "mouse_x");
        addField<DAS_BIND_MANAGED_FIELD(mouse_y)>("mouseY", "mouse_y");
        addField<DAS_BIND_MANAGED_FIELD(mouse_delta_x)>("mouseDeltaX", "mouse_delta_x");
        addField<DAS_BIND_MANAGED_FIELD(mouse_delta_y)>("mouseDeltaY", "mouse_delta_y");

        addField<DAS_BIND_MANAGED_FIELD(mouse_buttons)>("mouseButtons", "mouse_buttons");
        addField<DAS_BIND_MANAGED_FIELD(mouse_just_pressed)>("mouseJustPressed", "mouse_just_pressed");
        addField<DAS_BIND_MANAGED_FIELD(mouse_just_released)>("mouseJustReleased", "mouse_just_released");
    }
};

void request_quit(das::Context* ctx) {
    auto* try_ctx = static_cast<TryengineContext*>(ctx);
    if (auto* input = try_ctx->engine.TryGet<InputService>())
        input->RequestQuit();
}

InputState* get_input_state(das::Context* ctx) {
    auto* try_ctx = static_cast<TryengineContext*>(ctx);
    auto* input = try_ctx->engine.TryGet<InputService>();
    if (!input) {
        LogError("InputState not found");
        return nullptr;
    }
    return &input->GetState();
}

class Module_Input : public Module {
public:
    Module_Input() : Module("tryInput") {
        ModuleLibrary lib(this);
        lib.addBuiltInModule();

        addAnnotation(new InputStateAnnotation(lib));

        addEnumeration(new EnumerationKey());
        addEnumeration(new EnumerationMouse());

        addExtern<DAS_BIND_FUN(get_input_state)>(*this, lib, "get_input_state", SideEffects::accessGlobal,
                                                 "get_input_state");

        addExtern<DAS_BIND_FUN(request_quit)>(*this, lib, "RequestQuit", SideEffects::accessGlobal, "request_quit");

        verifyAotReady();
    }
};

REGISTER_DYN_MODULE(Module_Input, Module_Input);
REGISTER_MODULE(Module_Input);
