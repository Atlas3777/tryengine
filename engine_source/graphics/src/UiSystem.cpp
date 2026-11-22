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
    if (config_.font_guid == 0) {
        if (!font_failed_logged_) {
            font_failed_logged_ = true;
            LogWarn(LogCategory::Graphics,
                    "UiSystem: UiConfig.font_guid is 0; import a .ttf and put its guid in UiConfig/kDefaultUiFontGuid "
                    "to enable the game UI");
        }
        return;
    }

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
    if (material_ != nullptr && material_id_ != 0) {
        Material* current = resource_manager_.GetPointerExplicit<Material>(material_id_);
        if (current != material_ && current != nullptr) {
            material_ = current;
            atlas_bound_ = false;
        }
    }

    BindAtlas();
}

void UiSystem::PollFont() {
    if (!font_requested_) {
        resource_manager_.RequestLoad<Font>(config_.font_guid);
        font_requested_ = true;
        LogInfo(LogCategory::Graphics, "UI font {} requested", config_.font_guid);
        return;
    }
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
        LogError("UI font {} failed to load: check the .ttf import and UiConfig.font_guid", config_.font_guid);
    }
}

void UiSystem::PollMaterial() {
    if (material_id_ == 0) {
        const auto& registry = resource_manager_.GetAssetRegistry();
        const auto resolved = registry.ResolveRef(config_.material_path);
        if (!resolved.has_value() || *resolved == 0)
            return;  // registry may still be scanning at startup; retried next frame
        material_id_ = *resolved;
    }
    if (!material_requested_) {
        resource_manager_.RequestLoad<Material>(material_id_);
        material_requested_ = true;
        LogInfo(LogCategory::Graphics, "UI material '{}' ({}) requested", config_.material_path.c_str(), material_id_);
        return;
    }
    if (material_ != nullptr)
        return;

    if (resource_manager_.IsReady<Material>(material_id_)) {
        material_ = resource_manager_.GetPointerExplicit<Material>(material_id_);
        return;
    }
    if (resource_manager_.IsFailed<Material>(material_id_) && !material_failed_logged_) {
        material_failed_logged_ = true;
        LogError("UI material '{}' failed to load", config_.material_path.c_str());
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
