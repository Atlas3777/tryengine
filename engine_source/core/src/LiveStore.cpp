#include "engine/core/LiveStore.hpp"

#include <EASTL/hash_map.h>
#include <EASTL/string.h>

namespace tryengine::core {

namespace {

struct LiveStoreState {
    eastl::hash_map<eastl::string, eastl::vector<uint8_t>> bytes;
    bool reload = false;
};

//! Одно хранилище на процесс. Функция-local static, а не глобальная переменная: порядок
//! инициализации между статическими объектами не важен, а хранилище нужно из биндинга
//! (Module_Live) и из ScriptSystem при первой загрузке.
LiveStoreState& State() {
    static LiveStoreState state;
    return state;
}

eastl::string MakeKey(eastl::string_view key) {
    return eastl::string(key.data(), key.size());
}

}  // namespace

void StoreLiveBytes(eastl::string_view key, const uint8_t* data, size_t size) {
    auto& state = State();
    eastl::vector<uint8_t>& value = state.bytes[MakeKey(key)];
    if (size == 0) {
        value.clear();
        return;
    }
    value.assign(data, data + size);
}

bool LoadLiveBytes(eastl::string_view key, eastl::vector<uint8_t>& data) {
    const auto& state = State();
    const auto it = state.bytes.find(MakeKey(key));
    if (it == state.bytes.end())
        return false;
    data = it->second;
    return true;
}

void SetLiveReload(bool reload) {
    State().reload = reload;
}

bool IsLiveReload() {
    return State().reload;
}

void ResetLiveStore() {
    State().bytes.clear();
    State().reload = false;
}

}  // namespace tryengine::core
