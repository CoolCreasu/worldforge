#include "editor/editorinput.hpp"

#include "editor/sceneeditor.hpp"

#include "math/transform.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <string>

namespace wf::editor {
using wf::math::Fixed16;
using wf::world::ObjectId;
using wf::world::ObjectInstance;

namespace {
[[nodiscard]] bool is_ui_region(AppState& app, float x, float y) noexcept {
    int width = 0, height = 0;
    SDL_GetWindowSize(app.window, &width, &height);
    if (width <= 0 || height <= 0) return true;
    const float scale = std::clamp(static_cast<float>(height) / 800.0f, 0.8f, 2.0f);
    const float top = 44.0f * scale;
    const float bottom = 30.0f * scale;
    const float side = std::min(310.0f * scale, static_cast<float>(width) * 0.42f);
    return y < top || y >= static_cast<float>(height) - bottom || x >= static_cast<float>(width) - side;
}

[[nodiscard]] ObjectId pick_object(AppState& app, float mouse_x, float mouse_y) {
    int window_width = 0, window_height = 0;
    SDL_GetWindowSize(app.window, &window_width, &window_height);
    if (window_width <= 0 || window_height <= 0 || is_ui_region(app, mouse_x, mouse_y)) return 0;

    ObjectId best_id = 0;
    float best_distance = 42.0f;
    float best_depth = std::numeric_limits<float>::max();
    for (const auto& object : app.scene.objects) {
        const double x = object.transform.position.x.to_double();
        const double y = object.transform.position.y.to_double() + 0.5*object.transform.scale.y.to_double();
        const double z = object.transform.position.z.to_double();
        const auto point = app.renderer.project_to_window(x,y,z,window_width,window_height,app.camera);
        if (!point.visible) continue;
        const float dx = point.x-mouse_x;
        const float dy = point.y-mouse_y;
        const float distance = std::sqrt(dx*dx + dy*dy);
        if (distance < best_distance || (distance == best_distance && point.depth < best_depth)) {
            best_distance = distance;
            best_depth = point.depth;
            best_id = object.id;
        }
    }
    return best_id;
}
} // namespace

void process_event(AppState& app, const SDL_Event& event, bool& quit) {
    switch (event.type) {
    case SDL_EVENT_QUIT:
        quit = true;
        break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        if (event.window.windowID == SDL_GetWindowID(app.window)) quit = true;
        break;
    case SDL_EVENT_KEY_DOWN: {
        if (event.key.repeat || ImGui::GetIO().WantCaptureKeyboard) break;
        const SDL_Keycode key = event.key.key;
        const bool control = (event.key.mod & SDL_KMOD_CTRL) != 0;
        const bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
        const double step = shift ? 0.1 : 1.0;
        if (key == SDLK_ESCAPE) { quit = true; break; }
        if (control && key == SDLK_S) { save_scene(app); break; }
        if (control && key == SDLK_Z) { undo(app); break; }
        if (control && key == SDLK_Y) { redo(app); break; }
        if (key == SDLK_A) { add_cube(app); break; }
        if (key == SDLK_DELETE || key == SDLK_BACKSPACE) { delete_selected(app); break; }
        if (key == SDLK_LEFT) { move_selected(app,-step,0,0); break; }
        if (key == SDLK_RIGHT) { move_selected(app,step,0,0); break; }
        if (key == SDLK_UP) { move_selected(app,0,0,-step); break; }
        if (key == SDLK_DOWN) { move_selected(app,0,0,step); break; }
        if (key == SDLK_PAGEUP) { move_selected(app,0,step,0); break; }
        if (key == SDLK_PAGEDOWN) { move_selected(app,0,-step,0); break; }
        if (key == SDLK_Q) { rotate_selected_y(app,-15.0); break; }
        if (key == SDLK_E) { rotate_selected_y(app,15.0); break; }
        if (key == SDLK_F) {
            if (const auto* object = app.scene.find(app.selected)) {
                app.camera.target_x = object->transform.position.x.to_double();
                app.camera.target_y = object->transform.position.y.to_double() + 0.5;
                app.camera.target_z = object->transform.position.z.to_double();
                app.status = "FOCUSED OBJECT #" + std::to_string(object->id);
            }
            break;
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.button == SDL_BUTTON_RIGHT) {
            if (!is_ui_region(app, event.button.x, event.button.y)) {
                app.camera_orbiting = true;
                app.last_mouse_x = event.button.x;
                app.last_mouse_y = event.button.y;
            }
            break;
        }
        if (event.button.button == SDL_BUTTON_LEFT) {
            if (is_ui_region(app, event.button.x, event.button.y)) break;
            const ObjectId hit = pick_object(app, event.button.x, event.button.y);
            if (hit != 0) {
                app.selected = hit;
                push_undo(app); // One undo snapshot for the whole drag.
                const auto* object = app.scene.find(hit);
                double ground_x = 0.0, ground_z = 0.0;
                int width = 0, height = 0;
                SDL_GetWindowSize(app.window, &width, &height);
                app.dragging = object != nullptr && app.renderer.window_to_ground(
                    event.button.x, event.button.y, width, height, app.camera, ground_x, ground_z);
                if (object && app.dragging) {
                    app.drag_offset_world_x = object->transform.position.x.to_double() - ground_x;
                    app.drag_offset_world_z = object->transform.position.z.to_double() - ground_z;
                }
                app.status = "SELECTED OBJECT #" + std::to_string(hit) + " - DRAG TO MOVE";
            } else {
                app.selected = 0;
                app.status = "SELECTION CLEARED";
            }
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button == SDL_BUTTON_LEFT && app.dragging) {
            app.dragging = false;
            app.position_edit_undo_pushed = false;
            app.status = "TRANSFORM UPDATED - CTRL+S TO SAVE";
        }
        if (event.button.button == SDL_BUTTON_RIGHT) app.camera_orbiting = false;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (app.camera_orbiting) {
            app.camera.yaw += static_cast<double>(event.motion.xrel) * 0.008;
            app.camera.pitch -= static_cast<double>(event.motion.yrel) * 0.008;
            app.camera.pitch = std::clamp(app.camera.pitch, -1.45, 1.45);
            app.last_mouse_x = event.motion.x;
            app.last_mouse_y = event.motion.y;
        } else if (app.dragging) {
            if (auto* object = app.scene.find(app.selected)) {
                int width = 0, height = 0;
                SDL_GetWindowSize(app.window, &width, &height);
                double ground_x = 0.0, ground_z = 0.0;
                if (app.renderer.window_to_ground(event.motion.x, event.motion.y, width, height,
                                                  app.camera, ground_x, ground_z)) {
                    try {
                        const Fixed16 next_x = Fixed16::from_double(ground_x + app.drag_offset_world_x);
                        const Fixed16 next_z = Fixed16::from_double(ground_z + app.drag_offset_world_z);
                        object->transform.position.x = next_x;
                        object->transform.position.z = next_z;
                    } catch (const std::exception& ex) {
                        app.status = std::string("DRAG LIMIT: ") + ex.what();
                    }
                }
            }
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL: {
        float mouse_x = 0.0f, mouse_y = 0.0f;
        (void)SDL_GetMouseState(&mouse_x, &mouse_y);
        if (!is_ui_region(app, mouse_x, mouse_y)) {
            app.camera.distance = std::clamp(app.camera.distance *
                std::exp(-static_cast<double>(event.wheel.y) * 0.12), 4.0, 80.0);
            app.status = "CAMERA DISTANCE " + std::to_string(static_cast<int>(app.camera.distance));
        }
        break;
    }
    default:
        break;
    }
}

} // namespace wf::editor
