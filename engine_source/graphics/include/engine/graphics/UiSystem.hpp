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

//! Guid of the UI font asset: the guid stored in `game/assets/fonts/<Font>.ttf.meta`. The
//! FontImporter turns the .ttf into a `.font` artifact under this guid; put the imported
//! number here (0 disables the game UI until a font is imported).
inline constexpr uint64_t kDefaultUiFontGuid = 7732110565556845333ULL;

//! Guid of the texture the sample HUD draws with `Ui::ImageTexture`, so a build can pack it:
//! nothing else references a UI-only texture, and the pak keeps no path table for assets.
//! Leave the list empty when your game has no UI textures.
inline constexpr uint64_t kDefaultUiTextureGuid = 14429000468236573201ULL;

//! UI textures by guid: what the UI draws, and what a build has to pack into the common pak.
inline eastl::vector<uint64_t> DefaultUiTextures() {
    eastl::vector<uint64_t> out;
    if (kDefaultUiTextureGuid != 0)
        out.push_back(kDefaultUiTextureGuid);
    return out;
}

//! Game UI configuration: the font asset (its runtime id), the material the UI draws with
//! (an authoritative `game/data/...` path) and the textures it addresses by guid.
struct UiConfig {
    uint64_t font_guid = kDefaultUiFontGuid;
    //! Texture assets the UI samples by guid. The editor loads them loose; a build packs them
    //! into the common pak (see BuildSettings::ui_textures).
    eastl::vector<uint64_t> textures = DefaultUiTextures();
    eastl::string material_path = "game/data/materials/ui.mat";
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

//! Owns the one font the UI draws with: polls its async load (and the UI material's), binds
//! the distance-field atlas into that material once both are ready, and answers metrics to
//! the scripts and glyph quads to the pass. Nothing here builds vertices or touches the frame.
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

    [[nodiscard]] const UiConfig& GetConfig() const {
        return config_;
    }

    [[nodiscard]] uint64_t GetFontGuid() const {
        return config_.font_guid;
    }

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

    bool font_requested_ = false;
    bool material_requested_ = false;
    bool font_failed_logged_ = false;
    bool material_failed_logged_ = false;
    bool atlas_bound_ = false;

    static UiSystem* instance_;
    uint32_t atlas_version_ = 0;
    eastl::vector<eastl::string> frame_strings_;
    uint64_t material_id_ = 0;
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
    //! Textures the UI asked for; keeps RequestLoad from being issued every frame per guid.
    eastl::unordered_map<uint64_t, uint8_t> texture_requested_;
    //! Replaced materials whose descriptor sets may still be referenced by a recorded command
    //! buffer. Destruction of the GPU resources is deferred to the whole UI's lifetime.
    eastl::vector<eastl::unique_ptr<Material>> retired_texture_materials_;
    bool texture_binding_error_logged_ = false;
};

}  // namespace tryengine::graphics
