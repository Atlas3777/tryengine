#pragma once

#include <EASTL/span.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

#include "engine/async/Task.hpp"
#include "engine/core/MakeError.hpp"
#include "engine/graphics/RuntimeTypes.hpp"
#include "engine/graphics/VulkanDevice.hpp"
#include "engine/graphics/loaders/TextureLoader.hpp"
#include "engine/resources/binary/FontBinary.hpp"

namespace tryengine::graphics {

//! Loads a FontBinary artifact (.font) into a Font: metrics stay on the CPU, the nested
//! TextureBinary goes through TextureLoader so the atlas upload is exactly the texture path.
class FontLoader {
public:
    explicit FontLoader(VulkanDevice& vulkan_device) : vulkan_device_(vulkan_device) {}

    async::Task<Font> Parse(eastl::span<const uint8_t> data) const {
        auto unpacked = resources::FontBinary::Unpack(data);
        if (!unpacked.has_value())
            co_return LogAndMakeError("Font unpack failed: {}", unpacked.error().Message());

        co_await async::ExecutorSwitch(async::MainThread());

        Font font;
        font.header = unpacked->header;
        font.glyphs = eastl::move(unpacked->glyphs);
        font.codepoint_to_glyph.reserve(font.glyphs.size());
        for (uint32_t i = 0; i < font.glyphs.size(); ++i)
            font.codepoint_to_glyph[font.glyphs[i].codepoint] = i;

        auto atlas = co_await TextureLoader(vulkan_device_).Parse(unpacked->atlas_blob);
        if (!atlas.has_value())
            co_return LogAndMakeError("Font atlas upload failed: {}", atlas.error().Message());

        font.atlas = eastl::move(*atlas);
        co_return font;
    }

private:
    VulkanDevice& vulkan_device_;
};

}  // namespace tryengine::graphics
