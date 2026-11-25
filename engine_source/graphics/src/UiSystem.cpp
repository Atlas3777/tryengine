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

void UiSystem::Tick() {
    PollMaterial();
}

void UiSystem::PollMaterial() {
    if (material_id_ == 0) {
        const auto& registry = resource_manager_.GetAssetRegistry();
        const auto resolved = registry.ResolveRef(config_.material_path);
        if (!resolved.has_value() || *resolved == 0)
            return;  // registry may still be scanning at startup; retried next frame
        material_id_ = *resolved;
    }

    if (material_ != nullptr) {
        // Re-import or live material reload replaces the stored resource; the pointer we hold
        // is only valid while the cache keeps the same object. An aborted request can drop it:
        // go back to "not ready" instead of drawing through a stale pointer.
        Material* current = resource_manager_.GetPointerExplicit<Material>(material_id_);
        if (current == nullptr) {
            material_ = nullptr;
            return;
        }
        if (current != material_)
            material_ = current;
        return;
    }

    if (resource_manager_.IsReady<Material>(material_id_)) {
        material_ = resource_manager_.GetPointerExplicit<Material>(material_id_);
        return;
    }
    if (resource_manager_.IsFailed<Material>(material_id_) && !material_failed_logged_) {
        material_failed_logged_ = true;
        LogError("UI material '{}' failed to load", config_.material_path.c_str());
    }
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

Material* UiSystem::FontMaterial(uint64_t font_id) {
    if (font_id == 0 || material_ == nullptr || !material_->shader)
        return nullptr;

    Font* font = resource_manager_.GetPointerExplicit<Font>(font_id);
    if (font == nullptr) {
        if (resource_manager_.IsLoading<Font>(font_id))
            return nullptr;
        if (font_missing_logged_.find(font_id) == font_missing_logged_.end()) {
            font_missing_logged_[font_id] = 1;
            LogError("UI font {} was not preloaded; add it to the screen bundle (or its parent) and load that bundle",
                     font_id);
        }
        return nullptr;
    }

    auto cached = font_materials_.find(font_id);
    if (cached != font_materials_.end()) {
        if (cached->second.font == font && cached->second.view == font->atlas.view)
            return cached->second.material.get();

        retired_font_materials_.push_back(eastl::move(cached->second.material));
        font_materials_.erase(cached);
    }

    const auto* binding = material_->shader->reflection.FindBinding("albedo_map");
    if (binding == nullptr) {
        if (!texture_binding_error_logged_) {
            texture_binding_error_logged_ = true;
            LogError("UI material must sample the font atlas through a sampler named 'albedo_map'");
        }
        return nullptr;
    }

    auto created = eastl::make_unique<Material>(material_->shader);
    created->SetDefaultTexture(device_.GetDefaultWhiteTexture().view, device_.GetDefaultWhiteTexture().sampler);
    created->InitGpuResources(device_.GetDevice(), device_.GetPhysicalDevice());
    created->SetRawTexture(binding->binding, font->atlas.view, font->atlas.sampler);
    created->FinalizeDescriptors();

    if (!frame_params_set_) {
        frame_params_ = das::float4(font->header.onedge_value / 255.0f, font->header.pixel_dist_scale, 0.0f, 0.0f);
        frame_params_set_ = true;
    }

    Material* raw = created.get();
    FontMaterialEntry entry{};
    entry.material = eastl::move(created);
    entry.font = font;
    entry.view = font->atlas.view;
    font_materials_.emplace(font_id, eastl::move(entry));
    ++atlas_version_;
    return raw;
}

const Font* UiSystem::FontForMaterial(const Material* material) const {
    if (material == nullptr)
        return nullptr;
    for (const auto& pair : font_materials_)
        if (pair.second.material.get() == material)
            return pair.second.font;
    return nullptr;
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
    return material_ != nullptr && material_->shader && material_->shader->vertex_shader &&
           material_->shader->fragment_shader;
}

Material* UiSystem::GetMaterial() const {
    return material_;
}

UiFrameData UiSystem::FrameData(const das::float2& viewport) const {
    UiFrameData data{};
    data.viewport = viewport;
    data.params = frame_params_;
    return data;
}

const Font* UiSystem::FontById(uint64_t font_id) const {
    if (font_id == 0)
        return nullptr;
    return resource_manager_.GetPointerExplicit<Font>(font_id);
}

float UiSystem::LineHeight(uint64_t font_id, float px) const {
    const Font* font = FontById(font_id);
    if (font == nullptr || font->header.em_size <= 0.0f)
        return 0.0f;
    return font->header.line_height * (px / font->header.em_size);
}

float UiSystem::Ascent(const Font* font, float px) const {
    if (font == nullptr || font->header.em_size <= 0.0f)
        return 0.0f;
    return font->header.ascender * (px / font->header.em_size);
}

float UiSystem::Advance(const Font* font, uint32_t codepoint, float px) const {
    if (font == nullptr || font->header.em_size <= 0.0f || px <= 0.0f)
        return 0.0f;
    const auto* glyph = font->FindGlyph(codepoint);
    if (glyph == nullptr && codepoint != '?')
        glyph = font->FindGlyph('?');
    if (glyph == nullptr)
        return 0.0f;
    return glyph->advance * (px / font->header.em_size);
}

bool UiSystem::Glyph(const Font* font, uint32_t codepoint, float px, UiGlyphQuad& out) const {
    if (font == nullptr || font->header.em_size <= 0.0f || px <= 0.0f)
        return false;

    const auto* glyph = font->FindGlyph(codepoint);
    if (glyph == nullptr && codepoint != '?')
        glyph = font->FindGlyph('?');
    if (glyph == nullptr || glyph->plane_right <= glyph->plane_left)
        return false;

    const float scale = px / font->header.em_size;
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

das::float2 UiSystem::Measure(const char* utf8, float px, uint64_t font_id) const {
    const Font* font = FontById(font_id);
    if (font == nullptr || utf8 == nullptr || px <= 0.0f)
        return das::float2(0.0f, 0.0f);

    float width = 0.0f;
    const char* cursor = utf8;
    while (*cursor != '\0') {
        uint32_t codepoint = 0;
        cursor = NextCodepoint(cursor, codepoint);
        width += Advance(font, codepoint, px);
    }
    return das::float2(width, LineHeight(font_id, px));
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
