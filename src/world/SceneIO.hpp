#pragma once

#include "world/SceneDocument.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace wf::world {

// Returns false with a human-readable error; on load failure, the destination
// document is left unchanged.
[[nodiscard]] bool load_scene(const std::filesystem::path& path,
                              SceneDocument& destination,
                              std::string& error);

// Writes a temporary file and promotes it into place. The on-disk format is
// deliberately simple, versioned, and diff-friendly; strings use std::quoted.
[[nodiscard]] bool save_scene(const std::filesystem::path& path,
                              const SceneDocument& scene,
                              std::string& error);

} // namespace wf::world
