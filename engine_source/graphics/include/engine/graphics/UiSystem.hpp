#pragma once

#include <EASTL/string.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>
#include <chrono>
#include <daScript/misc/vectypes.h>

#include "engine/graphics/RuntimeTypes.hpp"

namespace tryengine::resources {
class ResourceManager;
}

namespace tryengine::graphics {

class VulkanDevice;

//! Optional asset overrides for an embedder that does not use bundles. The game and the editor
//! leave both at 0 and call `UiSystem::SetAssets` (script binding `UiSetAssets`) once the
//! loader has made the assets of the `ui.bundle` ready. `UiSystem` itself never requests a
//! resource: a UI asset that nobody preloaded stays missing, which is a loader bug, not a
//! reason for the UI to read the disk.
struct UiConfig {
    uint64_t font_guid = 0;
    uint64_t material_guid = 0;
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

//! Owns the one font the UI draws with: reads the loader-preloaded font and UI material,
//! binds the distance-field atlas into that material once both are ready, and answers metrics
//! to the scripts and glyph quads to the pass. It never requests a resource: `SetAssets` is
//! the game's/editor's promise that the loader already made them ready.
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

    [[nodiscard]] bool IsReady() const;
    [[nodiscard]] Material* GetMaterial() const;
    [[nodiscard]] const Font* GetFont() const;

    //! Material that draws `texture_guid` through the UI shader: the same shader and pipeline as
    //! the built-in UI material, with the image bound where the font atlas sits. Built once per
    //! texture and rebuilt if the imported texture is replaced; null while the texture loads.
    [[nodiscard]] Material* TextureMaterial(uint64_t texture_guid);

    //! Points the UI at the font and UI material the loader already preloaded. Called from
    //! the game/editor script (binding `UiSetAssets`) after the `ui.bundle` request finished.
    //! A changed guid drops the cached pointers and rebinds the atlas on the next `Tick`.
    void SetAssets(uint64_t font_guid, uint64_t material_guid);

    //! True when the built-in material has no unused sampler that still points at the white
    //! fallback texture; a warning is logged once when that happens.
    [[nodiscard]] UiFrameData FrameData(const das::float2& viewport) const;

    [[nodiscard]] float LineHeight(float px) const;
    [[nodiscard]] float Ascent(float px) const;
    [[nodiscard]] float Descent(float px) const;
    [[nodiscard]] float Advance(uint32_t codepoint, float px) const;
    //! Glyph quad, with '?' as the fallback for a codepoint the font does not have.
    [[nodiscard]] bool Glyph(uint32_t codepoint, float px, UiGlyphQuad& out) const;
    //! Width and line height of one line of UTF-8 text; zero width when the font is not ready.
    [[nodiscard]] das::float2 Measure(const char* utf8, float px) const;
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

    //! Grows when the atlas becomes available; scripts use it to drop cached glyph metrics.
    [[nodiscard]] uint32_t AtlasVersion() const {
        return atlas_version_;
    }

    //! Window size in pixels; the standalone game's default UI viewport, the editor overrides it.
    [[nodiscard]] das::float2 WindowSize() const;

private:
    void PollFont();
    void PollMaterial();
    void BindAtlas();

    UiConfig config_;
    VulkanDevice& device_;
    resources::ResourceManager& resource_manager_;

    bool font_failed_logged_ = false;
    bool material_failed_logged_ = false;
    bool font_not_requested_logged_ = false;
    bool material_not_requested_logged_ = false;
    bool assets_missing_logged_ = false;
    bool atlas_bound_ = false;

    static UiSystem* instance_;
    uint32_t atlas_version_ = 0;
    eastl::vector<eastl::string> frame_strings_;
    eastl::string overlay_text_;
    Font* font_ = nullptr;
    Material* material_ = nullptr;
    uint32_t atlas_binding_ = 0;

    //! One material per UI texture. The image is bound as a raw texture, exactly like the atlas,
    //! so no ResourceManager handle lives inside the material.
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
    //! Wall clock since the UI first found its assets unset; a warning is issued only when
    //! the absence is lasting (a slow first load is normal, a forgotten UiSetAssets is not).
    std::chrono::steady_clock::time_point assets_missing_since_{};
};

}  // namespace tryengine::graphics
