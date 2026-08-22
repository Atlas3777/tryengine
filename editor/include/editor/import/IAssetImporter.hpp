#pragma once

#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include "engine/async/Task.hpp"

namespace tryeditor {

enum class ArtifactTarget : uint8_t { Runtime, Editor };

struct ProducedArtifact {
    uint64_t sub_guid{0};
    ArtifactTarget target{ArtifactTarget::Runtime};
    eastl::string extension;
    eastl::vector<uint8_t> bytes;
};

struct ImportResult {
    uint64_t main_guid{0};
    eastl::vector<ProducedArtifact> artifacts;
    eastl::vector<uint8_t> meta_bytes;
};

struct ImportContext {
    const eastl::span<const uint8_t> source_bytes;
    const eastl::span<const uint8_t> meta_bytes;
};

class IAssetImporter {
public:
    virtual ~IAssetImporter() = default;

    [[nodiscard]] virtual tryengine::async::Task<ImportResult> Import(ImportContext ctx) const = 0;

    virtual eastl::string_view GetName() const = 0;
};

}  // namespace tryeditor