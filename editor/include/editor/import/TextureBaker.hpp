#pragma once

#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include "engine/core/Result.hpp"

namespace tryeditor {

// .png/.jpg -> TextureBinary artifact.
//
// Settings live in the asset's .meta. The enum-like fields are strings on purpose: a .meta is
// JSON that a human edits, and the only place the names are mapped is this file, so a typo is an
// import error instead of a silently different texture. Supported values:
//   format : rgba8, rgba8_srgb, r8, rg8, bc1, bc1_srgb, bc3, bc3_srgb, bc7, bc7_srgb
//   filter : linear, nearest
//   wrap   : repeat, clamp, mirror
struct TextureImportSettings {
    eastl::string format = "rgba8_srgb";
    bool generate_mips = true;
    //! Clamp the longest side before mips are generated; 0 keeps the source size.
    uint32_t max_size = 0;
    eastl::string filter = "linear";
    eastl::string wrap = "repeat";
};

struct TextureBakeStats {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mip_count = 1;
    //! tryengine::resources::TextureFormat value, for logs.
    uint32_t format = 0;
    uint64_t bytes = 0;
};

struct TextureBakeOutput {
    eastl::vector<uint8_t> bytes;
    TextureBakeStats stats;
};

//! Decodes an encoded image (stb_image) and writes a TextureBinary artifact. Pure: no IO, no
//! async. The importer and tools/texture_probe.cpp share this exact path, so what the probe
//! validates is what the editor writes.
tryengine::Result<TextureBakeOutput> BakeTexture(eastl::span<const uint8_t> encoded_bytes,
                                                 const TextureImportSettings& settings);

}  // namespace tryeditor
