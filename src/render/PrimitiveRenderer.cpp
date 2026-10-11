#include "render/PrimitiveRenderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace wf::render {
namespace {
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 1000.0f;
constexpr float kFovRadians = 0.872664626f; // 50 degrees
constexpr std::uint32_t kInitialVertexCapacity = 65'536;
constexpr std::uint32_t kMaximumVertexCapacity = 1'500'000;
constexpr std::size_t kUploadBufferCount = 3;

struct Vec3 final { float x, y, z; };
struct Color final { std::uint8_t r, g, b, a; };
struct Vertex final { float x, y, z; std::uint8_t r, g, b, a; };
static_assert(sizeof(Vertex) == 16);

[[nodiscard]] Vec3 add(Vec3 a, Vec3 b) noexcept { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
[[nodiscard]] Vec3 sub(Vec3 a, Vec3 b) noexcept { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
[[nodiscard]] Vec3 mul(Vec3 a, float s) noexcept { return {a.x*s, a.y*s, a.z*s}; }
[[nodiscard]] float dot(Vec3 a, Vec3 b) noexcept { return a.x*b.x + a.y*b.y + a.z*b.z; }
[[nodiscard]] Vec3 cross(Vec3 a, Vec3 b) noexcept {
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}
[[nodiscard]] Vec3 normalize(Vec3 v) noexcept {
    const float len = std::sqrt(std::max(dot(v, v), 1.0e-12f));
    return mul(v, 1.0f / len);
}

struct CameraBasis final {
    // Camera-relative coordinates keep rendering precise even when fixed-point
    // world positions are close to the limits of Q16.16.
    Vec3 eye{};
    Vec3 right{};
    Vec3 up{};
    Vec3 forward{};
    double origin_x = 0.0;
    double origin_y = 0.0;
    double origin_z = 0.0;
};

[[nodiscard]] CameraBasis make_camera(const EditorCamera& camera) noexcept {
    const float yaw = static_cast<float>(camera.yaw);
    const float pitch = static_cast<float>(camera.pitch);
    const float cos_pitch = std::cos(pitch);
    const Vec3 direction{cos_pitch * std::sin(yaw), std::sin(pitch), cos_pitch * std::cos(yaw)};
    // The camera basis is expressed relative to the orbit target. Absolute fixed-point
    // world coordinates are subtracted from this origin in double precision before
    // conversion to float, preserving nearby detail across the full Q16.16 range.
    const Vec3 eye = mul(direction, static_cast<float>(camera.distance));
    const Vec3 forward = mul(normalize(eye), -1.0f);
    const Vec3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
    const Vec3 up = normalize(cross(right, forward));
    return {eye, right, up, forward, camera.target_x, camera.target_y, camera.target_z};
}

struct ViewLayout final {
    float width = 1.0f;
    float height = 1.0f;
    float top = 1.0f;
    float bottom = 1.0f;
    float side = 1.0f;
    float viewport_width = 1.0f;
    float viewport_height = 1.0f;
    float ui_scale = 1.0f;
};

[[nodiscard]] ViewLayout make_layout(float width, float height) noexcept {
    ViewLayout l;
    l.width = std::max(width, 1.0f);
    l.height = std::max(height, 1.0f);
    l.ui_scale = std::clamp(l.height / 800.0f, 0.8f, 2.0f);
    l.top = 44.0f * l.ui_scale;
    l.bottom = 30.0f * l.ui_scale;
    l.side = std::min(310.0f * l.ui_scale, l.width * 0.42f);
    l.viewport_width = std::max(1.0f, l.width - l.side);
    l.viewport_height = std::max(1.0f, l.height - l.top - l.bottom);
    return l;
}

[[nodiscard]] ProjectedPoint project_point(Vec3 world, const CameraBasis& cam,
                                          const ViewLayout& layout) noexcept {
    const Vec3 relative = sub(world, cam.eye);
    const float view_x = dot(relative, cam.right);
    const float view_y = dot(relative, cam.up);
    const float view_z = dot(relative, cam.forward);
    if (!(view_z > kNearPlane) || !std::isfinite(view_z)) return {};

    const float aspect = layout.viewport_width / layout.viewport_height;
    const float f = 1.0f / std::tan(kFovRadians * 0.5f);
    const float ndc_x = (view_x * f / aspect) / view_z;
    const float ndc_y = (view_y * f) / view_z;
    const float depth = (kFarPlane / (kFarPlane - kNearPlane)) -
                        ((kFarPlane * kNearPlane) / ((kFarPlane - kNearPlane) * view_z));
    const float px = (ndc_x * 0.5f + 0.5f) * layout.viewport_width;
    const float py = layout.top + (0.5f - ndc_y * 0.5f) * layout.viewport_height;
    const float x = 2.0f * px / layout.width - 1.0f;
    // SDL_GPU standardizes the viewport so NDC y=-1 maps to the top edge.
    // SDL_GPU clip space is Y-up, while our projected window coordinates are
    // top-left-origin. Convert screen Y back to clip Y with an inversion.
    // Mapping top-of-window to NDC -1 flips the rendered view vertically.
    const float y = 1.0f - 2.0f * py / layout.height;
    const bool inside = ndc_x >= -1.5f && ndc_x <= 1.5f && ndc_y >= -1.5f && ndc_y <= 1.5f &&
                        view_z < kFarPlane;
    return {x, y, depth, inside};
}

[[nodiscard]] Vec3 rotate_by_quaternion(Vec3 v, const math::QuatQ30& q) noexcept {
    constexpr double q_scale = static_cast<double>(math::QuatQ30::one_raw);
    const float qx = static_cast<float>(static_cast<double>(q.x) / q_scale);
    const float qy = static_cast<float>(static_cast<double>(q.y) / q_scale);
    const float qz = static_cast<float>(static_cast<double>(q.z) / q_scale);
    const float qw = static_cast<float>(static_cast<double>(q.w) / q_scale);
    const Vec3 qv{qx, qy, qz};
    const Vec3 t = mul(cross(qv, v), 2.0f);
    return add(v, add(mul(t, qw), cross(qv, t)));
}

[[nodiscard]] Vec3 object_point(const world::ObjectInstance& object, Vec3 local,
                                const CameraBasis& camera) noexcept {
    local.x *= static_cast<float>(object.transform.scale.x.to_double());
    local.y *= static_cast<float>(object.transform.scale.y.to_double());
    local.z *= static_cast<float>(object.transform.scale.z.to_double());
    local = rotate_by_quaternion(local, object.transform.rotation);
    // Subtract the camera's world origin in double precision before converting to float.
    return add(local, {
        static_cast<float>(object.transform.position.x.to_double() - camera.origin_x),
        static_cast<float>(object.transform.position.y.to_double() - camera.origin_y),
        static_cast<float>(object.transform.position.z.to_double() - camera.origin_z)});
}

void append_triangle(std::vector<Vertex>& vertices, Vec3 a, Vec3 b, Vec3 c, Color color,
                     const CameraBasis& camera, const ViewLayout& layout) {
    // Triangles crossing the near plane are omitted in this first editor renderer.
    // That is visually preferable to feeding huge projected coordinates to the GPU.
    const Vec3 rel_a = sub(a, camera.eye), rel_b = sub(b, camera.eye), rel_c = sub(c, camera.eye);
    if (dot(rel_a, camera.forward) <= kNearPlane || dot(rel_b, camera.forward) <= kNearPlane ||
        dot(rel_c, camera.forward) <= kNearPlane) return;
    const ProjectedPoint pa = project_point(a, camera, layout);
    const ProjectedPoint pb = project_point(b, camera, layout);
    const ProjectedPoint pc = project_point(c, camera, layout);
    vertices.push_back({pa.x, pa.y, pa.depth, color.r, color.g, color.b, color.a});
    vertices.push_back({pb.x, pb.y, pb.depth, color.r, color.g, color.b, color.a});
    vertices.push_back({pc.x, pc.y, pc.depth, color.r, color.g, color.b, color.a});
}

void append_world_quad(std::vector<Vertex>& vertices, Vec3 a, Vec3 b, Vec3 c, Vec3 d,
                       Color color, const CameraBasis& camera, const ViewLayout& layout) {
    append_triangle(vertices, a, b, c, color, camera, layout);
    append_triangle(vertices, a, c, d, color, camera, layout);
}

void append_grid(std::vector<Vertex>& vertices, const CameraBasis& camera, const ViewLayout& layout) {
    constexpr float extent = 24.0f;
    constexpr float half_width = 0.008f;
    const double origin_grid_x = std::floor(camera.origin_x);
    const double origin_grid_z = std::floor(camera.origin_z);
    const float relative_ground_y = static_cast<float>(-camera.origin_y - 0.015);
    for (int i = -24; i <= 24; ++i) {
        const float x = static_cast<float>(origin_grid_x + static_cast<double>(i) - camera.origin_x);
        const float z = static_cast<float>(origin_grid_z + static_cast<double>(i) - camera.origin_z);
        const bool major = (static_cast<int>(origin_grid_x) + i) % 5 == 0;
        Color color = major ? Color{51, 63, 78, 255} : Color{37, 46, 58, 255};
        if (static_cast<int>(origin_grid_x) + i == 0) color = {95, 67, 63, 255};
        append_world_quad(vertices,
            {x-half_width, relative_ground_y, -extent}, {x+half_width, relative_ground_y, -extent},
            {x+half_width, relative_ground_y, extent}, {x-half_width, relative_ground_y, extent}, color, camera, layout);
        Color z_color = (((static_cast<int>(origin_grid_z) + i) % 5) == 0)
            ? Color{51, 63, 78, 255} : Color{37, 46, 58, 255};
        if (static_cast<int>(origin_grid_z) + i == 0) z_color = {53, 81, 112, 255};
        append_world_quad(vertices,
            {-extent, relative_ground_y + 0.001f, z-half_width}, {extent, relative_ground_y + 0.001f, z-half_width},
            {extent, relative_ground_y + 0.001f, z+half_width}, {-extent, relative_ground_y + 0.001f, z+half_width}, z_color, camera, layout);
    }
}

void append_cube(std::vector<Vertex>& vertices, const world::ObjectInstance& object,
                 bool selected, const CameraBasis& camera, const ViewLayout& layout) {
    // A unit cube with its origin on the bottom face, so y=0 means resting on the grid.
    static constexpr Vec3 faces[6][4] = {
        {{-0.5f,1.0f,-0.5f},{0.5f,1.0f,-0.5f},{0.5f,1.0f,0.5f},{-0.5f,1.0f,0.5f}},
        {{-0.5f,0.0f,0.5f},{0.5f,0.0f,0.5f},{0.5f,0.0f,-0.5f},{-0.5f,0.0f,-0.5f}},
        {{-0.5f,0.0f,-0.5f},{0.5f,0.0f,-0.5f},{0.5f,1.0f,-0.5f},{-0.5f,1.0f,-0.5f}},
        {{0.5f,0.0f,0.5f},{-0.5f,0.0f,0.5f},{-0.5f,1.0f,0.5f},{0.5f,1.0f,0.5f}},
        {{-0.5f,0.0f,0.5f},{-0.5f,0.0f,-0.5f},{-0.5f,1.0f,-0.5f},{-0.5f,1.0f,0.5f}},
        {{0.5f,0.0f,-0.5f},{0.5f,0.0f,0.5f},{0.5f,1.0f,0.5f},{0.5f,1.0f,-0.5f}}
    };
    static constexpr Color shades[6] = {
        {94, 158, 230, 255}, {33, 48, 65, 255}, {65, 125, 188, 255},
        {50, 101, 156, 255}, {45, 88, 137, 255}, {72, 135, 198, 255}
    };
    for (std::size_t fi = 0; fi < 6; ++fi) {
        Color color = shades[fi];
        if (selected) {
            const auto lift = [](std::uint8_t base) {
                return static_cast<std::uint8_t>(std::min(255, static_cast<int>(base) / 2 + 128));
            };
            color = {lift(color.r), lift(color.g), static_cast<std::uint8_t>(std::min(255, color.b + 24)), 255};
        }
        const Vec3 a = object_point(object, faces[fi][0], camera);
        const Vec3 b = object_point(object, faces[fi][1], camera);
        const Vec3 c = object_point(object, faces[fi][2], camera);
        const Vec3 d = object_point(object, faces[fi][3], camera);
        append_world_quad(vertices, a, b, c, d, color, camera, layout);
    }
}

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

ProjectedPoint PrimitiveRenderer::project_to_window(double x, double y, double z,
                                                     int width, int height,
                                                     const EditorCamera& camera) const noexcept {
    if (width <= 0 || height <= 0) return {};
    const auto layout = make_layout(static_cast<float>(width), static_cast<float>(height));
    const auto basis = make_camera(camera);
    const auto p = project_point({static_cast<float>(x-camera.target_x),
                                  static_cast<float>(y-camera.target_y),
                                  static_cast<float>(z-camera.target_z)},basis,layout);
    // UI and picking coordinates are in top-left-origin window points.
    const float screen_x = (p.x+1.0f)*0.5f*static_cast<float>(width);
    const float screen_y = (p.y+1.0f)*0.5f*static_cast<float>(height);
    return {screen_x,screen_y,p.depth,p.visible};
}

bool PrimitiveRenderer::window_to_ground(float mouse_x, float mouse_y, int width, int height,
                                         const EditorCamera& camera, double& world_x,
                                         double& world_z) const noexcept {
    if (width <= 0 || height <= 0 || !std::isfinite(mouse_x) || !std::isfinite(mouse_y)) return false;
    const auto layout = make_layout(static_cast<float>(width), static_cast<float>(height));
    if (mouse_x < 0.0f || mouse_x >= layout.viewport_width ||
        mouse_y < layout.top || mouse_y >= static_cast<float>(height) - layout.bottom) return false;
    const auto basis = make_camera(camera);
    const float aspect = layout.viewport_width / layout.viewport_height;
    const float f = 1.0f / std::tan(kFovRadians * 0.5f);
    const float ndc_x = 2.0f * mouse_x / layout.viewport_width - 1.0f;
    const float ndc_y = 1.0f - 2.0f * (mouse_y - layout.top) / layout.viewport_height;
    const Vec3 ray = normalize(add(basis.forward,
        add(mul(basis.right, ndc_x * aspect / f), mul(basis.up, ndc_y / f))));
    if (std::abs(ray.y) < 1.0e-5f) return false;
    const float ground_y = static_cast<float>(-basis.origin_y);
    const float t = (ground_y - basis.eye.y) / ray.y;
    if (!(t > 0.0f) || !std::isfinite(t)) return false;
    const Vec3 point = add(basis.eye, mul(ray, t));
    if (!std::isfinite(point.x) || !std::isfinite(point.z)) return false;
    world_x = basis.origin_x + static_cast<double>(point.x);
    world_z = basis.origin_z + static_cast<double>(point.z);
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
    const ViewLayout layout = make_layout(static_cast<float>(width), static_cast<float>(height));
    const CameraBasis basis = make_camera(camera);
    std::vector<Vertex> vertices;
    const std::size_t reserved = scene.objects.size()*36u + 65'536u;
    vertices.reserve(std::min<std::size_t>(reserved, impl_->vertex_capacity));
    append_grid(vertices, basis, layout);
    for (const auto& object : scene.objects) append_cube(vertices, object, object.id == selected_object, basis, layout);
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
