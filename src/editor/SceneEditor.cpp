#include "editor/SceneEditor.hpp"

#include "math/Fixed16.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <utility>

namespace wf::editor {
namespace {
constexpr std::size_t kUndoLimit = 64;
constexpr float kPi = 3.14159265358979323846f;
} // namespace

using wf::math::Fixed16;

[[nodiscard]] SceneDocument make_demo_scene() {
    SceneDocument scene;
    scene.name = "Starter World";
    const auto one = Fixed16::from_int(1);
    const auto two = Fixed16::from_int(2);
    scene.objects.push_back({1, "primitive.cube", "Default", {
        {Fixed16::from_int(0), Fixed16::from_int(0), Fixed16::from_int(0)},
        wf::math::QuatQ30::identity(), {one, one, one}}});
    scene.objects.push_back({2, "primitive.cube", "Default", {
        {two, Fixed16::from_int(0), Fixed16::from_int(0)},
        wf::math::QuatQ30::identity(), {one, one, one}}});
    scene.objects.push_back({3, "primitive.cube", "Default", {
        {Fixed16::from_int(0), Fixed16::from_int(0), two},
        wf::math::QuatQ30::identity(), {one, one, one}}});
    return scene;
}

void recalculate_next_id(AppState& app) noexcept {
    ObjectId next = 1;
    for (const auto& object : app.scene.objects) {
        if (object.id >= next && object.id != std::numeric_limits<ObjectId>::max()) next = object.id + 1;
    }
    app.next_id = next;
    if (!app.scene.find(app.selected)) app.selected = 0;
}

void push_undo(AppState& app) {
    app.undo_stack.push_back(app.scene);
    if (app.undo_stack.size() > kUndoLimit) app.undo_stack.pop_front();
    app.redo_stack.clear();
}

void undo(AppState& app) {
    if (app.undo_stack.empty()) { app.status = "UNDO STACK EMPTY"; return; }
    app.redo_stack.push_back(app.scene);
    app.scene = std::move(app.undo_stack.back());
    app.undo_stack.pop_back();
    recalculate_next_id(app);
    app.status = "UNDO";
}

void redo(AppState& app) {
    if (app.redo_stack.empty()) { app.status = "REDO STACK EMPTY"; return; }
    app.undo_stack.push_back(app.scene);
    if (app.undo_stack.size() > kUndoLimit) app.undo_stack.pop_front();
    app.scene = std::move(app.redo_stack.back());
    app.redo_stack.pop_back();
    recalculate_next_id(app);
    app.status = "REDO";
}

void save_scene(AppState& app) {
    std::string error;
    if (wf::world::save_scene(app.scene_path, app.scene, error)) {
        app.status = "SAVED " + app.scene_path.filename().string();
        SDL_Log("Saved scene '%s'", app.scene_path.string().c_str());
    } else {
        app.status = "SAVE FAILED - SEE LOG";
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not save scene: %s", error.c_str());
    }
}

void add_cube(AppState& app) {
    if (app.next_id == 0 || app.next_id == std::numeric_limits<ObjectId>::max()) {
        app.status = "NO MORE OBJECT IDS AVAILABLE";
        return;
    }
    push_undo(app);
    const std::size_t index = app.scene.objects.size();
    const auto grid_x = static_cast<std::int32_t>(index % 5u);
    const auto grid_z = static_cast<std::int32_t>(index / 5u);
    const double x = static_cast<double>(grid_x - 2) * 1.5;
    const double z = static_cast<double>(grid_z) * 1.5;
    const auto one = Fixed16::from_int(1);
    ObjectInstance object;
    object.id = app.next_id++;
    object.prefab = "primitive.cube";
    object.layer = "Default";
    object.transform.position = {Fixed16::from_double(x), Fixed16::from_int(0), Fixed16::from_double(z)};
    object.transform.rotation = wf::math::QuatQ30::identity();
    object.transform.scale = {one, one, one};
    app.scene.objects.push_back(object);
    app.selected = object.id;
    app.status = "ADDED CUBE #" + std::to_string(object.id);
}

void delete_selected(AppState& app) {
    if (app.selected == 0) { app.status = "NOTHING SELECTED"; return; }
    const auto it = std::find_if(app.scene.objects.begin(), app.scene.objects.end(),
        [&app](const ObjectInstance& object) { return object.id == app.selected; });
    if (it == app.scene.objects.end()) { app.selected = 0; return; }
    push_undo(app);
    const ObjectId removed = app.selected;
    app.scene.objects.erase(it);
    app.selected = 0;
    recalculate_next_id(app);
    app.status = "DELETED OBJECT #" + std::to_string(removed);
}

void move_selected(AppState& app, double dx, double dy, double dz) {
    auto* object = app.scene.find(app.selected);
    if (!object) { app.status = "SELECT AN OBJECT FIRST"; return; }
    try {
        auto next_position = object->transform.position;
        next_position.x += Fixed16::from_double(dx);
        next_position.y += Fixed16::from_double(dy);
        next_position.z += Fixed16::from_double(dz);
        push_undo(app);
        object->transform.position = next_position;
        app.status = "MOVED OBJECT #" + std::to_string(object->id);
    } catch (const std::exception& ex) {
        app.status = std::string("MOVE FAILED: ") + ex.what();
    }
}

void rotate_selected_y(AppState& app, double angle_degrees) {
    auto* object = app.scene.find(app.selected);
    if (!object) { app.status = "SELECT AN OBJECT FIRST"; return; }
    constexpr double one = static_cast<double>(wf::math::QuatQ30::one_raw);
    const auto& q = object->transform.rotation;
    const double qx = static_cast<double>(q.x) / one;
    const double qy = static_cast<double>(q.y) / one;
    const double qz = static_cast<double>(q.z) / one;
    const double qw = static_cast<double>(q.w) / one;
    const double half_angle = angle_degrees * static_cast<double>(kPi) / 360.0;
    const double dy = std::sin(half_angle);
    const double dw = std::cos(half_angle);
    double x = dw*qx + dy*qz;
    double y = dw*qy + dy*qw;
    double z = dw*qz - dy*qx;
    double w = dw*qw - dy*qy;
    const double len = std::sqrt(x*x + y*y + z*z + w*w);
    if (!(len > 0.0) || !std::isfinite(len)) { app.status = "INVALID ROTATION"; return; }
    x /= len; y /= len; z /= len; w /= len;
    try {
        const wf::math::QuatQ30 next{
            static_cast<std::int32_t>(std::llround(x*one)),
            static_cast<std::int32_t>(std::llround(y*one)),
            static_cast<std::int32_t>(std::llround(z*one)),
            static_cast<std::int32_t>(std::llround(w*one))};
        push_undo(app);
        object->transform.rotation = next;
        app.status = "ROTATED OBJECT #" + std::to_string(object->id);
    } catch (const std::exception& ex) {
        app.status = std::string("ROTATION FAILED: ") + ex.what();
    }
}

} // namespace wf::editor
