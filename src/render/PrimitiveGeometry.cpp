#include "render/PrimitiveGeometry.hpp"
#include "render/PrimitiveRenderer.hpp"

#include "render/ViewportCoordinates.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace wf::render {
namespace {
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 1000.0f;
constexpr float kFovRadians = 0.872664626f; // 50 degrees

struct Vec3 final { float x, y, z; };
struct Color final { std::uint8_t r, g, b, a; };
using detail::Vertex;
static_assert(sizeof(Vertex) == 32);

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
    // Pixel coordinates increase downward; SDL_GPU NDC Y increases upward.
    // Convert the pixel position so a point at the top maps to NDC Y=+1.
    const float y = viewport::ndc_y_up_from_pixel_y(py, layout.height);
    const bool inside = ndc_x >= -1.5f && ndc_x <= 1.5f && ndc_y >= -1.5f && ndc_y <= 1.5f &&
                        view_z < kFarPlane;
    return {x, y, depth, inside};
}

// Grid strips are extruded in screen space instead of relying on a very thin
// world-space width. A ground-plane strip can become sub-pixel when the camera
// skims across the plane; screen-space extrusion keeps both grid directions legible.
void append_grid_line(std::vector<Vertex>& vertices, Vec3 a, Vec3 b, Color color,
                      float half_width_pixels, Vec3 view_normal, const CameraBasis& camera,
                      const ViewLayout& layout) {
    constexpr float clip_depth = kNearPlane + 0.01f;
    float depth_a = dot(sub(a, camera.eye), camera.forward);
    float depth_b = dot(sub(b, camera.eye), camera.forward);
    if (!std::isfinite(depth_a) || !std::isfinite(depth_b) ||
        (depth_a <= clip_depth && depth_b <= clip_depth)) {
        return;
    }

    // Clip a line crossing the near plane before projection to avoid enormous or
    // invalid screen coordinates when orbiting close to the grid.
    if (depth_a <= clip_depth) {
        const float t = (clip_depth - depth_a) / (depth_b - depth_a);
        a = add(a, mul(sub(b, a), t));
        depth_a = clip_depth;
    } else if (depth_b <= clip_depth) {
        const float t = (clip_depth - depth_b) / (depth_a - depth_b);
        b = add(b, mul(sub(a, b), t));
        depth_b = clip_depth;
    }

    const ProjectedPoint pa = project_point(a, camera, layout);
    const ProjectedPoint pb = project_point(b, camera, layout);
    const float ax = (pa.x + 1.0f) * 0.5f * layout.width;
    const float ay = viewport::pixel_y_from_ndc_y_up(pa.y, layout.height);
    const float bx = (pb.x + 1.0f) * 0.5f * layout.width;
    const float by = viewport::pixel_y_from_ndc_y_up(pb.y, layout.height);
    const float dx = bx - ax;
    const float dy = by - ay;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (!(length > 0.25f) || !std::isfinite(length)) return;

    const float offset_x = -dy / length * half_width_pixels;
    const float offset_y = dx / length * half_width_pixels;
    const auto push_vertex = [&](float pixel_x, float pixel_y, float depth) {
        const float ndc_x = 2.0f * pixel_x / layout.width - 1.0f;
        const float ndc_y = viewport::ndc_y_up_from_pixel_y(pixel_y, layout.height);
        vertices.push_back({ndc_x, ndc_y, depth, color.r, color.g, color.b, color.a,
                            view_normal.x, view_normal.y, view_normal.z, 0.0f});
    };

    // Each endpoint's NDC depth is shared by both sides of the strip.
    push_vertex(ax + offset_x, ay + offset_y, pa.depth);
    push_vertex(bx + offset_x, by + offset_y, pb.depth);
    push_vertex(bx - offset_x, by - offset_y, pb.depth);
    push_vertex(ax + offset_x, ay + offset_y, pa.depth);
    push_vertex(bx - offset_x, by - offset_y, pb.depth);
    push_vertex(ax - offset_x, ay - offset_y, pa.depth);
}

void append_grid(std::vector<Vertex>& vertices, const CameraBasis& camera, const ViewLayout& layout) {
    constexpr float extent = 24.0f;
    constexpr float minor_half_width_pixels = 0.65f;
    constexpr float major_half_width_pixels = 0.90f;
    constexpr float axis_half_width_pixels = 1.15f;
    const double origin_grid_x = std::floor(camera.origin_x);
    const double origin_grid_z = std::floor(camera.origin_z);
    const float relative_ground_y = static_cast<float>(-camera.origin_y - 0.015);
    // Dot the world-up normal into the camera basis to store it in view space.
    const Vec3 ground_normal_in_view_space{camera.right.y, camera.up.y, camera.forward.y};
    for (int i = -24; i <= 24; ++i) {
        const float x = static_cast<float>(origin_grid_x + static_cast<double>(i) - camera.origin_x);
        const float z = static_cast<float>(origin_grid_z + static_cast<double>(i) - camera.origin_z);

        const int grid_x_index = static_cast<int>(origin_grid_x) + i;
        const bool x_major = grid_x_index % 5 == 0;
        const bool x_axis = grid_x_index == 0;
        Color x_color = x_major ? Color{51, 63, 78, 255} : Color{37, 46, 58, 255};
        if (x_axis) x_color = {95, 67, 63, 255};
        const float x_width = x_axis ? axis_half_width_pixels
            : (x_major ? major_half_width_pixels : minor_half_width_pixels);
        append_grid_line(vertices, {x, relative_ground_y, -extent},
                         {x, relative_ground_y, extent}, x_color, x_width,
                         ground_normal_in_view_space, camera, layout);

        const int grid_z_index = static_cast<int>(origin_grid_z) + i;
        const bool z_major = grid_z_index % 5 == 0;
        const bool z_axis = grid_z_index == 0;
        Color z_color = z_major ? Color{51, 63, 78, 255} : Color{37, 46, 58, 255};
        if (z_axis) z_color = {53, 81, 112, 255};
        const float z_width = z_axis ? axis_half_width_pixels
            : (z_major ? major_half_width_pixels : minor_half_width_pixels);
        append_grid_line(vertices, {-extent, relative_ground_y + 0.001f, z},
                         {extent, relative_ground_y + 0.001f, z}, z_color, z_width,
                         ground_normal_in_view_space, camera, layout);
    }
}
} // namespace

SceneViewport make_scene_viewport(float window_width, float window_height) noexcept {
    const ViewLayout layout = make_layout(window_width, window_height);
    return {0.0f, layout.top, layout.viewport_width, layout.viewport_height};
}

void build_grid_vertices(std::vector<detail::Vertex>& vertices,
                         const EditorCamera& camera,
                         float window_width,
                         float window_height) {
    const ViewLayout layout = make_layout(window_width, window_height);
    const CameraBasis basis = make_camera(camera);
    append_grid(vertices, basis, layout);
}

void append_unit_cube_vertices(std::vector<detail::Vertex>& vertices, bool selected) {
    // Unit cube origin is on the bottom face so y=0 rests on the editor grid.
    static constexpr Vec3 faces[6][4] = {
        {{-0.5f,1.0f,-0.5f},{0.5f,1.0f,-0.5f},{0.5f,1.0f,0.5f},{-0.5f,1.0f,0.5f}},
        {{-0.5f,0.0f,0.5f},{0.5f,0.0f,0.5f},{0.5f,0.0f,-0.5f},{-0.5f,0.0f,-0.5f}},
        {{-0.5f,0.0f,-0.5f},{0.5f,0.0f,-0.5f},{0.5f,1.0f,-0.5f},{-0.5f,1.0f,-0.5f}},
        {{0.5f,0.0f,0.5f},{-0.5f,0.0f,0.5f},{-0.5f,1.0f,0.5f},{0.5f,1.0f,0.5f}},
        {{-0.5f,0.0f,0.5f},{-0.5f,0.0f,-0.5f},{-0.5f,1.0f,-0.5f},{-0.5f,1.0f,0.5f}},
        {{0.5f,0.0f,-0.5f},{0.5f,0.0f,0.5f},{0.5f,1.0f,0.5f},{0.5f,1.0f,-0.5f}}
    };
    static constexpr Vec3 face_normals[6] = {
        {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
        {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f},
        {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}
    };
    static constexpr Color face_colors[6] = {
        {94, 158, 230, 255}, {33, 48, 65, 255}, {65, 125, 188, 255},
        {50, 101, 156, 255}, {45, 88, 137, 255}, {72, 135, 198, 255}
    };

    for (std::size_t face = 0; face < 6; ++face) {
        Color color = face_colors[face];
        if (selected) {
            const auto lift = [](std::uint8_t channel) {
                return static_cast<std::uint8_t>(std::min(255, static_cast<int>(channel) / 2 + 128));
            };
            color = {lift(color.r), lift(color.g),
                     static_cast<std::uint8_t>(std::min(255, static_cast<int>(color.b) + 24)), 255};
        }

        const Vec3 normal = face_normals[face];
        const Vec3 a = faces[face][0];
        const Vec3 b = faces[face][1];
        const Vec3 c = faces[face][2];
        const Vec3 d = faces[face][3];
        vertices.push_back({a.x, a.y, a.z, color.r, color.g, color.b, color.a, normal.x, normal.y, normal.z, 1.0f});
        vertices.push_back({b.x, b.y, b.z, color.r, color.g, color.b, color.a, normal.x, normal.y, normal.z, 1.0f});
        vertices.push_back({c.x, c.y, c.z, color.r, color.g, color.b, color.a, normal.x, normal.y, normal.z, 1.0f});
        vertices.push_back({a.x, a.y, a.z, color.r, color.g, color.b, color.a, normal.x, normal.y, normal.z, 1.0f});
        vertices.push_back({c.x, c.y, c.z, color.r, color.g, color.b, color.a, normal.x, normal.y, normal.z, 1.0f});
        vertices.push_back({d.x, d.y, d.z, color.r, color.g, color.b, color.a, normal.x, normal.y, normal.z, 1.0f});
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
    const float screen_y = viewport::pixel_y_from_ndc_y_up(p.y, static_cast<float>(height));
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
    const float ndc_y = viewport::ndc_y_up_from_pixel_y(mouse_y - layout.top, layout.viewport_height);
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
