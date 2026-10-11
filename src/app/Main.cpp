#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"

#include "math/Fixed16.hpp"
#include "render/PrimitiveRenderer.hpp"
#include "world/SceneIO.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace {
using wf::math::Fixed16;
using wf::world::ObjectId;
using wf::world::ObjectInstance;
using wf::world::SceneDocument;
using wf::render::EditorCamera;
using wf::render::PrimitiveRenderer;

constexpr std::size_t kUndoLimit = 64;
constexpr float kPi = 3.14159265358979323846f;

struct AppState final {
    SDL_Window* window = nullptr;
    SDL_GPUDevice* gpu = nullptr;
    bool window_claimed = false;
    bool imgui_context_created = false;
    bool imgui_platform_initialized = false;
    bool imgui_renderer_initialized = false;
    PrimitiveRenderer renderer;
    SceneDocument scene;
    std::filesystem::path scene_path = "assets/scenes/starter.world";
    ObjectId selected = 0;
    ObjectId next_id = 1;
    EditorCamera camera{};
    std::string status = "READY - DRAG OBJECTS TO MOVE; RIGHT-DRAG TO ORBIT";

    std::mutex event_mutex;
    std::vector<SDL_Event> pending_events;
    bool events_closed = false;

    std::deque<SceneDocument> undo_stack;
    std::deque<SceneDocument> redo_stack;

    bool dragging = false;
    bool camera_orbiting = false;
    double drag_offset_world_x = 0.0;
    double drag_offset_world_z = 0.0;
    float last_mouse_x = 0.0f;
    float last_mouse_y = 0.0f;
    bool position_edit_undo_pushed = false;
    bool scale_edit_undo_pushed = false;
};

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

void build_editor_ui(AppState& app) {
    ImGuiIO& io = ImGui::GetIO();
    const float width = io.DisplaySize.x;
    const float height = io.DisplaySize.y;
    if (width <= 0.0f || height <= 0.0f) return;
    const float scale = std::clamp(height / 800.0f, 0.8f, 2.0f);
    const float top = 44.0f * scale;
    const float bottom = 30.0f * scale;
    const float side = std::min(310.0f * scale, width * 0.42f);
    const ImGuiWindowFlags fixed_panel = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * scale, 7.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * scale, 5.0f * scale));
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, top), ImGuiCond_Always);
    if (ImGui::Begin("##Toolbar", nullptr, fixed_panel)) {
        ImGui::TextColored(ImVec4(0.45f, 0.72f, 0.98f, 1.0f), "WORLDFORGE");
        ImGui::SameLine(145.0f * scale);
        if (ImGui::Button("Add Cube")) add_cube(app);
        ImGui::SameLine();
        if (ImGui::Button("Delete")) delete_selected(app);
        ImGui::SameLine();
        if (ImGui::Button("Undo")) undo(app);
        ImGui::SameLine();
        if (ImGui::Button("Redo")) redo(app);
        ImGui::SameLine();
        if (ImGui::Button("Save")) save_scene(app);
        ImGui::SameLine();
        ImGui::TextDisabled("RMB orbit  |  Wheel zoom  |  A add  |  Q/E rotate  |  Ctrl+S save");
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(width - side, top), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(side, height - top - bottom), ImGuiCond_Always);
    if (ImGui::Begin("##WorldPanel", nullptr, fixed_panel)) {
        ImGui::Text("SCENE OBJECTS");
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu)", app.scene.objects.size());
        ImGui::Separator();
        const float list_height = std::clamp(ImGui::GetContentRegionAvail().y * 0.43f, 120.0f * scale, 280.0f * scale);
        if (ImGui::BeginChild("##ObjectList", ImVec2(0.0f, list_height), ImGuiChildFlags_Borders)) {
            for (const auto& object : app.scene.objects) {
                const std::string label = "Cube #" + std::to_string(object.id);
                if (ImGui::Selectable(label.c_str(), object.id == app.selected)) {
                    app.selected = object.id;
                    app.position_edit_undo_pushed = false;
                    app.scale_edit_undo_pushed = false;
                    app.status = "SELECTED OBJECT #" + std::to_string(object.id);
                }
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::Text("INSPECTOR");
        ImGui::Separator();
        if (auto* object = app.scene.find(app.selected)) {
            ImGui::TextDisabled("ID: %llu", static_cast<unsigned long long>(object->id));
            ImGui::TextDisabled("Prefab: %s", object->prefab.c_str());
            ImGui::TextDisabled("Layer: %s", object->layer.c_str());
            double position[3] = {
                object->transform.position.x.to_double(), object->transform.position.y.to_double(),
                object->transform.position.z.to_double()};
            if (ImGui::DragScalarN("Position", ImGuiDataType_Double, position, 3, 0.05f,
                                   nullptr, nullptr, "%.4f")) {
                try {
                    const wf::math::Vec3Fx16 next{
                        Fixed16::from_double(position[0]), Fixed16::from_double(position[1]),
                        Fixed16::from_double(position[2])};
                    if (!app.position_edit_undo_pushed) {
                        push_undo(app);
                        app.position_edit_undo_pushed = true;
                    }
                    app.scene.find(app.selected)->transform.position = next;
                    app.status = "EDITING POSITION";
                } catch (const std::exception& ex) {
                    app.status = std::string("POSITION EDIT FAILED: ") + ex.what();
                }
            }
            if (ImGui::IsItemDeactivatedAfterEdit() || !ImGui::IsItemActive())
                app.position_edit_undo_pushed = false;

            double object_scale[3] = {
                object->transform.scale.x.to_double(), object->transform.scale.y.to_double(),
                object->transform.scale.z.to_double()};
            if (ImGui::DragScalarN("Scale", ImGuiDataType_Double, object_scale, 3, 0.025f,
                                   nullptr, nullptr, "%.3f")) {
                try {
                    const wf::math::Vec3Fx16 next{
                        Fixed16::from_double(object_scale[0]), Fixed16::from_double(object_scale[1]),
                        Fixed16::from_double(object_scale[2])};
                    if (!app.scale_edit_undo_pushed) {
                        push_undo(app);
                        app.scale_edit_undo_pushed = true;
                    }
                    app.scene.find(app.selected)->transform.scale = next;
                    app.status = "EDITING SCALE";
                } catch (const std::exception& ex) {
                    app.status = std::string("SCALE EDIT FAILED: ") + ex.what();
                }
            }
            if (ImGui::IsItemDeactivatedAfterEdit() || !ImGui::IsItemActive())
                app.scale_edit_undo_pushed = false;

            ImGui::Text("Rotation (Q2.30)");
            const auto& q = app.scene.find(app.selected)->transform.rotation;
            ImGui::TextDisabled("x %d  y %d", q.x, q.y);
            ImGui::TextDisabled("z %d  w %d", q.z, q.w);
            if (ImGui::Button("Rotate -15 deg")) rotate_selected_y(app, -15.0);
            ImGui::SameLine();
            if (ImGui::Button("Rotate +15 deg")) rotate_selected_y(app, 15.0);
            ImGui::Spacing();
            ImGui::TextDisabled("Stored position: signed Q16.16");
        } else {
            ImGui::TextDisabled("Select an object in the scene or list.");
            ImGui::TextDisabled("Use Add Cube to begin building.");
        }
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(0.0f, height - bottom), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, bottom), ImGuiCond_Always);
    if (ImGui::Begin("##StatusBar", nullptr, fixed_panel)) {
        ImGui::TextUnformatted(app.status.c_str());
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void cleanup(AppState& state) noexcept {
    {
        std::lock_guard<std::mutex> lock(state.event_mutex);
        state.events_closed = true;
        state.pending_events.clear();
    }
    if (state.gpu) (void)SDL_WaitForGPUIdle(state.gpu);
    if (state.imgui_platform_initialized) {
        ImGui_ImplSDL3_Shutdown();
        state.imgui_platform_initialized = false;
    }
    if (state.imgui_renderer_initialized) {
        ImGui_ImplSDLGPU3_Shutdown();
        state.imgui_renderer_initialized = false;
    }
    if (state.imgui_context_created) {
        ImGui::DestroyContext();
        state.imgui_context_created = false;
    }
    state.renderer.shutdown();
    if (state.gpu) {
        (void)SDL_WaitForGPUIdle(state.gpu);
        if (state.window_claimed && state.window) {
            SDL_ReleaseWindowFromGPUDevice(state.gpu, state.window);
            state.window_claimed = false;
        }
        SDL_DestroyGPUDevice(state.gpu);
        state.gpu = nullptr;
    }
    if (state.window) {
        SDL_DestroyWindow(state.window);
        state.window = nullptr;
    }
}

SDL_AppResult fail(AppState& state, const char* action) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", action, SDL_GetError());
    cleanup(state);
    return SDL_APP_FAILURE;
}
} // namespace

SDL_AppResult SDLCALL SDL_AppInit(void** appstate, int argc, char* argv[]) {
    SDL_SetAppMetadata("Worldforge", "0.3.1", "org.worldforge.game-tools");
    auto state = std::make_unique<AppState>();
    if (argc > 1 && argv && argv[1]) state->scene_path = argv[1];
    *appstate = state.release();
    auto& app = *static_cast<AppState*>(*appstate);

    if (!SDL_Init(SDL_INIT_VIDEO)) return fail(app, "SDL_Init failed");
    app.window = SDL_CreateWindow("Worldforge - Primitive World Editor", 1280, 800,
                                  SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!app.window) return fail(app, "SDL_CreateWindow failed");

    // This checkpoint ships SPIR-V for Vulkan and MSL for Metal. The callback
    // lifecycle is cross-platform; adding DXIL is a follow-up for native D3D12.
    constexpr SDL_GPUShaderFormat shader_formats = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL;
    app.gpu = SDL_CreateGPUDevice(shader_formats, true, nullptr);
    if (!app.gpu) return fail(app, "SDL_CreateGPUDevice failed");
    if (!SDL_ClaimWindowForGPUDevice(app.gpu, app.window)) return fail(app, "SDL_ClaimWindowForGPUDevice failed");
    app.window_claimed = true;
    (void)SDL_SetGPUAllowedFramesInFlight(app.gpu, 2);

    std::string scene_error;
    if (std::filesystem::exists(app.scene_path)) {
        if (!wf::world::load_scene(app.scene_path, app.scene, scene_error)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not load scene '%s': %s",
                        app.scene_path.string().c_str(), scene_error.c_str());
            app.scene = make_demo_scene();
            app.status = "LOAD FAILED - STARTED SAMPLE WORLD";
        } else {
            app.status = "LOADED " + app.scene_path.filename().string();
        }
    } else if (app.scene_path.filename() == "starter.world") {
        app.scene = make_demo_scene();
        app.status = "STARTED SAMPLE WORLD";
    } else {
        app.scene.name = "Untitled World";
        app.status = "NEW EMPTY WORLD";
    }
    recalculate_next_id(app);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    app.imgui_context_created = true;
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 3.0f;
    style.WindowBorderSize = 1.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.090f, 0.115f, 1.0f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.065f, 0.075f, 0.095f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.090f, 0.115f, 0.150f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.16f, 0.28f, 0.40f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.38f, 0.54f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.13f, 0.23f, 0.33f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.36f, 0.50f, 1.0f);

    if (!ImGui_ImplSDL3_InitForSDLGPU(app.window)) return fail(app, "ImGui SDL3 backend initialization failed");
    app.imgui_platform_initialized = true;
    ImGui_ImplSDLGPU3_InitInfo imgui_gpu_info{};
    imgui_gpu_info.Device = app.gpu;
    imgui_gpu_info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(app.gpu, app.window);
    imgui_gpu_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    if (!ImGui_ImplSDLGPU3_Init(&imgui_gpu_info)) return fail(app, "ImGui SDL_GPU backend initialization failed");
    app.imgui_renderer_initialized = true;

    std::string renderer_error;
    if (!app.renderer.initialize(app.gpu, app.window, renderer_error)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Renderer initialization failed: %s", renderer_error.c_str());
        cleanup(app);
        return SDL_APP_FAILURE;
    }
    SDL_Log("GPU driver: %s", SDL_GetGPUDeviceDriver(app.gpu));
    SDL_Log("Worldforge editor ready. A add cube, click/drag to move, right-drag orbit, wheel zoom, Q/E rotate, Ctrl+S save, Ctrl+Z/Y undo/redo.");
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDLCALL SDL_AppEvent(void* appstate, SDL_Event* event) {
    auto* app = static_cast<AppState*>(appstate);
    if (!app || !event) return SDL_APP_CONTINUE;
    std::lock_guard<std::mutex> lock(app->event_mutex);
    if (!app->events_closed) app->pending_events.push_back(*event);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDLCALL SDL_AppIterate(void* appstate) {
    auto& app = *static_cast<AppState*>(appstate);
    std::vector<SDL_Event> events;
    {
        std::lock_guard<std::mutex> lock(app.event_mutex);
        events.swap(app.pending_events);
    }
    bool quit = false;
    for (const auto& event : events) {
        (void)ImGui_ImplSDL3_ProcessEvent(&event);
        process_event(app, event, quit);
    }
    if (quit) return SDL_APP_SUCCESS;

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    build_editor_ui(app);
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();

    int pixel_width = 0, pixel_height = 0;
    if (!SDL_GetWindowSizeInPixels(app.window, &pixel_width, &pixel_height)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_GetWindowSizeInPixels: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    if (pixel_width > 0 && pixel_height > 0) {
        std::string error;
        if (!app.renderer.prepare(static_cast<std::uint32_t>(pixel_width),
                                  static_cast<std::uint32_t>(pixel_height), app.scene.objects.size(), error)) {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "Renderer prepare failed: %s", error.c_str());
            return SDL_APP_FAILURE;
        }
    }

    SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(app.gpu);
    if (!command_buffer) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "SDL_AcquireGPUCommandBuffer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_GPUTexture* swapchain_texture = nullptr;
    Uint32 swapchain_width = 0, swapchain_height = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, app.window, &swapchain_texture,
                                               &swapchain_width, &swapchain_height)) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "SDL_WaitAndAcquireGPUSwapchainTexture: %s", SDL_GetError());
        (void)SDL_CancelGPUCommandBuffer(command_buffer);
        return SDL_APP_FAILURE;
    }

    if (swapchain_texture && swapchain_width > 0 && swapchain_height > 0) {
        std::string error;
        if (!app.renderer.prepare(swapchain_width, swapchain_height, app.scene.objects.size(), error)) {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "Renderer resize failed: %s", error.c_str());
            (void)SDL_SubmitGPUCommandBuffer(command_buffer);
            return SDL_APP_FAILURE;
        }
        if (!app.renderer.render(command_buffer, swapchain_texture, swapchain_width, swapchain_height,
                                 app.scene, app.selected, app.camera, error)) {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "Render failed: %s", error.c_str());
            (void)SDL_SubmitGPUCommandBuffer(command_buffer);
            return SDL_APP_FAILURE;
        }

        ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, command_buffer);
        SDL_GPUColorTargetInfo ui_target{};
        ui_target.texture = swapchain_texture;
        ui_target.load_op = SDL_GPU_LOADOP_LOAD;
        ui_target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* ui_pass = SDL_BeginGPURenderPass(command_buffer, &ui_target, 1, nullptr);
        if (!ui_pass) {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "Could not begin ImGui render pass: %s", SDL_GetError());
            (void)SDL_SubmitGPUCommandBuffer(command_buffer);
            return SDL_APP_FAILURE;
        }
        ImGui_ImplSDLGPU3_RenderDrawData(draw_data, command_buffer, ui_pass);
        SDL_EndGPURenderPass(ui_pass);
    }
    if (!SDL_SubmitGPUCommandBuffer(command_buffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "SDL_SubmitGPUCommandBuffer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    return SDL_APP_CONTINUE;
}

void SDLCALL SDL_AppQuit(void* appstate, SDL_AppResult result) {
    (void)result;
    auto* app = static_cast<AppState*>(appstate);
    if (!app) return;
    cleanup(*app);
    delete app;
}
