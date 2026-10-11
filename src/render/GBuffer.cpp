#include "render/GBuffer.hpp"

namespace wf::render {
namespace {
constexpr SDL_GPUTextureFormat kColorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
constexpr SDL_GPUTextureFormat kDepthFormat = SDL_GPU_TEXTUREFORMAT_D16_UNORM;

[[nodiscard]] SDL_GPUTexture* create_texture(SDL_GPUDevice* device,
                                             SDL_GPUTextureFormat format,
                                             SDL_GPUTextureUsageFlags usage,
                                             std::uint32_t width,
                                             std::uint32_t height) {
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = format;
    info.usage = usage;
    info.width = width;
    info.height = height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    return SDL_CreateGPUTexture(device, &info);
}
} // namespace

GBuffer::~GBuffer() {
    shutdown();
}

bool GBuffer::resize(SDL_GPUDevice* device, std::uint32_t width,
                     std::uint32_t height, std::string& error) {
    if (!device) {
        error = "Cannot create the geometry buffer without a GPU device.";
        return false;
    }
    if (width == 0 || height == 0) {
        error.clear();
        return true;
    }
    if (device_ == device && width_ == width && height_ == height && ready()) {
        error.clear();
        return true;
    }

    constexpr SDL_GPUTextureUsageFlags color_usage =
        SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    constexpr SDL_GPUTextureUsageFlags depth_usage =
        SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;

    if (!SDL_GPUTextureSupportsFormat(device, kColorFormat, SDL_GPU_TEXTURETYPE_2D, color_usage)) {
        error = "The GPU does not support a sampleable RGBA8 color target for the geometry buffer.";
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(device, kDepthFormat, SDL_GPU_TEXTURETYPE_2D, depth_usage)) {
        error = "The GPU does not support a sampleable D16 depth target for the geometry buffer.";
        return false;
    }

    SDL_GPUTexture* new_albedo = create_texture(device, kColorFormat, color_usage, width, height);
    if (!new_albedo) {
        error = std::string("Could not create albedo target: ") + SDL_GetError();
        return false;
    }
    SDL_GPUTexture* new_normal = create_texture(device, kColorFormat, color_usage, width, height);
    if (!new_normal) {
        SDL_ReleaseGPUTexture(device, new_albedo);
        error = std::string("Could not create normal target: ") + SDL_GetError();
        return false;
    }
    SDL_GPUTexture* new_depth = create_texture(device, kDepthFormat, depth_usage, width, height);
    if (!new_depth) {
        SDL_ReleaseGPUTexture(device, new_normal);
        SDL_ReleaseGPUTexture(device, new_albedo);
        error = std::string("Could not create sampleable depth target: ") + SDL_GetError();
        return false;
    }

    // Build all replacements first. If allocation fails the currently active
    // buffers remain usable, avoiding a half-resized renderer.
    if (device_) {
        (void)SDL_WaitForGPUIdle(device_);
        if (albedo_) SDL_ReleaseGPUTexture(device_, albedo_);
        if (normal_) SDL_ReleaseGPUTexture(device_, normal_);
        if (depth_) SDL_ReleaseGPUTexture(device_, depth_);
    }
    device_ = device;
    albedo_ = new_albedo;
    normal_ = new_normal;
    depth_ = new_depth;
    width_ = width;
    height_ = height;
    error.clear();
    return true;
}

void GBuffer::shutdown() noexcept {
    if (device_) {
        (void)SDL_WaitForGPUIdle(device_);
        if (albedo_) SDL_ReleaseGPUTexture(device_, albedo_);
        if (normal_) SDL_ReleaseGPUTexture(device_, normal_);
        if (depth_) SDL_ReleaseGPUTexture(device_, depth_);
    }
    device_ = nullptr;
    albedo_ = nullptr;
    normal_ = nullptr;
    depth_ = nullptr;
    width_ = 0;
    height_ = 0;
}

} // namespace wf::render
