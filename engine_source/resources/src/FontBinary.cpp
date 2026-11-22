#include "engine/resources/binary/FontBinary.hpp"

#include <cstring>

#include "engine/core/MakeError.hpp"

namespace tryengine::resources {

namespace {

constexpr uint64_t kHeaderSize = sizeof(FontHeader);

}  // namespace

eastl::vector<uint8_t> FontBinary::Pack(const FontHeader& header, eastl::span<const FontGlyph> glyphs,
                                        eastl::span<const FontKerning> kerning, eastl::span<const uint8_t> atlas_blob) {
    FontHeader final_header = header;
    final_header.magic = kFontMagic;
    final_header.version = kFontVersion;
    final_header.glyph_count = static_cast<uint32_t>(glyphs.size());
    final_header.kerning_count = static_cast<uint32_t>(kerning.size());
    final_header.atlas_blob_size = static_cast<uint32_t>(atlas_blob.size());

    const uint64_t glyph_size = static_cast<uint64_t>(glyphs.size()) * sizeof(FontGlyph);
    const uint64_t kerning_size = static_cast<uint64_t>(kerning.size()) * sizeof(FontKerning);
    const uint64_t atlas_offset = kHeaderSize + glyph_size + kerning_size;

    eastl::vector<uint8_t> buffer(static_cast<size_t>(atlas_offset + atlas_blob.size()));
    std::memcpy(buffer.data(), &final_header, sizeof(FontHeader));
    if (!glyphs.empty())
        std::memcpy(buffer.data() + kHeaderSize, glyphs.data(), glyph_size);
    if (!kerning.empty())
        std::memcpy(buffer.data() + kHeaderSize + glyph_size, kerning.data(), kerning_size);
    if (!atlas_blob.empty())
        std::memcpy(buffer.data() + atlas_offset, atlas_blob.data(), atlas_blob.size());
    return buffer;
}

Result<UnpackedFont> FontBinary::Unpack(eastl::span<const uint8_t> raw_bytes) {
    if (raw_bytes.size() < sizeof(FontHeader))
        return LogAndMakeError("FontBinary: file is smaller than the header");

    UnpackedFont view;
    std::memcpy(&view.header, raw_bytes.data(), sizeof(FontHeader));

    if (view.header.magic != kFontMagic)
        return LogAndMakeError("FontBinary: invalid magic");
    if (view.header.version != kFontVersion)
        return LogAndMakeError("FontBinary: unsupported version {}", view.header.version);

    const uint64_t glyph_bytes = static_cast<uint64_t>(view.header.glyph_count) * sizeof(FontGlyph);
    const uint64_t kerning_bytes = static_cast<uint64_t>(view.header.kerning_count) * sizeof(FontKerning);
    const uint64_t atlas_offset = sizeof(FontHeader) + glyph_bytes + kerning_bytes;
    if (atlas_offset > raw_bytes.size() ||
        static_cast<uint64_t>(view.header.atlas_blob_size) > raw_bytes.size() - atlas_offset) {
        return LogAndMakeError("FontBinary: tables do not fit the file");
    }

    view.glyphs.resize(view.header.glyph_count);
    if (glyph_bytes > 0)
        std::memcpy(view.glyphs.data(), raw_bytes.data() + sizeof(FontHeader), glyph_bytes);

    view.kerning.resize(view.header.kerning_count);
    if (kerning_bytes > 0)
        std::memcpy(view.kerning.data(), raw_bytes.data() + sizeof(FontHeader) + glyph_bytes, kerning_bytes);

    view.atlas_blob = raw_bytes.subspan(atlas_offset, view.header.atlas_blob_size);
    return view;
}

}  // namespace tryengine::resources
