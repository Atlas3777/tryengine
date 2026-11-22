#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>

#include "engine/core/Result.hpp"
#include "engine/resources/binary/FontBinaryLayout.hpp"

namespace tryengine::resources {

// Unpacked view over a FontBinary blob. `atlas_blob` points into the input buffer: the caller
// (FontLoader) hands it to TextureLoader while the bytes are still alive.
struct UnpackedFont {
    FontHeader header;
    eastl::vector<FontGlyph> glyphs;
    eastl::vector<FontKerning> kerning;
    eastl::span<const uint8_t> atlas_blob;
};

// FontBinary is the runtime artifact of a `.ttf` asset: header + glyph table + kerning table +
// a nested TextureBinary (distance-field atlas). Pack/Unpack live here; the baker writes the
// same layout through FontHeader/FontGlyph/FontKerning from FontBinaryLayout.hpp.
class FontBinary {
public:
    static eastl::vector<uint8_t> Pack(const FontHeader& header, eastl::span<const FontGlyph> glyphs,
                                       eastl::span<const FontKerning> kerning, eastl::span<const uint8_t> atlas_blob);

    static Result<UnpackedFont> Unpack(eastl::span<const uint8_t> raw_bytes);
};

}  // namespace tryengine::resources
