#include <daScript/daScript.h>
#include <daScript/simulate/aot.h>

#include "engine/platform/InputService.hpp"
#include "engine/platform/InputState.hpp"
#include "engine/core/TryengineContext.hpp"
#include "engine/graphics/RuntimeTypes.hpp"
#include "engine/resources/AsyncFileManager.hpp"
#include "engine/resources/ResourceManager.hpp"

using namespace tryengine::core;
using namespace tryengine::platform;

// Биндинг enum'ов — ДО using namespace das (иначе коллизии имён)
DAS_BASE_BIND_ENUM(Key, Key, Unknown, A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
                   Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9, Num0, Return, Escape, Backspace, Tab, Space,
                   F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, Right, Left, Down, Up, LCtrl, LShift, LAlt, LGui,
                   RCtrl, RShift, RAlt, RGui, Count)

DAS_BASE_BIND_ENUM(Mouse, Mouse, Left, Middle, Right, X1, X2, Count)

// Биндинг enum TaskStatus
DAS_BASE_BIND_ENUM(tryengine::resources::TaskStatus, TaskStatus, Unused, Pending, Completed, Failed)

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

InputState* get_input_state(das::Context* ctx) {
    auto* try_ctx = static_cast<TryengineContext*>(ctx);
    auto* input = try_ctx->engine.TryGet<InputService>();
    if (!input) {
        LogError("InputState not found");
        return nullptr;
    }
    LogInfo("InputState found!");
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

        verifyAotReady();
    }
};

REGISTER_DYN_MODULE(Module_Input, Module_Input);
REGISTER_MODULE(Module_Input);

void RequestLoadMesh(uint64_t guid, das::Context* ctx) {
    tryengine::core::TryengineContext* try_ctx = static_cast<tryengine::core::TryengineContext*>(ctx);
    auto* res_manager = try_ctx->engine.TryGet<tryengine::resources::ResourceManager>();

    if (!res_manager)
        LogError("Resource Manager not found");

    res_manager->RequestLoad<tryengine::graphics::Mesh>(guid);
}

void RequestLoadMaterial(uint64_t guid, das::Context* ctx) {
    tryengine::core::TryengineContext* try_ctx = static_cast<tryengine::core::TryengineContext*>(ctx);
    auto* res_manager = try_ctx->engine.TryGet<tryengine::resources::ResourceManager>();

    if (!res_manager)
        LogError("Resource Manager not found");

    res_manager->RequestLoad<tryengine::graphics::Material>(guid);
}

bool ResourceIsReadyMesh(uint64_t guid, das::Context* ctx) {
    tryengine::core::TryengineContext* try_ctx = static_cast<tryengine::core::TryengineContext*>(ctx);
    auto* res_manager = try_ctx->engine.TryGet<tryengine::resources::ResourceManager>();

    if (!res_manager)
        LogError("Resource Manager not found");

    return res_manager->IsReady<tryengine::graphics::Mesh>(guid);
}

bool ResourceIsReadyMaterial(uint64_t guid, das::Context* ctx) {
    tryengine::core::TryengineContext* try_ctx = static_cast<tryengine::core::TryengineContext*>(ctx);
    auto* res_manager = try_ctx->engine.TryGet<tryengine::resources::ResourceManager>();

    if (!res_manager)
        LogError("Resource Manager not found");

    return res_manager->IsReady<tryengine::graphics::Material>(guid);
}

uint64_t GetMeshPointer(uint64_t guid, das::Context* ctx) {
    tryengine::core::TryengineContext* try_ctx = static_cast<tryengine::core::TryengineContext*>(ctx);
    auto* res_manager = try_ctx->engine.TryGet<tryengine::resources::ResourceManager>();

    if (!res_manager)
        LogError("Resource Manager not found");

    return res_manager->GetPointer<tryengine::graphics::Mesh>(guid);
}

uint64_t GetMaterialPointer(uint64_t guid, das::Context* ctx) {
    tryengine::core::TryengineContext* try_ctx = static_cast<tryengine::core::TryengineContext*>(ctx);
    auto* res_manager = try_ctx->engine.TryGet<tryengine::resources::ResourceManager>();

    if (!res_manager)
        LogError("Resource Manager not found");

    return res_manager->GetPointer<tryengine::graphics::Material>(guid);
}

void ReleaseMeshResource(uint64_t guid, das::Context* ctx) {
    tryengine::core::TryengineContext* try_ctx = static_cast<tryengine::core::TryengineContext*>(ctx);
    auto* res_manager = try_ctx->engine.TryGet<tryengine::resources::ResourceManager>();

    if (!res_manager)
        LogError("Resource Manager not found");

    res_manager->Release<tryengine::graphics::Mesh>(guid);
}

void ReleaseMaterialResource(uint64_t guid, das::Context* ctx) {
    tryengine::core::TryengineContext* try_ctx = static_cast<tryengine::core::TryengineContext*>(ctx);
    auto* res_manager = try_ctx->engine.TryGet<tryengine::resources::ResourceManager>();

    if (!res_manager)
        LogError("Resource Manager not found");

    res_manager->Release<tryengine::graphics::Material>(guid);
}

class Module_Resources : public das::Module {
public:
    Module_Resources() : Module("tryResources") {
        das::ModuleLibrary lib(this);
        lib.addBuiltInModule();


        das::addExtern<DAS_BIND_FUN(RequestLoadMaterial)>(*this, lib, "RequestLoadMaterial",
                                                          das::SideEffects::accessGlobal, "RequestLoadMaterial");

        das::addExtern<DAS_BIND_FUN(RequestLoadMesh)>(*this, lib, "RequestLoadMesh",
                                                  das::SideEffects::accessGlobal, "RequestLoadMesh");

        das::addExtern<DAS_BIND_FUN(ResourceIsReadyMaterial)>(*this, lib, "ResourceIsReadyMaterial",
                                          das::SideEffects::accessGlobal, "ResourceIsReadyMaterial");

        das::addExtern<DAS_BIND_FUN(ResourceIsReadyMesh)>(*this, lib, "ResourceIsReadyMesh",
                                  das::SideEffects::accessGlobal, "ResourceIsReadyMesh");

        das::addExtern<DAS_BIND_FUN(ReleaseMaterialResource)>(*this, lib, "ReleaseMaterialResource",
                          das::SideEffects::accessGlobal, "ReleaseMaterialResource");

        das::addExtern<DAS_BIND_FUN(ReleaseMeshResource)>(*this, lib, "ReleaseMeshResource",
                  das::SideEffects::accessGlobal, "ReleaseMeshResource");

        das::addExtern<DAS_BIND_FUN(GetMeshPointer)>(*this, lib, "GetMeshPointer",
                                                          das::SideEffects::accessGlobal, "GetMeshPointer");

        das::addExtern<DAS_BIND_FUN(GetMaterialPointer)>(
            *this, lib, "GetMaterialPointer", das::SideEffects::accessGlobal, "GetMaterialPointer");

        verifyAotReady();
    }  // das::SimNode_ExtFuncCallAndCopyOrMove
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

using namespace tryengine::resources;

MAKE_TYPE_FACTORY(FileHandle, FileHandle)

MAKE_TYPE_FACTORY(FileTime, FileTime)

struct FileTimeAnnotation : ManagedStructureAnnotation<FileTime, true, true> {
    FileTimeAnnotation(ModuleLibrary& ml) : ManagedStructureAnnotation("FileTime", ml) {
        addField<DAS_BIND_MANAGED_FIELD(sec)>("sec", "sec");
        addField<DAS_BIND_MANAGED_FIELD(nsec)>("nsec", "nsec");
    }
};

struct FileHandleAnnotation : ManagedStructureAnnotation<FileHandle, false, true> {
    FileHandleAnnotation(ModuleLibrary& ml) : ManagedStructureAnnotation("FileHandle", ml) {
        addProperty<DAS_BIND_MANAGED_PROP(IsReady)>("IsReady", "IsReady");
        addProperty<DAS_BIND_MANAGED_PROP(IsFailed)>("IsFailed", "IsFailed");
        addProperty<DAS_BIND_MANAGED_PROP(IsPending)>("IsPending", "IsPending");
        addProperty<DAS_BIND_MANAGED_PROP(GetFileSize)>("GetFileSize", "GetFileSize");
    }


    bool isLocal() const override { return true; }
    bool canMove() const override { return true; }
    bool canCopy() const override { return false; }
    bool hasNonTrivialDtor() const override { return true; }
};

// --- Функции ввода-вывода, извлекающие AsyncFileManager из TryengineContext ---

FileHandle ReadChunkAsync(const char* path, uint64_t offset, uint32_t size, Context* ctx) {
    auto* try_ctx = static_cast<TryengineContext*>(ctx);
    auto* manager = try_ctx ? try_ctx->engine.TryGet<AsyncFileManager>() : nullptr;
    if (!manager) {
        LogError("AsyncFileManager not found in TryengineContext");
        return FileHandle{};
    }
    return manager->ReadChunkAsyncCopy(path, offset, size);
}

FileHandle WriteChunkAsync(const char* path, const das::TArray<uint8_t>& data, uint64_t offset, Context* ctx) {
    auto* try_ctx = static_cast<TryengineContext*>(ctx);
    auto* manager = try_ctx ? try_ctx->engine.TryGet<AsyncFileManager>() : nullptr;
    if (!manager) {
        LogError("AsyncFileManager not found in TryengineContext");
        return FileHandle{};
    }
    eastl::span span_data(reinterpret_cast<const uint8_t*>(data.data), data.size);
    return manager->WriteChunkAsyncCopy(path, span_data, offset);
}

FileHandle GetStatAsync(const char* path, Context* ctx) {
    auto* try_ctx = static_cast<TryengineContext*>(ctx);
    auto* manager = try_ctx ? try_ctx->engine.TryGet<AsyncFileManager>() : nullptr;
    if (!manager) {
        LogError("AsyncFileManager not found in TryengineContext");
        return FileHandle{};
    }
    return manager->GetStatAsyncCopy(path);
}

das::Array GetDataSpan(tryengine::resources::FileHandle& self) {
    das::Array arr{};

    auto* data = self.GetData().data();

    arr.data = (char*)(data);
    arr.size = static_cast<uint32_t>(self.GetData().size());
    arr.capacity = static_cast<uint32_t>(self.GetData().size());
    return arr;
}

void finalize_file_handle(FileHandle& handle) {
    handle.Release();
}

class Module_AsyncFileManager : public das::Module {
public:
    Module_AsyncFileManager() : Module("tryAsyncFile") {
        das::ModuleLibrary lib(this);
        lib.addBuiltInModule();

        // Аннотации и enum
        addAnnotation(new FileTimeAnnotation(lib));
        addAnnotation(new FileHandleAnnotation(lib));
        addEnumeration(new EnumerationTaskStatus());

        // Вспомогательные функции
        addExtern<DAS_BIND_FUN(finalize_file_handle)>(
            *this, lib, "finalize_file_handle", SideEffects::modifyArgument, "finalize_file_handle");

        addExtern<DAS_BIND_FUN(ReadChunkAsync), das::SimNode_ExtFuncCallAndCopyOrMove>(
            *this, lib, "ReadChunkAsync", SideEffects::accessGlobal, "ReadChunkAsync");

        addExtern<DAS_BIND_FUN(WriteChunkAsync), das::SimNode_ExtFuncCallAndCopyOrMove>(
            *this, lib, "WriteChunkAsync", SideEffects::accessGlobal, "WriteChunkAsync");

        addExtern<DAS_BIND_FUN(GetStatAsync), das::SimNode_ExtFuncCallAndCopyOrMove>(
            *this, lib, "GetStatAsync", SideEffects::accessGlobal, "GetStatAsync");

        addExtern<DAS_BIND_FUN(GetDataSpan), das::SimNode_ExtFuncCallAndCopyOrMove>(
            *this, lib, "GetDataSpan", das::SideEffects::none, "GetDataSpan");

        verifyAotReady();
    }
};

REGISTER_DYN_MODULE(Module_AsyncFileManager, Module_AsyncFileManager);
REGISTER_MODULE(Module_AsyncFileManager);