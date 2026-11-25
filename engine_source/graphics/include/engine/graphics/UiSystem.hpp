#pragma once

#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>
#include <daScript/misc/vectypes.h>

#include "engine/graphics/RuntimeTypes.hpp"

namespace tryengine::resources {
class ResourceManager;
}

namespace tryengine::graphics {

class VulkanDevice;

//! Authored base material of the UI shader. The material is fixed for the whole UI: texture and
//! font materials are derived from it at runtime (same shader, different `albedo_map`), so no
//! bundle entry is needed for it. The game's boot request loads it by this path:
//! `request.AddAsset("material", UiMaterialPath())`.
inline constexpr const char* kUiMaterialPath = "game/data/materials/ui.mat";

//! UI configuration. The base material path is the only engine-owned asset; fonts, textures
//! and custom materials come from screen bundles and are resolved by the UI script.
struct UiConfig {
    eastl::string material_path = kUiMaterialPath;
};

//! Frame constants of the UI shader, pushed once per draw. Mirrored in Ui.slang.
struct alignas(16) UiFrameData {
    das::float2 viewport = das::float2(1.0f, 1.0f);
    das::float2 _pad = das::float2(0.0f, 0.0f);
    //! x = onedge_value / 255, y = pixel_dist_scale, z/w reserved.
    das::float4 params = das::float4(128.0f / 255.0f, 32.0f, 0.0f, 0.0f);
};

//! One glyph quad: atlas UVs plus the glyph plane in em pixels, relative to the pen on the
//! baseline (y down). The pass scales the plane by the requested pixel size and moves it.
struct UiGlyphQuad {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    float plane_left = 0.0f;
    float plane_top = 0.0f;
    float plane_right = 0.0f;
    float plane_bottom = 0.0f;
    float px_per_em = 0.0f;
};

//! Owns the base UI material and one derived material per font. It never requests a resource:
//! the base material and every font are preloaded by the loader; `FontMaterial` only builds a
//! descriptor set when the font is already resident. Text commands carry the font material,
//! the pass finds the `Font` by it to build glyph quads.
class UiSystem {
public:
    UiSystem(UiConfig config, VulkanDevice& device, resources::ResourceManager& resource_manager);
    ~UiSystem();

    UiSystem(const UiSystem&) = delete;
    UiSystem& operator=(const UiSystem&) = delete;
    UiSystem(UiSystem&&) = delete;
    UiSystem& operator=(UiSystem&&) = delete;

    //! The instance the tryUi bindings talk to. There is one UI per process: the editor and
    //! the game each own a single engine, and both editor contexts (normal and hot-reload)
    //! share it. Null outside the engine, which is what a headless daslang CLI sees.
    static UiSystem* ForScripts();

    //! Advances loading; safe to call every frame and before anything is ready.
    void Tick();

    //! True when the base material is resident; text also needs its font material, which the
    //! script asks for per style.
    [[nodiscard]] bool IsReady() const;
    [[nodiscard]] Material* GetMaterial() const;

    //! Authored path of the base material; diagnostics only.
    [[nodiscard]] const eastl::string& MaterialPath() const {
        return config_.material_path;
    }

    //! Material that draws `texture_guid` through the UI shader: the same shader and pipeline as
    //! the base material, with the image bound where the font atlas sits. Built once per texture
    //! and rebuilt if the imported texture is replaced; null while the texture loads.
    [[nodiscard]] Material* TextureMaterial(uint64_t texture_guid);

    //! Material that draws glyphs of `font_id`: same shader as the base material, the font atlas
    //! bound to `albedo_map`. Null while the font is not resident (the loader preloads it; this
    //! method never requests). Rebuilt when the font is re-baked.
    [[nodiscard]] Material* FontMaterial(uint64_t font_id);

    //! Font behind a text command material (the inverse of `FontMaterial`); null for the base
    //! material and for texture materials.
    [[nodiscard]] const Font* FontForMaterial(const Material* material) const;

    [[nodiscard]] UiFrameData FrameData(const das::float2& viewport) const;

    //! Line height of `font_id`; zero while the font is not resident.
    [[nodiscard]] float LineHeight(uint64_t font_id, float px) const;
    //! Metrics of a resolved font (the pass uses these after `FontForMaterial`).
    [[nodiscard]] float Ascent(const Font* font, float px) const;
    [[nodiscard]] float Advance(const Font* font, uint32_t codepoint, float px) const;
    //! Glyph quad, with '?' as the fallback for a codepoint the font does not have.
    [[nodiscard]] bool Glyph(const Font* font, uint32_t codepoint, float px, UiGlyphQuad& out) const;
    //! Width and line height of one line of UTF-8 text; zero while the font is not resident.
    [[nodiscard]] das::float2 Measure(const char* utf8, float px, uint64_t font_id) const;

    //! Overlay text has no font when the boot request has not run yet; the fallback draws only
    //! its dots then. Kept as an overload so the pass does not special-case it.
    [[nodiscard]] das::float2 Measure(const char* utf8, float px) const {
        (void) utf8;
        (void) px;
        return das::float2(0.0f, 0.0f);
    }

    //! Decodes one UTF-8 codepoint and returns the pointer after it; `text` is never null.
    static const char* NextCodepoint(const char* text, uint32_t& codepoint);

    //! Frame-local string table: scripts intern text when they emit a text command, the pass
    //! resolves the ids while expanding it. Cleared by UiFrameBegin.
    uint32_t InternText(const char* text);
    [[nodiscard]] const char* FrameString(uint32_t id) const;
    void ResetFrameStrings();

    //! Text of the C++ fallback overlay (loading dots caption, compile error). Unlike
    //! InternText it is not part of the script frame table: when the script context does not
    //! exist yet there is no UiFrameBegin to clear it. Cleared by an empty string.
    void SetOverlayText(const char* text);

    [[nodiscard]] const char* OverlayText() const {
        return overlay_text_.c_str();
    }

    //! Grows when a font material is built or rebuilt; scripts use it to drop cached glyph
    //! metrics.
    [[nodiscard]] uint32_t AtlasVersion() const {
        return atlas_version_;
    }

    //! Window size in pixels; the standalone game's default UI viewport, the editor overrides it.
    [[nodiscard]] das::float2 WindowSize() const;

private:
    void PollMaterial();
    [[nodiscard]] const Font* FontById(uint64_t font_id) const;

    UiConfig config_;
    VulkanDevice& device_;
    resources::ResourceManager& resource_manager_;

    bool material_failed_logged_ = false;

    static UiSystem* instance_;
    uint32_t atlas_version_ = 0;
    eastl::vector<eastl::string> frame_strings_;
    eastl::string overlay_text_;
    uint64_t material_id_ = 0;
    Material* material_ = nullptr;
    //! SDF constants of the first font material; all UI fonts are expected to share the bake
    //! settings (`onedge_value`, `pixel_dist_scale`), so one frame constant serves the pass.
    das::float4 frame_params_ = das::float4(128.0f / 255.0f, 32.0f, 0.0f, 0.0f);
    bool frame_params_set_ = false;

    //! One material per UI texture. The image is bound as a raw texture, exactly like a font
    //! atlas, so no ResourceManager handle lives inside the material.
    struct TextureMaterialEntry {
        eastl::unique_ptr<Material> material;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };

    eastl::unordered_map<uint64_t, TextureMaterialEntry> texture_materials_;
    //! Textures a screen named but nobody preloaded; one error per guid, not per frame.
    eastl::unordered_map<uint64_t, uint8_t> texture_missing_logged_;
    //! Replaced materials whose descriptor sets may still be referenced by a recorded command
    //! buffer. Destruction of the GPU resources is deferred to the whole UI's lifetime.
    eastl::vector<eastl::unique_ptr<Material>> retired_texture_materials_;
    bool texture_binding_error_logged_ = false;

    //! One material per font asset: base shader + the font atlas bound to `albedo_map`.
    struct FontMaterialEntry {
        eastl::unique_ptr<Material> material;
        const Font* font = nullptr;
        VkImageView view = VK_NULL_HANDLE;
    };

    eastl::unordered_map<uint64_t, FontMaterialEntry> font_materials_;
    eastl::unordered_map<uint64_t, uint8_t> font_missing_logged_;
    eastl::vector<eastl::unique_ptr<Material>> retired_font_materials_;
};

}  // namespace tryengine::graphics
