#include "render/rendertransforms.hpp"

#include "math/transform.hpp"

#include <algorithm>
#include <cmath>

namespace wf::render::detail {
namespace {
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 1000.0f;
constexpr float kFieldOfViewRadians = 0.872664626f; // 50 degrees

struct Vector3 final { float x, y, z; };

[[nodiscard]] Vector3 add(Vector3 a, Vector3 b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
[[nodiscard]] Vector3 multiply(Vector3 vector, float scalar) noexcept {
    return {vector.x * scalar, vector.y * scalar, vector.z * scalar};
}
[[nodiscard]] float dot(Vector3 a, Vector3 b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
[[nodiscard]] Vector3 cross(Vector3 a, Vector3 b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] Vector3 normalize(Vector3 vector) noexcept {
    const float length = std::sqrt(std::max(dot(vector, vector), 1.0e-12f));
    return multiply(vector, 1.0f / length);
}
[[nodiscard]] Vector3 rotate_by_quaternion(Vector3 vector, const math::QuatQ30& rotation) noexcept {
    constexpr double quaternion_scale = static_cast<double>(math::QuatQ30::one_raw);
    const Vector3 q{
        static_cast<float>(static_cast<double>(rotation.x) / quaternion_scale),
        static_cast<float>(static_cast<double>(rotation.y) / quaternion_scale),
        static_cast<float>(static_cast<double>(rotation.z) / quaternion_scale)};
    const float qw = static_cast<float>(static_cast<double>(rotation.w) / quaternion_scale);
    const Vector3 twice_cross = multiply(cross(q, vector), 2.0f);
    return add(vector, add(multiply(twice_cross, qw), cross(q, twice_cross)));
}
struct CameraBasis final {
    Vector3 eye;
    Vector3 right;
    Vector3 up;
    Vector3 forward;
};
[[nodiscard]] CameraBasis make_camera_basis(const EditorCamera& camera) noexcept {
    const float yaw = static_cast<float>(camera.yaw);
    const float pitch = static_cast<float>(camera.pitch);
    const float cosine_pitch = std::cos(pitch);
    const Vector3 direction{
        cosine_pitch * std::sin(yaw), std::sin(pitch), cosine_pitch * std::cos(yaw)};
    const Vector3 eye = multiply(direction, static_cast<float>(camera.distance));
    const Vector3 forward = multiply(normalize(eye), -1.0f);
    const Vector3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
    const Vector3 up = normalize(cross(right, forward));
    return {eye, right, up, forward};
}
} // namespace

Matrix4x4 make_model_matrix(const world::ObjectInstance& object,
                            const EditorCamera& camera) noexcept {
    const float scale_x = static_cast<float>(object.transform.scale.x.to_double());
    const float scale_y = static_cast<float>(object.transform.scale.y.to_double());
    const float scale_z = static_cast<float>(object.transform.scale.z.to_double());

    // Scale local basis vectors before rotating them. Subtract the camera target
    // from fixed-point positions in double precision before converting to float.
    const Vector3 x_axis = rotate_by_quaternion({scale_x, 0.0f, 0.0f}, object.transform.rotation);
    const Vector3 y_axis = rotate_by_quaternion({0.0f, scale_y, 0.0f}, object.transform.rotation);
    const Vector3 z_axis = rotate_by_quaternion({0.0f, 0.0f, scale_z}, object.transform.rotation);

    Matrix4x4 model;
    model.at(0, 0) = x_axis.x; model.at(1, 0) = x_axis.y; model.at(2, 0) = x_axis.z;
    model.at(0, 1) = y_axis.x; model.at(1, 1) = y_axis.y; model.at(2, 1) = y_axis.z;
    model.at(0, 2) = z_axis.x; model.at(1, 2) = z_axis.y; model.at(2, 2) = z_axis.z;
    model.at(0, 3) = static_cast<float>(object.transform.position.x.to_double() - camera.target_x);
    model.at(1, 3) = static_cast<float>(object.transform.position.y.to_double() - camera.target_y);
    model.at(2, 3) = static_cast<float>(object.transform.position.z.to_double() - camera.target_z);
    model.at(3, 3) = 1.0f;
    return model;
}

Matrix4x4 make_view_matrix(const EditorCamera& camera) noexcept {
    const CameraBasis basis = make_camera_basis(camera);
    Matrix4x4 view;
    view.at(0, 0) = basis.right.x;   view.at(0, 1) = basis.right.y;   view.at(0, 2) = basis.right.z;
    view.at(1, 0) = basis.up.x;      view.at(1, 1) = basis.up.y;      view.at(1, 2) = basis.up.z;
    view.at(2, 0) = basis.forward.x; view.at(2, 1) = basis.forward.y; view.at(2, 2) = basis.forward.z;
    view.at(0, 3) = -dot(basis.right, basis.eye);
    view.at(1, 3) = -dot(basis.up, basis.eye);
    view.at(2, 3) = -dot(basis.forward, basis.eye);
    view.at(3, 3) = 1.0f;
    return view;
}

Direction3 transform_direction_to_view_space(
    const EditorCamera& camera, Direction3 world_direction) noexcept {
    // Directions have no translation component. Rotate a world-space direction
    // into view space so light direction does not change as the camera orbits.
    const Matrix4x4 view = make_view_matrix(camera);
    return {
        view.at(0, 0) * world_direction.x +
            view.at(0, 1) * world_direction.y +
            view.at(0, 2) * world_direction.z,
        view.at(1, 0) * world_direction.x +
            view.at(1, 1) * world_direction.y +
            view.at(1, 2) * world_direction.z,
        view.at(2, 0) * world_direction.x +
            view.at(2, 1) * world_direction.y +
            view.at(2, 2) * world_direction.z
    };
}

Matrix4x4 make_projection_matrix(float aspect_ratio) noexcept {
    const float safe_aspect = std::max(aspect_ratio, 1.0e-4f);
    const float focal_length = 1.0f / std::tan(kFieldOfViewRadians * 0.5f);
    Matrix4x4 projection;
    projection.at(0, 0) = focal_length / safe_aspect;
    projection.at(1, 1) = focal_length;
    projection.at(2, 2) = kFarPlane / (kFarPlane - kNearPlane);
    projection.at(2, 3) = -(kFarPlane * kNearPlane) / (kFarPlane - kNearPlane);
    projection.at(3, 2) = 1.0f;
    return projection;
}

} // namespace wf::render::detail
