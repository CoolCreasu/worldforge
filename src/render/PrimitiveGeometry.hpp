#pragma once

#include "render/EditorCamera.hpp"

#include <cstdint>
#include <vector>

namespace wf::render::detail {

struct Vertex final {
    float x;
    float y;
    float z;
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;
    float normal_x;
    float normal_y;
    float normal_z;
};

static_assert(sizeof(Vertex) == 28, "GPU vertex layout must remain 16 bytes");
inline constexpr std::uint32_t kCubeVertexCount = 36;

} // namespace wf::render::detail

namespace wf::render {

struct SceneViewport final {
    float x = 0.0f;
    float y = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
};

// Build the editor grid in clip space. CPU projection is intentional: it keeps
// grid-line thickness stable in pixels while supplying a view-space ground normal.
void build_grid_vertices(std::vector<detail::Vertex>& vertices,
                         const EditorCamera& camera,
                         float window_width,
                         float window_height);

// Append the unit-cube mesh in local space. The GPU applies each object's
// model transform and the camera view/projection matrices.
void append_unit_cube_vertices(std::vector<detail::Vertex>& vertices, bool selected);

// The scene is rendered inside the client area left open by editor chrome.
[[nodiscard]] SceneViewport make_scene_viewport(float window_width, float window_height) noexcept;

} // namespace wf::render
