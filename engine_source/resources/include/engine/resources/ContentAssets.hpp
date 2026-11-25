#pragma once

#include <EASTL/string_view.h>
#include <EASTL/vector.h>

#include "engine/core/MakeError.hpp"
#include "engine/resources/AssetRegistry.hpp"
#include "engine/resources/ConfigFormat.hpp"

namespace tryengine::resources {

// Resource ids a content file references. Build packs them into the same pak; the
// script-side Loader requests and waits for them before it switches to the target scene.
struct ContentDependencies {
    eastl::vector<uint64_t> meshes;
    eastl::vector<uint64_t> materials;
    eastl::vector<uint64_t> textures;
    eastl::vector<uint64_t> fonts;
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

// Reads the nodes of a prefab document - the same fields the prefab spawner applies. A
// prefab is usable in a game only when its meshes and materials are packed and preloaded
// as well.
inline void CollectPrefabDependencies(eastl::string_view text, const AssetRegistry& registry,
                                      ContentDependencies& out) {
    using namespace content_detail;

    auto parsed = ParseConfig(text);
    if (!parsed.has_value()) {
        LogError("Prefab: failed to parse for dependencies: {}", parsed.error().Message());
        return;
    }

    const ConfigField* nodes_field = FindField(*parsed, "nodes");
    const ConfigArray* nodes = nodes_field ? GetArray(nodes_field->value) : nullptr;
    if (!nodes)
        return;

    for (const auto& element : nodes->elements) {
        const ConfigObject* node = GetObject(element.value);
        if (!node)
            continue;

        // `model` = ModelRender: гид меша плюс необязательный оверрайд материала путём.
        // Материал по умолчанию приходит из артефакта меша и собирается в BuildSystem.
        const ConfigField* model_field = FindField(*node, "model");
        const ConfigObject* model = model_field ? GetObject(model_field->value) : nullptr;
        if (!model)
            continue;

        const uint64_t mesh = ConfigUInt64(FindField(*model, "mesh"));
        if (mesh != 0)
            out.meshes.push_back(mesh);

        if (const ConfigField* ref_field = FindField(*model, "material")) {
            const ConfigObject* ref_obj = GetObject(ref_field->value);
            if (!ref_obj)
                continue;
            const ConfigField* path_field = FindField(*ref_obj, "path");
            if (!path_field)
                continue;
            const auto* ref = eastl::get_if<eastl::string>(&path_field->value);
            if (!ref || ref->empty())
                continue;
            const auto id = registry.ResolveRef(*ref);
            if (id.has_value() && *id != 0)
                out.materials.push_back(*id);
        }
    }
}

// A scene header lists the meshes and materials it requests on load.
inline void CollectSceneDependencies(eastl::string_view text, const AssetRegistry& registry, ContentDependencies& out) {
    using namespace content_detail;

    auto parsed = ParseConfig(text);
    if (!parsed.has_value()) {
        LogError("Scene: failed to parse for dependencies: {}", parsed.error().Message());
        return;
    }

    AddRefArray(FindField(*parsed, "requireMeshs"), registry, out.meshes);
    AddRefArray(FindField(*parsed, "requireMaterials"), registry, out.materials);
}

// A bundle declares the assets of its screen as `{ name, kind, ref }`. The loader learns the
// same ids from the script side; the build needs them to put every asset the screen names
// into the common pak, because nothing else references a UI-only texture or font.
inline void CollectBundleDependencies(eastl::string_view text, const AssetRegistry& registry,
                                      ContentDependencies& out) {
    auto parsed = ParseConfig(text);
    if (!parsed.has_value()) {
        LogError("Bundle: failed to parse for dependencies: {}", parsed.error().Message());
        return;
    }

    const ConfigField* assets_field = FindField(*parsed, "assets");
    const ConfigArray* assets = assets_field ? GetArray(assets_field->value) : nullptr;
    if (!assets)
        return;

    for (const auto& element : assets->elements) {
        const ConfigObject* asset = GetObject(element.value);
        if (!asset)
            continue;

        const ConfigField* kind_field = FindField(*asset, "kind");
        const ConfigField* ref_field = FindField(*asset, "ref");
        if (!kind_field || !ref_field)
            continue;

        const auto* kind = eastl::get_if<eastl::string>(&kind_field->value);
        const auto* ref = eastl::get_if<eastl::string>(&ref_field->value);
        if (!kind || !ref || ref->empty())
            continue;

        const auto id = registry.ResolveRef(*ref);
        if (!id.has_value() || *id == 0) {
            LogError("Bundle asset '{}': reference '{}' is not a known asset", *kind, ref->c_str());
            continue;
        }

        if (*kind == "texture")
            out.textures.push_back(*id);
        else if (*kind == "font")
            out.fonts.push_back(*id);
        else if (*kind == "material")
            out.materials.push_back(*id);
        else if (*kind == "mesh")
            out.meshes.push_back(*id);
    }
}

}  // namespace tryengine::resources
