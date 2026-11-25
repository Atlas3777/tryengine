#include <cstring>
#include <daScript/daScript.h>

#include "engine/core/LiveStore.hpp"

namespace {

//! Ключ приходит из сгенерированного `@live`-хука; пустой ключ допустим только как
//! ошибка макроса, поэтому биндинг просто ничего не делает.
eastl::string_view KeyView(const char* key) {
    if (key == nullptr)
        return {};
    return eastl::string_view(key, std::strlen(key));
}

void LiveStoreBytes(const char* key, const das::TArray<uint8_t>& data) {
    if (key == nullptr)
        return;
    tryengine::core::StoreLiveBytes(KeyView(key), reinterpret_cast<const uint8_t*>(data.data),
                                    static_cast<size_t>(data.size));
}

bool LiveLoadBytes(const char* key, das::TArray<uint8_t>& data, das::Context* ctx) {
    if (key == nullptr || ctx == nullptr)
        return false;

    eastl::vector<uint8_t> bytes;
    if (!tryengine::core::LoadLiveBytes(KeyView(key), bytes))
        return false;

    das::array_resize(*ctx, data, static_cast<uint32_t>(bytes.size()), sizeof(uint8_t), false, nullptr);
    if (!bytes.empty())
        std::memcpy(data.data, bytes.data(), bytes.size());
    return true;
}

bool LiveIsReload() {
    return tryengine::core::IsLiveReload();
}

//! Контракт жизненного цикла перезагрузки, общий для любого хоста: имя функции становится
//! префиксом, по которому ScriptSystem находит хуки. Аннотации живут здесь, а не в редакторе:
//! хост подключает саму перезагрузку, но имена функций — часть движка.
struct BeforeReloadAnnotation final : das::FunctionAnnotation {
    BeforeReloadAnnotation() : das::FunctionAnnotation("before_reload") {}

    bool apply(const das::FunctionPtr& func, das::ModuleGroup&, das::AnnotationArgumentList&, das::string&) override {
        func->exports = true;
        func->name = "__before_reload_" + func->name;
        return true;
    }

    bool apply(das::ExprBlock*, das::ModuleGroup&, das::AnnotationArgumentList&, das::string& err) override {
        err = "not supported for blocks";
        return false;
    }

    bool finalize(const das::FunctionPtr&, das::ModuleGroup&, das::AnnotationArgumentList&,
                  const das::AnnotationArgumentList&, das::string&) override {
        return true;
    }

    bool finalize(das::ExprBlock*, das::ModuleGroup&, das::AnnotationArgumentList&, const das::AnnotationArgumentList&,
                  das::string&) override {
        return true;
    }
};

struct AfterReloadAnnotation final : das::FunctionAnnotation {
    AfterReloadAnnotation() : das::FunctionAnnotation("after_reload") {}

    bool apply(const das::FunctionPtr& func, das::ModuleGroup&, das::AnnotationArgumentList&, das::string&) override {
        func->exports = true;
        func->name = "__after_reload_" + func->name;
        return true;
    }

    bool apply(das::ExprBlock*, das::ModuleGroup&, das::AnnotationArgumentList&, das::string& err) override {
        err = "not supported for blocks";
        return false;
    }

    bool finalize(const das::FunctionPtr&, das::ModuleGroup&, das::AnnotationArgumentList&,
                  const das::AnnotationArgumentList&, das::string&) override {
        return true;
    }

    bool finalize(das::ExprBlock*, das::ModuleGroup&, das::AnnotationArgumentList&, const das::AnnotationArgumentList&,
                  das::string&) override {
        return true;
    }
};

}  // namespace

//! Хранилище `@live`-переменных (engine/core/LiveStore.hpp) и аннотации перезагрузки для
//! daslang. Хост ставит и читает хранилище через ScriptSystem, скрипты — через `tryengine/Live`.
class Module_Live : public das::Module {
public:
    Module_Live() : das::Module("tryLive") {
        das::ModuleLibrary lib(this);
        lib.addBuiltInModule();

        addAnnotation(new BeforeReloadAnnotation());
        addAnnotation(new AfterReloadAnnotation());

        das::addExtern<DAS_BIND_FUN(LiveStoreBytes)>(*this, lib, "try_live_store_bytes",
                                                     das::SideEffects::modifyExternal, "LiveStoreBytes");
        das::addExtern<DAS_BIND_FUN(LiveLoadBytes)>(*this, lib, "try_live_load_bytes",
                                                    das::SideEffects::modifyArgumentAndExternal, "LiveLoadBytes");
        das::addExtern<DAS_BIND_FUN(LiveIsReload)>(*this, lib, "try_live_is_reload", das::SideEffects::accessGlobal,
                                                   "LiveIsReload");

        verifyAotReady();
    }
};

REGISTER_DYN_MODULE(Module_Live, Module_Live);
REGISTER_MODULE(Module_Live);
