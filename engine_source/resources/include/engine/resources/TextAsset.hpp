#pragma once

#include <EASTL/span.h>
#include <EASTL/string.h>

#include "engine/async/Task.hpp"
#include "engine/core/MakeError.hpp"

namespace tryengine::resources {

// Raw text of a content file (prefab, scene, material, ...). This is the only way the
// scripts read content: the loader gets the bytes from AssetRegistry/AsyncFileManager,
// so the same code works for loose files in the editor and for pak chunks in the game.
struct TextAsset {
    eastl::string text;
};

class TextAssetLoader {
public:
    async::Task<TextAsset> Parse(eastl::span<const uint8_t> bytes) const {
        if (bytes.empty())
            co_return core::Error("Text asset is empty");

        co_return TextAsset{eastl::string(reinterpret_cast<const char*>(bytes.data()), bytes.size())};
    }
};

}  // namespace tryengine::resources
