#include "render/PrimitiveRenderer.hpp"

#include "render/PrimitiveGeometry.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace wf::render {
namespace {
constexpr std::uint32_t kInitialVertexCapacity = 65'536;
constexpr std::uint32_t kMaximumVertexCapacity = 1'500'000;
constexpr std::size_t kUploadBufferCount = 3;
using detail::Vertex;
static_assert(sizeof(Vertex) == 16);

[[nodiscard]] SDL_GPUShader* load_shader(SDL_GPUDevice* device, const char* path,
                                         SDL_GPUShaderStage stage, SDL_GPUShaderFormat format,
                                         const char* entrypoint) {
    std::size_t code_size = 0;
    void* data = SDL_LoadFile(path, &code_size);
    if (!data) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Could not load shader '%s': %s", path, SDL_GetError());
        return nullptr;
    }
    SDL_GPUShaderCreateInfo info{};
    info.code = static_cast<const Uint8*>(data);
    info.code_size = code_size;
    info.entrypoint = entrypoint;
    info.format = format;
    info.stage = stage;
    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);
    SDL_free(data);
    if (!shader) SDL_LogError(SDL_LOG_CATEGORY_GPU, "Could not create shader '%s': %s", path, SDL_GetError());
    return shader;
}
} // namespace

struct PrimitiveRenderer::Impl final {
    SDL_GPUDevice* device = nullptr;
    SDL_Window* window = nullptr;
    SDL_GPUShaderFormat shader_format = SDL_GPU_SHADERFORMAT_INVALID;
    const char* shader_entrypoint = "main";
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUBuffer* vertex_buffer = nullptr;
    std::array<SDL_GPUTransferBuffer*, kUploadBufferCount> upload_buffers{};
    std::size_t frame_index = 0;
    std::uint32_t vertex_capacity = 0;
    SDL_GPUTexture* depth_texture = nullptr;
    std::uint32_t depth_width = 0;
    std::uint32_t depth_height = 0;
};

PrimitiveRenderer::PrimitiveRenderer() = default;
PrimitiveRenderer::~PrimitiveRenderer() { shutdown(); }

bool PrimitiveRenderer::initialize(SDL_GPUDevice* device, SDL_Window* window, std::string& error) {
    shutdown();
    impl_ = std::make_unique<Impl>();
    impl_->device = device;
    impl_->window = window;
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
    if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0) {
        impl_->shader_format = SDL_GPU_SHADERFORMAT_SPIRV;
        impl_->shader_entrypoint = "main";
    } else if ((formats & SDL_GPU_SHADERFORMAT_MSL) != 0) {
        impl_->shader_format = SDL_GPU_SHADERFORMAT_MSL;
        impl_->shader_entrypoint = "main0";
    } else {
        error = "This build includes SPIR-V and MSL shaders only; no compatible shader format was found.";
        return false;
    }

    const char* vertex_path = impl_->shader_format == SDL_GPU_SHADERFORMAT_SPIRV
        ? "assets/shaders/Primitive.vert.spv" : "assets/shaders/Primitive.vert.msl";
    const char* fragment_path = impl_->shader_format == SDL_GPU_SHADERFORMAT_SPIRV
        ? "assets/shaders/Primitive.frag.spv" : "assets/shaders/Primitive.frag.msl";
    SDL_GPUShader* vertex_shader = load_shader(device, vertex_path, SDL_GPU_SHADERSTAGE_VERTEX,
                                                impl_->shader_format, impl_->shader_entrypoint);
    if (!vertex_shader) { error = std::string("Could not load vertex shader: ") + SDL_GetError(); return false; }
    SDL_GPUShader* fragment_shader = load_shader(device, fragment_path, SDL_GPU_SHADERSTAGE_FRAGMENT,
                                                  impl_->shader_format, impl_->shader_entrypoint);
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(device, vertex_shader);
        error = std::string("Could not load fragment shader: ") + SDL_GetError();
        return false;
    }

    SDL_GPUColorTargetDescription color_target{};
    color_target.format = SDL_GetGPUSwapchainTextureFormat(device, window);
    // SDL3 calls this color_write_mask and requires explicitly enabling it.
    color_target.blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G |
                                                SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
    color_target.blend_state.enable_color_write_mask = true;
    SDL_GPUGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.vertex_shader = vertex_shader;
    pipeline_info.fragment_shader = fragment_shader;
    pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline_info.vertex_input_state.num_vertex_buffers = 1;
    SDL_GPUVertexBufferDescription vertex_buffer_desc{};
    vertex_buffer_desc.slot = 0;
    vertex_buffer_desc.pitch = sizeof(Vertex);
    vertex_buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attributes[2]{};
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = 0;
    attributes[1].location = 1;
    attributes[1].buffer_slot = 0;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    attributes[1].offset = 12;
    pipeline_info.vertex_input_state.vertex_buffer_descriptions = &vertex_buffer_desc;
    pipeline_info.vertex_input_state.num_vertex_buffers = 1;
    pipeline_info.vertex_input_state.vertex_attributes = attributes;
    pipeline_info.vertex_input_state.num_vertex_attributes = 2;
    pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipeline_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pipeline_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pipeline_info.depth_stencil_state.enable_depth_test = true;
    pipeline_info.depth_stencil_state.enable_depth_write = true;
    pipeline_info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    pipeline_info.target_info.color_target_descriptions = &color_target;
    pipeline_info.target_info.num_color_targets = 1;
    pipeline_info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    pipeline_info.target_info.has_depth_stencil_target = true;
    impl_->pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipeline_info);
    SDL_ReleaseGPUShader(device, vertex_shader);
    SDL_ReleaseGPUShader(device, fragment_shader);
    if (!impl_->pipeline) {
        error = std::string("Could not create primitive graphics pipeline: ") + SDL_GetError();
        return false;
    }

    if (!ensure_vertex_capacity(kInitialVertexCapacity, error)) return false;
    error.clear();
    return true;
}

bool PrimitiveRenderer::ensure_vertex_capacity(std::uint32_t required, std::string& error) {
    if (!impl_) { error = "Renderer is not initialized"; return false; }
    if (required <= impl_->vertex_capacity) return true;
    if (required > kMaximumVertexCapacity) {
        error = "Scene produces too many primitive vertices for this editor prototype";
        return false;
    }
    std::uint32_t capacity = std::max(impl_->vertex_capacity, kInitialVertexCapacity);
    while (capacity < required) {
        if (capacity > kMaximumVertexCapacity / 2) { capacity = kMaximumVertexCapacity; break; }
        capacity *= 2;
    }
    if (capacity < required) { error = "Vertex buffer capacity limit exceeded"; return false; }
    (void)SDL_WaitForGPUIdle(impl_->device);
    if (impl_->vertex_buffer) SDL_ReleaseGPUBuffer(impl_->device, impl_->vertex_buffer);
    impl_->vertex_buffer = nullptr;
    for (std::size_t i = 0; i < impl_->upload_buffers.size(); ++i) {
        SDL_GPUTransferBuffer*& upload = impl_->upload_buffers[i];
        if (upload) {
            SDL_ReleaseGPUTransferBuffer(impl_->device, upload);
            upload = nullptr;
        }
    }
    const Uint32 bytes = static_cast<Uint32>(static_cast<std::uint64_t>(capacity) * sizeof(Vertex));
    SDL_GPUBufferCreateInfo buffer_info{};
    buffer_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    buffer_info.size = bytes;
    impl_->vertex_buffer = SDL_CreateGPUBuffer(impl_->device, &buffer_info);
    if (!impl_->vertex_buffer) { error = std::string("Could not create vertex buffer: ") + SDL_GetError(); return false; }
    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = bytes;
    for (std::size_t i = 0; i < impl_->upload_buffers.size(); ++i) {
        impl_->upload_buffers[i] = SDL_CreateGPUTransferBuffer(impl_->device, &transfer_info);
        if (!impl_->upload_buffers[i]) {
            error = std::string("Could not create upload buffer: ") + SDL_GetError();
            return false;
        }
    }
    impl_->vertex_capacity = capacity;
    impl_->frame_index = 0;
    error.clear();
    return true;
}

bool PrimitiveRenderer::prepare(std::uint32_t width, std::uint32_t height,
                                std::size_t object_count, std::string& error) {
    if (!impl_) { error = "Renderer is not initialized"; return false; }
    if (width == 0 || height == 0) { error.clear(); return true; }
    const std::uint64_t estimated = static_cast<std::uint64_t>(object_count) * 36u + 4'096u;
    if (estimated > kMaximumVertexCapacity) {
        error = "This prototype can draw at most about 38,000 unit cubes at once";
        return false;
    }
    if (!ensure_vertex_capacity(static_cast<std::uint32_t>(estimated), error)) return false;
    if (impl_->depth_texture && impl_->depth_width == width && impl_->depth_height == height) {
        error.clear(); return true;
    }
    (void)SDL_WaitForGPUIdle(impl_->device);
    if (impl_->depth_texture) SDL_ReleaseGPUTexture(impl_->device, impl_->depth_texture);
    impl_->depth_texture = nullptr;
    SDL_GPUTextureCreateInfo depth_info{};
    depth_info.type = SDL_GPU_TEXTURETYPE_2D;
    depth_info.format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    depth_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    depth_info.width = width;
    depth_info.height = height;
    depth_info.layer_count_or_depth = 1;
    depth_info.num_levels = 1;
    depth_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    impl_->depth_texture = SDL_CreateGPUTexture(impl_->device, &depth_info);
    if (!impl_->depth_texture) {
        error = std::string("Could not create depth texture: ") + SDL_GetError();
        return false;
    }
    impl_->depth_width = width;
    impl_->depth_height = height;
    error.clear();
    return true;
}

bool PrimitiveRenderer::render(SDL_GPUCommandBuffer* command_buffer,
                               SDL_GPUTexture* swapchain_texture,
                               std::uint32_t width, std::uint32_t height,
                               const world::SceneDocument& scene,
                               world::ObjectId selected_object,
                               const EditorCamera& camera,
                               std::string& error) {
    if (!impl_ || !command_buffer || !swapchain_texture || !impl_->depth_texture) {
        error = "Renderer is missing a required GPU resource"; return false;
    }
    std::vector<Vertex> vertices;
    const std::size_t reserved = scene.objects.size() * 36u + 65'536u;
    vertices.reserve(std::min<std::size_t>(reserved, impl_->vertex_capacity));
    build_primitive_vertices(vertices, scene, selected_object, camera,
                             static_cast<float>(width), static_cast<float>(height));
    if (vertices.size() > impl_->vertex_capacity) {
        error = "Generated geometry exceeded preallocated vertex capacity"; return false;
    }
    if (vertices.empty()) { error.clear(); return true; }

    SDL_GPUTransferBuffer* upload = impl_->upload_buffers[impl_->frame_index++ % kUploadBufferCount];
    void* mapped = SDL_MapGPUTransferBuffer(impl_->device, upload, false);
    if (!mapped) { error = std::string("Could not map upload buffer: ") + SDL_GetError(); return false; }
    const std::size_t byte_count = vertices.size() * sizeof(Vertex);
    std::memcpy(mapped, vertices.data(), byte_count);
    SDL_UnmapGPUTransferBuffer(impl_->device, upload);

    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    if (!copy_pass) { error = std::string("Could not begin GPU copy pass: ") + SDL_GetError(); return false; }
    SDL_GPUTransferBufferLocation source{};
    source.transfer_buffer = upload;
    source.offset = 0;
    SDL_GPUBufferRegion destination{};
    destination.buffer = impl_->vertex_buffer;
    destination.offset = 0;
    destination.size = static_cast<Uint32>(byte_count);
    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, true);
    SDL_EndGPUCopyPass(copy_pass);

    SDL_GPUColorTargetInfo color_target{};
    color_target.texture = swapchain_texture;
    color_target.clear_color = SDL_FColor{0.055f, 0.067f, 0.085f, 1.0f};
    color_target.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth_target{};
    depth_target.texture = impl_->depth_texture;
    depth_target.clear_depth = 1.0f;
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, &depth_target);
    if (!pass) { error = std::string("Could not begin GPU render pass: ") + SDL_GetError(); return false; }
    SDL_GPUViewport viewport{};
    viewport.x = 0.0f; viewport.y = 0.0f;
    viewport.w = static_cast<float>(width); viewport.h = static_cast<float>(height);
    viewport.min_depth = 0.0f; viewport.max_depth = 1.0f;
    SDL_SetGPUViewport(pass, &viewport);
    SDL_BindGPUGraphicsPipeline(pass, impl_->pipeline);
    SDL_GPUBufferBinding binding{};
    binding.buffer = impl_->vertex_buffer;
    binding.offset = 0;
    SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
    SDL_DrawGPUPrimitives(pass, static_cast<Uint32>(vertices.size()), 1, 0, 0);
    SDL_EndGPURenderPass(pass);
    error.clear();
    return true;
}

void PrimitiveRenderer::shutdown() noexcept {
    if (!impl_) return;
    if (impl_->device) {
        (void)SDL_WaitForGPUIdle(impl_->device);
        if (impl_->depth_texture) SDL_ReleaseGPUTexture(impl_->device, impl_->depth_texture);
        for (std::size_t i = 0; i < impl_->upload_buffers.size(); ++i) {
            if (impl_->upload_buffers[i]) {
                SDL_ReleaseGPUTransferBuffer(impl_->device, impl_->upload_buffers[i]);
                impl_->upload_buffers[i] = nullptr;
            }
        }
        if (impl_->vertex_buffer) SDL_ReleaseGPUBuffer(impl_->device, impl_->vertex_buffer);
        if (impl_->pipeline) SDL_ReleaseGPUGraphicsPipeline(impl_->device, impl_->pipeline);
    }
    impl_.reset();
}

} // namespace wf::render

