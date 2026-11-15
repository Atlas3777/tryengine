#include <daScript/daScript.h>
#include <daScript/simulate/aot.h>

#include "engine/core/TryengineContext.hpp"
#include "engine/resources/AsyncFileManager.hpp"

using namespace tryengine::core;
using namespace tryengine::resources;

// Биндинг enum TaskStatus — ДО using namespace das (иначе коллизии имён)
DAS_BASE_BIND_ENUM(tryengine::resources::TaskStatus, TaskStatus, Unused, Pending, Completed, Failed)

using namespace das;

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

namespace {

// Менеджер асинхронного ввода-вывода живёт в движке; скрипт передаёт только путь.
AsyncFileManager* GetAsyncFileManager(Context* ctx) {
    auto* try_ctx = static_cast<TryengineContext*>(ctx);
    auto* manager = try_ctx ? try_ctx->engine.TryGet<AsyncFileManager>() : nullptr;
    if (!manager) {
        LogError("AsyncFileManager not found in TryengineContext");
    }
    return manager;
}

}  // namespace

FileHandle ReadChunkAsync(const char* path, uint64_t offset, uint32_t size, Context* ctx) {
    auto* manager = GetAsyncFileManager(ctx);
    return manager ? manager->ReadChunkAsyncCopy(path, offset, size) : FileHandle{};
}

FileHandle WriteChunkAsync(const char* path, const das::TArray<uint8_t>& data, uint64_t offset, Context* ctx) {
    auto* manager = GetAsyncFileManager(ctx);
    if (!manager)
        return FileHandle{};
    eastl::span span_data(reinterpret_cast<const uint8_t*>(data.data), data.size);
    return manager->WriteChunkAsyncCopy(path, span_data, offset);
}

FileHandle GetStatAsync(const char* path, Context* ctx) {
    auto* manager = GetAsyncFileManager(ctx);
    return manager ? manager->GetStatAsyncCopy(path) : FileHandle{};
}

das::Array GetDataSpan(tryengine::resources::FileHandle& self) {
    das::Array arr{};

    auto* data = self.GetData().data();

    arr.data = (char*) (data);
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
        addExtern<DAS_BIND_FUN(finalize_file_handle)>(*this, lib, "finalize_file_handle", SideEffects::modifyArgument,
                                                      "finalize_file_handle");

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
