#pragma once

#include "render/EditorCamera.hpp"
#include "world/SceneDocument.hpp"

#include <array>
#include <cstddef>

namespace wf::render::detail {

// Column-major storage matches GLSL mat4 and Metal float4x4 uniform layout.
struct Matrix4x4 final {
    std::array<float, 16> elements{};

    [[nodiscard]] float& at(std::size_t row, std::size_t column) noexcept {
        return elements[column * 4 + row];
    }
    [[nodiscard]] float at(std::size_t row, std::size_t column) const noexcept {
        return elements[column * 4 + row];
    }
};
static_assert(sizeof(Matrix4x4) == 64);

[[nodiscard]] Matrix4x4 make_model_matrix(const world::ObjectInstance& object,
                                           const EditorCamera& camera) noexcept;
[[nodiscard]] Matrix4x4 make_view_matrix(const EditorCamera& camera) noexcept;
[[nodiscard]] Matrix4x4 make_projection_matrix(float aspect_ratio) noexcept;

} // namespace wf::render::detail
