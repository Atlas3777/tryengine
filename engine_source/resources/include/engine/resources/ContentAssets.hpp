#pragma once

#include <EASTL/string_view.h>
#include <EASTL/vector.h>

#include "engine/core/MakeError.hpp"
#include "engine/resources/AssetRegistry.hpp"
#include "engine/resources/ConfigFormat.hpp"

namespace tryengine::resources {

// Resource ids a content file references. Build packs them into the same pak; the
// script-side Loader requests and waits for them before it switches to the target scene.
struct ContentAssetRefs {
    eastl::vector<uint64_t> meshes;
    eastl::vector<uint64_t> mats;
};

namespace content_detail {

inline uint64_t ConfigUInt64(const ConfigField* field) {
    if (!field)
        return 0;
    if (const auto* value = eastl::get_if<uint64_t>(&field->value))
        return *value;
    if (const auto* value = eastl::get_if<int64_t>(&field->value))
        return static_cast<uint64_t>(*value);
    if (const auto* value = eastl::get_if<uint32_t>(&field->value))
        return *value;
    if (const auto* value = eastl::get_if<int32_t>(&field->value))
        return static_cast<uint64_t>(*value);
    return 0;
}

inline void AddRefArray(const ConfigField* field, const AssetRegistry& registry, eastl::vector<uint64_t>& out) {
    const ConfigArray* array = field ? GetArray(field->value) : nullptr;
    if (!array)
        return;

    for (const auto& element : array->elements) {
        if (const auto* ref = eastl::get_if<eastl::string>(&element.value)) {
            const auto id = registry.ResolveRef(*ref);
            if (id.has_value() && *id != 0)
                out.push_back(*id);
        }
    }
}

}  // namespace content_detail

// Reads the same fields the prefab spawner applies. A prefab is usable in a game only
// when its meshes and materials are packed and preloaded as well.
inline void CollectPrefabAssetRefs(eastl::string_view text, const AssetRegistry& registry, ContentAssetRefs& out) {
    using namespace content_detail;

    auto parsed = ParseConfig(text);
    if (!parsed.has_value()) {
        LogError("Prefab: failed to parse for asset refs: {}", parsed.error().Message());
        return;
    }

    const ConfigField* entities_field = FindField(*parsed, "entities");
    const ConfigArray* entities = entities_field ? GetArray(entities_field->value) : nullptr;
    if (!entities)
        return;

    for (const auto& element : entities->elements) {
        const ConfigObject* entity = GetObject(element.value);
        if (!entity)
            continue;

        const uint64_t mesh = ConfigUInt64(FindField(*entity, "meshGuid"));
        if (mesh != 0)
            out.meshes.push_back(mesh);

        if (const ConfigField* ref_field = FindField(*entity, "materialRef")) {
            const ConfigObject* ref_obj = GetObject(ref_field->value);
            if (!ref_obj)
                continue;
            const ConfigField* path_field = FindField(*ref_obj, "path");
            if (!path_field)
                continue;
            const auto* ref = eastl::get_if<eastl::string>(&path_field->value);
            if (!ref)
                continue;
            const auto id = registry.ResolveRef(*ref);
            if (id.has_value() && *id != 0)
                out.mats.push_back(*id);
        }
    }
}

// A scene header lists the meshes and materials it requests on load.
inline void CollectSceneAssetRefs(eastl::string_view text, const AssetRegistry& registry, ContentAssetRefs& out) {
    using namespace content_detail;

    auto parsed = ParseConfig(text);
    if (!parsed.has_value()) {
        LogError("Scene: failed to parse for asset refs: {}", parsed.error().Message());
        return;
    }

    AddRefArray(FindField(*parsed, "requireMeshs"), registry, out.meshes);
    AddRefArray(FindField(*parsed, "requireMaterials"), registry, out.mats);
}

}  // namespace tryengine::resources
