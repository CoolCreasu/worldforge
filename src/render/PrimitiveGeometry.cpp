#include "render/PrimitiveGeometry.hpp"

#include "render/ViewportCoordinates.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace wf::render {
namespace {
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 1000.0f;
constexpr float kFovRadians = 0.872664626f; // 50 degrees

struct Vec3 final { float x, y, z; };
struct Color final { std::uint8_t r, g, b, a; };
using detail::Vertex;
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
    // SDL_GPU viewport coordinates use NDC Y=-1 at the top edge. Window Y is
    // top-left-origin, so the conversion must preserve top-to-top and bottom-to-bottom.
    const float y = viewport::clip_y_from_window_y(py, layout.height);
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
} // namespace

void build_primitive_vertices(std::vector<detail::Vertex>& vertices,
                              const world::SceneDocument& scene,
                              world::ObjectId selected_object,
                              const EditorCamera& camera,
                              float width,
                              float height) {
    const ViewLayout layout = make_layout(width, height);
    const CameraBasis basis = make_camera(camera);
    append_grid(vertices, basis, layout);
    for (const auto& object : scene.objects) {
        append_cube(vertices, object, object.id == selected_object, basis, layout);
    }
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
    const float screen_y = viewport::window_y_from_clip_y(p.y, static_cast<float>(height));
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

} // namespace wf::render
