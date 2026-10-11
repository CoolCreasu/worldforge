#pragma once

#include "math/transform.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace wf::world {

using ObjectId = std::uint64_t;

struct ObjectInstance final {
    ObjectId id = 0;
    std::string prefab; // Stable asset key, not a machine-specific absolute path.
    std::string layer = "Default";
    math::Transform transform{};

    friend bool operator==(const ObjectInstance&, const ObjectInstance&) = default;
};

struct SceneDocument final {
    static constexpr std::uint32_t current_format_version = 1;

    std::uint32_t format_version = current_format_version;
    std::string name = "Untitled";
    std::vector<ObjectInstance> objects;

    [[nodiscard]] const ObjectInstance* find(ObjectId id) const noexcept {
        for (const auto& object : objects) {
            if (object.id == id) return &object;
        }
        return nullptr;
    }

    [[nodiscard]] ObjectInstance* find(ObjectId id) noexcept {
        for (auto& object : objects) {
            if (object.id == id) return &object;
        }
        return nullptr;
    }
};

} // namespace wf::world
