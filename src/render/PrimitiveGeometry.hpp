#pragma once

#include "render/PrimitiveRenderer.hpp"

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
};

static_assert(sizeof(Vertex) == 16, "GPU vertex layout must remain 16 bytes");

} // namespace wf::render::detail

namespace wf::render {

// CPU-side primitive mesh generation is isolated from SDL_GPU resource management.
// The prototype currently generates the grid and cube vertices in clip space.
void build_primitive_vertices(std::vector<detail::Vertex>& vertices,
                              const world::SceneDocument& scene,
                              world::ObjectId selected_object,
                              const EditorCamera& camera,
                              float width,
                              float height);

} // namespace wf::render
