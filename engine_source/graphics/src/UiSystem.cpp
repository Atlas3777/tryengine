#include "engine/graphics/UiSystem.hpp"

#include <EASTL/utility.h>

#include "engine/core/Log.hpp"
#include "engine/graphics/VulkanDevice.hpp"
#include "engine/resources/ResourceManager.hpp"

namespace tryengine::graphics {

UiSystem* UiSystem::instance_ = nullptr;

UiSystem::UiSystem(UiConfig config, VulkanDevice& device, resources::ResourceManager& resource_manager)
    : config_(eastl::move(config)), device_(device), resource_manager_(resource_manager) {
    if (instance_ != nullptr && instance_ != this)
        LogWarn(LogCategory::Graphics, "UiSystem: a second UI instance replaced the script-visible one");
    instance_ = this;
}

UiSystem::~UiSystem() {
    if (instance_ == this)
        instance_ = nullptr;
}

UiSystem* UiSystem::ForScripts() {
    return instance_;
}

void UiSystem::SetAssets(uint64_t font_guid, uint64_t material_guid) {
    if (font_guid != config_.font_guid) {
        config_.font_guid = font_guid;
        font_ = nullptr;
        font_failed_logged_ = false;
        font_not_requested_logged_ = false;
        atlas_bound_ = false;
        assets_missing_logged_ = false;
    }
    if (material_guid != config_.material_guid) {
        config_.material_guid = material_guid;
        material_ = nullptr;
        material_failed_logged_ = false;
        material_not_requested_logged_ = false;
        atlas_bound_ = false;
        assets_missing_logged_ = false;
    }
}

void UiSystem::Tick() {
    if (config_.font_guid == 0 || config_.material_guid == 0) {
        // Startup normally runs a few frames before the game's boot request calls
        // UiSetAssets; only a lasting absence is a mistake worth a warning.
        const auto now = std::chrono::steady_clock::now();
        if (assets_missing_since_ == std::chrono::steady_clock::time_point{})
            assets_missing_since_ = now;
        if (!assets_missing_logged_ && now - assets_missing_since_ > std::chrono::seconds(10)) {
            assets_missing_logged_ = true;
            LogWarn(LogCategory::Graphics,
                    "UiSystem: UI assets are not set; call UiSetAssets(font_guid, material_guid) after the ui.bundle "
                    "request is ready (the game UI stays off until then)");
        }
        return;
    }
    assets_missing_since_ = {};

    PollFont();
    PollMaterial();

    // Re-import or material live-reload replaces the stored resource; the pointers we hold are
    // only valid while the cache keeps the same object, so re-read them and rebind the atlas.
    if (font_ != nullptr) {
        Font* current = resource_manager_.GetPointerExplicit<Font>(config_.font_guid);
        if (current != font_ && current != nullptr) {
            font_ = current;
            atlas_bound_ = false;
        }
    }
    if (material_ != nullptr && config_.material_guid != 0) {
        Material* current = resource_manager_.GetPointerExplicit<Material>(config_.material_guid);
        if (current != material_ && current != nullptr) {
            material_ = current;
            atlas_bound_ = false;
        }
    }

    BindAtlas();
}

void UiSystem::PollFont() {
    if (font_ != nullptr)
        return;

    if (resource_manager_.IsReady<Font>(config_.font_guid)) {
        font_ = resource_manager_.GetPointerExplicit<Font>(config_.font_guid);
        if (font_ != nullptr)
            LogInfo(LogCategory::Graphics, "UI font {} ready: {} baked glyphs", config_.font_guid,
                    font_->glyphs.size());
        return;
    }
    if (resource_manager_.IsFailed<Font>(config_.font_guid) && !font_failed_logged_) {
        font_failed_logged_ = true;
        LogError("UI font {} failed to load: check the .ttf import", config_.font_guid);
        return;
    }
    // Nobody asked the loader for this asset: the UI will stay blank, and the cause is a
    // missing ui.bundle entry rather than a slow read.
    if (!resource_manager_.IsLoading<Font>(config_.font_guid) && !font_not_requested_logged_) {
        font_not_requested_logged_ = true;
        LogError("UI font {} was not preloaded; add it to the ui.bundle and load that bundle before the UI frame",
                 config_.font_guid);
    }
}

void UiSystem::PollMaterial() {
    if (material_ != nullptr)
        return;

    if (resource_manager_.IsReady<Material>(config_.material_guid)) {
        material_ = resource_manager_.GetPointerExplicit<Material>(config_.material_guid);
        return;
    }
    if (resource_manager_.IsFailed<Material>(config_.material_guid) && !material_failed_logged_) {
        material_failed_logged_ = true;
        LogError("UI material {} failed to load", config_.material_guid);
        return;
    }
    if (!resource_manager_.IsLoading<Material>(config_.material_guid) && !material_not_requested_logged_) {
        material_not_requested_logged_ = true;
        LogError("UI material {} was not preloaded; add it to the ui.bundle and load that bundle before the UI frame",
                 config_.material_guid);
    }
}

void UiSystem::BindAtlas() {
    if (atlas_bound_ || font_ == nullptr || material_ == nullptr)
        return;

    const auto* binding = material_->shader->reflection.FindBinding("albedo_map");
    if (binding == nullptr) {
        LogError("UI material must sample the font atlas through a sampler named 'albedo_map'");
        atlas_bound_ = true;  // do not repeat the error every frame
        return;
    }

    atlas_binding_ = binding->binding;
    if (font_->atlas.view != VK_NULL_HANDLE && font_->atlas.sampler != VK_NULL_HANDLE) {
        material_->SetRawTexture(atlas_binding_, font_->atlas.view, font_->atlas.sampler);
        ++atlas_version_;
        LogInfo(LogCategory::Graphics, "UI font atlas bound to material set 1, binding {}", atlas_binding_);
    }
    atlas_bound_ = true;
}

Material* UiSystem::TextureMaterial(uint64_t texture_guid) {
    if (texture_guid == 0 || material_ == nullptr || !material_->shader)
        return nullptr;

    // Reference-only: the texture belongs to a screen bundle the game or the editor loaded
    // before the screen. If it is missing, the UI draws nothing; it must not start a read.
    TextureSampler* texture = resource_manager_.GetPointerExplicit<TextureSampler>(texture_guid);
    if (texture == nullptr) {
        // The bundle publishes its names as soon as it is parsed; its resources arrive in the
        // loader's Resources phase. Loading is silent, a texture nobody requested is an error.
        if (resource_manager_.IsLoading<TextureSampler>(texture_guid))
            return nullptr;
        if (texture_missing_logged_.find(texture_guid) == texture_missing_logged_.end()) {
            texture_missing_logged_[texture_guid] = 1;
            LogError("UI texture {} was not preloaded; add it to the screen bundle and load that bundle first",
                     texture_guid);
        }
        return nullptr;
    }

    auto cached = texture_materials_.find(texture_guid);
    if (cached != texture_materials_.end()) {
        if (cached->second.view == texture->view)
            return cached->second.material.get();

        // The asset was re-baked while the UI was running: the cached set points at a destroyed
        // image. Keep the old material alive (a recorded command buffer may reference its set)
        // and build a fresh one below.
        retired_texture_materials_.push_back(eastl::move(cached->second.material));
        texture_materials_.erase(cached);
    }

    const auto* binding = material_->shader->reflection.FindBinding("albedo_map");
    if (binding == nullptr) {
        if (!texture_binding_error_logged_) {
            texture_binding_error_logged_ = true;
            LogError("UI material must sample images through a sampler named 'albedo_map'");
        }
        return nullptr;
    }

    auto created = eastl::make_unique<Material>(material_->shader);
    created->SetDefaultTexture(device_.GetDefaultWhiteTexture().view, device_.GetDefaultWhiteTexture().sampler);
    created->InitGpuResources(device_.GetDevice(), device_.GetPhysicalDevice());
    created->SetRawTexture(binding->binding, texture->view, texture->sampler);
    created->FinalizeDescriptors();

    Material* raw = created.get();
    TextureMaterialEntry entry{};
    entry.material = eastl::move(created);
    entry.view = texture->view;
    entry.sampler = texture->sampler;
    texture_materials_.emplace(texture_guid, eastl::move(entry));
    return raw;
}

uint32_t UiSystem::InternText(const char* text) {
    const uint32_t id = static_cast<uint32_t>(frame_strings_.size());
    frame_strings_.emplace_back(text != nullptr ? text : "");
    return id;
}

const char* UiSystem::FrameString(uint32_t id) const {
    return id < frame_strings_.size() ? frame_strings_[id].c_str() : nullptr;
}

void UiSystem::ResetFrameStrings() {
    frame_strings_.clear();
}

void UiSystem::SetOverlayText(const char* text) {
    overlay_text_ = text != nullptr ? text : "";
}

das::float2 UiSystem::WindowSize() const {
    int width = 0;
    int height = 0;
    SDL_Window* window = device_.GetWindow();
    if (window != nullptr)
        SDL_GetWindowSizeInPixels(window, &width, &height);
    return das::float2(static_cast<float>(width > 0 ? width : 1), static_cast<float>(height > 0 ? height : 1));
}

bool UiSystem::IsReady() const {
    return font_ != nullptr && material_ != nullptr && material_->shader && material_->shader->vertex_shader &&
           material_->shader->fragment_shader && atlas_bound_;
}

Material* UiSystem::GetMaterial() const {
    return material_;
}

const Font* UiSystem::GetFont() const {
    return font_;
}

UiFrameData UiSystem::FrameData(const das::float2& viewport) const {
    UiFrameData data{};
    data.viewport = viewport;
    if (font_ != nullptr) {
        data.params.x = font_->header.onedge_value / 255.0f;
        data.params.y = font_->header.pixel_dist_scale;
    }
    return data;
}

float UiSystem::LineHeight(float px) const {
    if (font_ == nullptr || font_->header.em_size <= 0.0f)
        return 0.0f;
    return font_->header.line_height * (px / font_->header.em_size);
}

float UiSystem::Ascent(float px) const {
    if (font_ == nullptr || font_->header.em_size <= 0.0f)
        return 0.0f;
    return font_->header.ascender * (px / font_->header.em_size);
}

float UiSystem::Descent(float px) const {
    if (font_ == nullptr || font_->header.em_size <= 0.0f)
        return 0.0f;
    return font_->header.descender * (px / font_->header.em_size);
}

float UiSystem::Advance(uint32_t codepoint, float px) const {
    if (font_ == nullptr || font_->header.em_size <= 0.0f || px <= 0.0f)
        return 0.0f;
    const auto* glyph = font_->FindGlyph(codepoint);
    if (glyph == nullptr && codepoint != '?')
        glyph = font_->FindGlyph('?');
    if (glyph == nullptr)
        return 0.0f;
    return glyph->advance * (px / font_->header.em_size);
}

bool UiSystem::Glyph(uint32_t codepoint, float px, UiGlyphQuad& out) const {
    if (font_ == nullptr || font_->header.em_size <= 0.0f || px <= 0.0f)
        return false;

    const auto* glyph = font_->FindGlyph(codepoint);
    if (glyph == nullptr && codepoint != '?')
        glyph = font_->FindGlyph('?');
    if (glyph == nullptr || glyph->plane_right <= glyph->plane_left)
        return false;

    const float scale = px / font_->header.em_size;
    out.u0 = glyph->atlas_left;
    out.v0 = glyph->atlas_top;
    out.u1 = glyph->atlas_right;
    out.v1 = glyph->atlas_bottom;
    out.plane_left = glyph->plane_left * scale;
    out.plane_top = glyph->plane_top * scale;
    out.plane_right = glyph->plane_right * scale;
    out.plane_bottom = glyph->plane_bottom * scale;
    out.px_per_em = scale;
    return true;
}

das::float2 UiSystem::Measure(const char* utf8, float px) const {
    if (font_ == nullptr || utf8 == nullptr || px <= 0.0f)
        return das::float2(0.0f, 0.0f);

    float width = 0.0f;
    const char* cursor = utf8;
    while (*cursor != '\0') {
        uint32_t codepoint = 0;
        cursor = NextCodepoint(cursor, codepoint);
        width += Advance(codepoint, px);
    }
    return das::float2(width, LineHeight(px));
}

const char* UiSystem::NextCodepoint(const char* text, uint32_t& codepoint) {
    if (text == nullptr || *text == '\0') {
        codepoint = 0;
        return text != nullptr ? text : "";
    }

    const auto first = static_cast<uint8_t>(*text++);
    if (first < 0x80) {
        codepoint = first;
        return text;
    }

    uint32_t value = 0;
    int continuation_count = 0;
    if ((first & 0xE0) == 0xC0) {
        value = first & 0x1Fu;
        continuation_count = 1;
    } else if ((first & 0xF0) == 0xE0) {
        value = first & 0x0Fu;
        continuation_count = 2;
    } else if ((first & 0xF8) == 0xF0) {
        value = first & 0x07u;
        continuation_count = 3;
    } else {
        codepoint = 0xFFFDu;
        return text;
    }

    for (int i = 0; i < continuation_count; ++i) {
        const auto byte = static_cast<uint8_t>(*text);
        if ((byte & 0xC0) != 0x80) {
            codepoint = 0xFFFDu;
            return text;
        }
        value = (value << 6) | (byte & 0x3Fu);
        ++text;
    }
    codepoint = value;
    return text;
}

}  // namespace tryengine::graphics
