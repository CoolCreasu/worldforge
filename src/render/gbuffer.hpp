#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>

namespace wf::render {

// Owns the off-screen textures written by the geometry pass and sampled by lighting.
// The renderer chooses how to draw into these buffers; this class only manages lifetime.
class GBuffer final {
public:
    GBuffer() = default;
    ~GBuffer();

    GBuffer(const GBuffer&) = delete;
    GBuffer& operator=(const GBuffer&) = delete;

    [[nodiscard]] bool resize(SDL_GPUDevice* device, std::uint32_t width,
                              std::uint32_t height, std::string& error);
    void shutdown() noexcept;

    [[nodiscard]] SDL_GPUTexture* albedo_texture() const noexcept { return albedo_; }
    [[nodiscard]] SDL_GPUTexture* normal_texture() const noexcept { return normal_; }
    [[nodiscard]] SDL_GPUTexture* depth_texture() const noexcept { return depth_; }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] bool ready() const noexcept {
        return albedo_ != nullptr && normal_ != nullptr && depth_ != nullptr;
    }

private:
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUTexture* albedo_ = nullptr;
    SDL_GPUTexture* normal_ = nullptr;
    SDL_GPUTexture* depth_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
};

} // namespace wf::render
