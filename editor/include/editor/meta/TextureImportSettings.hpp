#pragma once
#include <cereal/cereal.hpp>
#include "engine/resources/Types.hpp"

namespace tryeditor {

struct TextureImportSettings {
    tryengine::resources::TextureFilter min_filter = tryengine::resources::TextureFilter::Linear;
    tryengine::resources::TextureFilter mag_filter = tryengine::resources::TextureFilter::Linear;
    tryengine::resources::TextureAddressMode address_u = tryengine::resources::TextureAddressMode::Repeat;
    tryengine::resources::TextureAddressMode address_v = tryengine::resources::TextureAddressMode::Repeat;

    template <class Archive>
    void serialize(Archive& archive) {
        archive(cereal::make_nvp("min_filter", min_filter));
        archive(cereal::make_nvp("mag_filter", mag_filter));
        archive(cereal::make_nvp("address_u", address_u));
        archive(cereal::make_nvp("address_v", address_v));
    }
};

} // namespace tryeditor