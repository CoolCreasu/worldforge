#pragma once

#include "world/SceneDocument.hpp"
#include "render/EditorCamera.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace wf::render {

struct ProjectedPoint final {
    float x = 0.0f;
    float y = 0.0f;
    float depth = 1.0f;
    bool visible = false;
};

class PrimitiveRenderer final {
public:
    // Defined out-of-line so MSVC never instantiates unique_ptr<Impl> cleanup
    // while Impl is still incomplete (notably in /std:c++latest).
    PrimitiveRenderer();
    ~PrimitiveRenderer();

    PrimitiveRenderer(const PrimitiveRenderer&) = delete;
    PrimitiveRenderer& operator=(const PrimitiveRenderer&) = delete;

    [[nodiscard]] bool initialize(SDL_GPUDevice* device, SDL_Window* window, std::string& error);
    void shutdown() noexcept;

    [[nodiscard]] bool prepare(std::uint32_t width, std::uint32_t height,
                               std::size_t object_count, std::string& error);

    [[nodiscard]] bool render(SDL_GPUCommandBuffer* command_buffer,
                              SDL_GPUTexture* swapchain_texture,
                              std::uint32_t width,
                              std::uint32_t height,
                              const world::SceneDocument& scene,
                              world::ObjectId selected_object,
                              const EditorCamera& camera,
                              std::string& error);

    [[nodiscard]] ProjectedPoint project_to_window(double x, double y, double z,
                                                   int width, int height,
                                                   const EditorCamera& camera) const noexcept;
    [[nodiscard]] bool window_to_ground(float mouse_x, float mouse_y, int width, int height,
                                        const EditorCamera& camera, double& world_x,
                                        double& world_z) const noexcept;

private:
    // Grows the vertex/transfer buffers when the scene or grid needs more space.
    // Keep this declaration in the class: the implementation lives in the .cpp.
    [[nodiscard]] bool ensure_vertex_capacity(std::uint32_t required, std::string& error);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wf::render
