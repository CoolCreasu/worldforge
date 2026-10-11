#include "render/PrimitiveRenderer.hpp"

#include "render/PrimitiveGeometry.hpp"
#include "render/RenderTransforms.hpp"
#include "render/GBuffer.hpp"

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
static_assert(sizeof(Vertex) == 32);

[[nodiscard]] SDL_GPUShader* load_shader(SDL_GPUDevice* device, const char* path,
                                         SDL_GPUShaderStage stage, SDL_GPUShaderFormat format,
                                         const char* entrypoint,
                                         std::uint32_t uniform_buffer_count = 0,
                                         std::uint32_t sampler_count = 0) {
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
    info.num_uniform_buffers = uniform_buffer_count;
    info.num_samplers = sampler_count;
    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);
    SDL_free(data);
    if (!shader) SDL_LogError(SDL_LOG_CATEGORY_GPU, "Could not create shader '%s': %s", path, SDL_GetError());
    return shader;
}

[[nodiscard]] SDL_GPUGraphicsPipeline* create_geometry_pipeline(
    SDL_GPUDevice* device, SDL_GPUShader* vertex_shader, SDL_GPUShader* fragment_shader) {
    SDL_GPUColorTargetDescription color_targets[2]{};
    for (auto& target : color_targets) {
        target.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        target.blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G |
                                              SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
        target.blend_state.enable_color_write_mask = true;
    }

    SDL_GPUVertexBufferDescription vertex_buffer{};
    vertex_buffer.slot = 0;
    vertex_buffer.pitch = sizeof(Vertex);
    vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attributes[3]{};
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = 0;
    attributes[1].location = 1;
    attributes[1].buffer_slot = 0;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    attributes[1].offset = 12;
    attributes[2].location = 2;
    attributes[2].buffer_slot = 0;
    attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[2].offset = 16;
    attributes[3].location = 3;
    attributes[3].buffer_slot = 0;
    attributes[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
    attributes[3].offset = 28;

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex_shader;
    info.fragment_shader = fragment_shader;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.vertex_input_state.vertex_buffer_descriptions = &vertex_buffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = 4;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.depth_stencil_state.enable_depth_test = true;
    info.depth_stencil_state.enable_depth_write = true;
    info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    info.target_info.color_target_descriptions = color_targets;
    info.target_info.num_color_targets = 2;
    info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    info.target_info.has_depth_stencil_target = true;
    return SDL_CreateGPUGraphicsPipeline(device, &info);
}

[[nodiscard]] SDL_GPUGraphicsPipeline* create_composite_pipeline(
    SDL_GPUDevice* device, SDL_Window* window,
    SDL_GPUShader* vertex_shader, SDL_GPUShader* fragment_shader) {
    SDL_GPUColorTargetDescription target{};
    target.format = SDL_GetGPUSwapchainTextureFormat(device, window);
    target.blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G |
                                          SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
    target.blend_state.enable_color_write_mask = true;

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex_shader;
    info.fragment_shader = fragment_shader;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.target_info.color_target_descriptions = &target;
    info.target_info.num_color_targets = 1;
    return SDL_CreateGPUGraphicsPipeline(device, &info);
}

} // namespace

struct PrimitiveRenderer::Impl final {
    SDL_GPUDevice* device = nullptr;
    SDL_Window* window = nullptr;
    SDL_GPUShaderFormat shader_format = SDL_GPU_SHADERFORMAT_INVALID;
    const char* shader_entrypoint = "main";
    SDL_GPUGraphicsPipeline* grid_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* mesh_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* composite_pipeline = nullptr;
    SDL_GPUSampler* gbuffer_sampler = nullptr;
    GBuffer gbuffer;
    SDL_GPUBuffer* vertex_buffer = nullptr;
    std::array<SDL_GPUTransferBuffer*, kUploadBufferCount> upload_buffers{};
    std::size_t frame_index = 0;
    std::uint32_t vertex_capacity = 0;
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

    const char* grid_vertex_path = impl_->shader_format == SDL_GPU_SHADERFORMAT_SPIRV
        ? "assets/shaders/Primitive.vert.spv" : "assets/shaders/Primitive.vert.msl";
    const char* mesh_vertex_path = impl_->shader_format == SDL_GPU_SHADERFORMAT_SPIRV
        ? "assets/shaders/Mesh.vert.spv" : "assets/shaders/Mesh.vert.msl";
    const char* geometry_fragment_path = impl_->shader_format == SDL_GPU_SHADERFORMAT_SPIRV
        ? "assets/shaders/GBuffer.frag.spv" : "assets/shaders/GBuffer.frag.msl";
    const char* composite_vertex_path = impl_->shader_format == SDL_GPU_SHADERFORMAT_SPIRV
        ? "assets/shaders/Composite.vert.spv" : "assets/shaders/Composite.vert.msl";
    const char* composite_fragment_path = impl_->shader_format == SDL_GPU_SHADERFORMAT_SPIRV
        ? "assets/shaders/Composite.frag.spv" : "assets/shaders/Composite.frag.msl";

    SDL_GPUShader* grid_vertex = load_shader(
        device, grid_vertex_path, SDL_GPU_SHADERSTAGE_VERTEX, impl_->shader_format,
        impl_->shader_entrypoint);
    SDL_GPUShader* mesh_vertex = load_shader(
        device, mesh_vertex_path, SDL_GPU_SHADERSTAGE_VERTEX, impl_->shader_format,
        impl_->shader_entrypoint, 3);
    SDL_GPUShader* geometry_fragment = load_shader(
        device, geometry_fragment_path, SDL_GPU_SHADERSTAGE_FRAGMENT, impl_->shader_format,
        impl_->shader_entrypoint);
    SDL_GPUShader* composite_vertex = load_shader(
        device, composite_vertex_path, SDL_GPU_SHADERSTAGE_VERTEX, impl_->shader_format,
        impl_->shader_entrypoint);
    SDL_GPUShader* composite_fragment = load_shader(
        device, composite_fragment_path, SDL_GPU_SHADERSTAGE_FRAGMENT, impl_->shader_format,
        impl_->shader_entrypoint, 1, 3);

    if (!grid_vertex || !mesh_vertex || !geometry_fragment || !composite_vertex || !composite_fragment) {
        if (grid_vertex) SDL_ReleaseGPUShader(device, grid_vertex);
        if (mesh_vertex) SDL_ReleaseGPUShader(device, mesh_vertex);
        if (geometry_fragment) SDL_ReleaseGPUShader(device, geometry_fragment);
        if (composite_vertex) SDL_ReleaseGPUShader(device, composite_vertex);
        if (composite_fragment) SDL_ReleaseGPUShader(device, composite_fragment);
        error = std::string("Could not load renderer shaders: ") + SDL_GetError();
        return false;
    }

    impl_->grid_pipeline = create_geometry_pipeline(device, grid_vertex, geometry_fragment);
    impl_->mesh_pipeline = create_geometry_pipeline(device, mesh_vertex, geometry_fragment);
    impl_->composite_pipeline = create_composite_pipeline(
        device, window, composite_vertex, composite_fragment);

    SDL_ReleaseGPUShader(device, grid_vertex);
    SDL_ReleaseGPUShader(device, mesh_vertex);
    SDL_ReleaseGPUShader(device, geometry_fragment);
    SDL_ReleaseGPUShader(device, composite_vertex);
    SDL_ReleaseGPUShader(device, composite_fragment);

    if (!impl_->grid_pipeline || !impl_->mesh_pipeline || !impl_->composite_pipeline) {
        error = std::string("Could not create renderer pipelines: ") + SDL_GetError();
        return false;
    }

    SDL_GPUSamplerCreateInfo sampler_info{};
    sampler_info.min_filter = SDL_GPU_FILTER_NEAREST;
    sampler_info.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.min_lod = 0.0f;
    sampler_info.max_lod = 0.0f;
    impl_->gbuffer_sampler = SDL_CreateGPUSampler(device, &sampler_info);
    if (!impl_->gbuffer_sampler) {
        error = std::string("Could not create G-buffer sampler: ") + SDL_GetError();
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
    return impl_->gbuffer.resize(impl_->device, width, height, error);
}

bool PrimitiveRenderer::render(SDL_GPUCommandBuffer* command_buffer,
                               SDL_GPUTexture* swapchain_texture,
                               std::uint32_t width, std::uint32_t height,
                               const world::SceneDocument& scene,
                               world::ObjectId selected_object,
                               const EditorCamera& camera,
                               std::string& error) {
    if (!impl_ || !command_buffer || !swapchain_texture || !impl_->gbuffer.ready() ||
        !impl_->gbuffer_sampler) {
        error = "Renderer is missing a required GPU resource"; return false;
    }
    std::vector<Vertex> vertices;
    const std::size_t reserved = scene.objects.size() * detail::kCubeVertexCount + 65'536u;
    vertices.reserve(std::min<std::size_t>(reserved, impl_->vertex_capacity));
    build_grid_vertices(vertices, camera, static_cast<float>(width), static_cast<float>(height));
    const std::uint32_t grid_vertex_count = static_cast<std::uint32_t>(vertices.size());
    for (const auto& object : scene.objects) {
        append_unit_cube_vertices(vertices, object.id == selected_object);
    }
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

    SDL_GPUColorTargetInfo geometry_targets[2]{};
    geometry_targets[0].texture = impl_->gbuffer.albedo_texture();
    geometry_targets[0].clear_color = SDL_FColor{0.055f, 0.067f, 0.085f, 1.0f};
    geometry_targets[0].load_op = SDL_GPU_LOADOP_CLEAR;
    geometry_targets[0].store_op = SDL_GPU_STOREOP_STORE;
    geometry_targets[1].texture = impl_->gbuffer.normal_texture();
    geometry_targets[1].clear_color = SDL_FColor{0.5f, 0.5f, 1.0f, 0.0f};
    geometry_targets[1].load_op = SDL_GPU_LOADOP_CLEAR;
    geometry_targets[1].store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo depth_target{};
    depth_target.texture = impl_->gbuffer.depth_texture();
    depth_target.clear_depth = 1.0f;
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* geometry_pass = SDL_BeginGPURenderPass(
        command_buffer, geometry_targets, 2, &depth_target);
    if (!geometry_pass) {
        error = std::string("Could not begin G-buffer geometry pass: ") + SDL_GetError();
        return false;
    }

    SDL_GPUViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.w = static_cast<float>(width);
    viewport.h = static_cast<float>(height);
    viewport.min_depth = 0.0f;
    viewport.max_depth = 1.0f;
    SDL_SetGPUViewport(geometry_pass, &viewport);

    SDL_GPUBufferBinding binding{};
    binding.buffer = impl_->vertex_buffer;
    binding.offset = 0;
    SDL_BindGPUVertexBuffers(geometry_pass, 0, &binding, 1);

    // Grid vertices are already projected so line widths remain stable in pixels.
    SDL_BindGPUGraphicsPipeline(geometry_pass, impl_->grid_pipeline);
    if (grid_vertex_count > 0) {
        SDL_DrawGPUPrimitives(geometry_pass, grid_vertex_count, 1, 0, 0);
    }

    // Cube vertices stay local-space; GPU uniforms apply each object transform.
    if (!scene.objects.empty()) {
        const SceneViewport scene_viewport = make_scene_viewport(
            static_cast<float>(width), static_cast<float>(height));
        SDL_GPUViewport object_viewport{};
        object_viewport.x = scene_viewport.x;
        object_viewport.y = scene_viewport.y;
        object_viewport.w = scene_viewport.width;
        object_viewport.h = scene_viewport.height;
        object_viewport.min_depth = 0.0f;
        object_viewport.max_depth = 1.0f;
        SDL_SetGPUViewport(geometry_pass, &object_viewport);
        SDL_BindGPUGraphicsPipeline(geometry_pass, impl_->mesh_pipeline);

        const detail::Matrix4x4 view_matrix = detail::make_view_matrix(camera);
        const detail::Matrix4x4 projection_matrix = detail::make_projection_matrix(
            scene_viewport.width / scene_viewport.height);
        SDL_PushGPUVertexUniformData(command_buffer, 1, view_matrix.elements.data(),
                                    static_cast<Uint32>(sizeof(view_matrix)));
        SDL_PushGPUVertexUniformData(command_buffer, 2, projection_matrix.elements.data(),
                                    static_cast<Uint32>(sizeof(projection_matrix)));

        for (std::size_t object_index = 0; object_index < scene.objects.size(); ++object_index) {
            const detail::Matrix4x4 model_matrix =
                detail::make_model_matrix(scene.objects[object_index], camera);
            SDL_PushGPUVertexUniformData(command_buffer, 0, model_matrix.elements.data(),
                                        static_cast<Uint32>(sizeof(model_matrix)));
            const Uint32 first_vertex = grid_vertex_count +
                static_cast<Uint32>(object_index * detail::kCubeVertexCount);
            SDL_DrawGPUPrimitives(geometry_pass, detail::kCubeVertexCount, 1, first_vertex, 0);
        }
    }
    SDL_EndGPURenderPass(geometry_pass);

    // The composition pass reads albedo, normals, and depth. This first lighting
    // model is intentionally simple; it proves the G-buffer path before PBR.
    SDL_GPUColorTargetInfo output_target{};
    output_target.texture = swapchain_texture;
    output_target.clear_color = SDL_FColor{0.055f, 0.067f, 0.085f, 1.0f};
    output_target.load_op = SDL_GPU_LOADOP_CLEAR;
    output_target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* composition_pass = SDL_BeginGPURenderPass(
        command_buffer, &output_target, 1, nullptr);
    if (!composition_pass) {
        error = std::string("Could not begin deferred composition pass: ") + SDL_GetError();
        return false;
    }
    SDL_BindGPUGraphicsPipeline(composition_pass, impl_->composite_pipeline);

    const detail::Direction3 light_in_view_space =
        detail::transform_direction_to_view_space(camera, {-0.35f, 0.75f, 0.55f});
    alignas(16) const std::array<float, 4> light_direction_uniform{
        light_in_view_space.x, light_in_view_space.y, light_in_view_space.z, 0.0f};
    SDL_PushGPUFragmentUniformData(command_buffer, 0, light_direction_uniform.data(),
                                  static_cast<Uint32>(sizeof(light_direction_uniform)));

    SDL_GPUTextureSamplerBinding sampled_buffers[3]{};
    sampled_buffers[0].texture = impl_->gbuffer.albedo_texture();
    sampled_buffers[0].sampler = impl_->gbuffer_sampler;
    sampled_buffers[1].texture = impl_->gbuffer.normal_texture();
    sampled_buffers[1].sampler = impl_->gbuffer_sampler;
    sampled_buffers[2].texture = impl_->gbuffer.depth_texture();
    sampled_buffers[2].sampler = impl_->gbuffer_sampler;
    SDL_BindGPUFragmentSamplers(composition_pass, 0, sampled_buffers, 3);
    SDL_DrawGPUPrimitives(composition_pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(composition_pass);
    error.clear();
    return true;
}

void PrimitiveRenderer::shutdown() noexcept {
    if (!impl_) return;
    if (impl_->device) {
        (void)SDL_WaitForGPUIdle(impl_->device);
        impl_->gbuffer.shutdown();
        if (impl_->gbuffer_sampler) SDL_ReleaseGPUSampler(impl_->device, impl_->gbuffer_sampler);
        for (std::size_t i = 0; i < impl_->upload_buffers.size(); ++i) {
            if (impl_->upload_buffers[i]) {
                SDL_ReleaseGPUTransferBuffer(impl_->device, impl_->upload_buffers[i]);
                impl_->upload_buffers[i] = nullptr;
            }
        }
        if (impl_->vertex_buffer) SDL_ReleaseGPUBuffer(impl_->device, impl_->vertex_buffer);
        if (impl_->grid_pipeline) SDL_ReleaseGPUGraphicsPipeline(impl_->device, impl_->grid_pipeline);
        if (impl_->mesh_pipeline) SDL_ReleaseGPUGraphicsPipeline(impl_->device, impl_->mesh_pipeline);
        if (impl_->composite_pipeline) SDL_ReleaseGPUGraphicsPipeline(impl_->device, impl_->composite_pipeline);
    }
    impl_.reset();
}

} // namespace wf::render

