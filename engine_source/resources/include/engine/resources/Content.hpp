#pragma once

#include <cstdint>

#include "engine/core/strings/Hash.hpp"

namespace tryengine::resources {

namespace assets {

constexpr uint64_t DEFAULT_PBR_SHADER = 13732156250205846749ULL;

}  // namespace assets

// Content-domain files (game/content, game/data) are addressed by their authored path:
// the path's PathId is the asset id, both in the editor and in a pak. These two entries
// are not files on disk. The build writes them into a pak next to the content, and the
// runtime reads them through the same text API as prefabs and scenes.
//
// The manifest is a newline-separated list of content paths. It exists because a pak
// has no directory listing: the runtime needs to enumerate prefabs before spawning
// them by short name. Ids are still derived from the paths, so the manifest is a list,
// not a path -> id table.
inline constexpr char kContentManifestPath[] = "tryengine/content_manifest";
inline constexpr char kStartupScenePath[] = "tryengine/startup";

inline constexpr uint64_t kContentManifestId = core::PathId(eastl::string_view(kContentManifestPath));
inline constexpr uint64_t kStartupSceneId = core::PathId(eastl::string_view(kStartupScenePath));

}  // namespace tryengine::resources
