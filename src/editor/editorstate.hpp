#pragma once

#include "render/primitiverenderer.hpp"
#include "world/sceneio.hpp"

#include <SDL3/SDL.h>

#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace wf::editor {

using wf::world::ObjectId;
using wf::world::ObjectInstance;
using wf::world::SceneDocument;
using wf::render::EditorCamera;
using wf::render::PrimitiveRenderer;

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

} // namespace wf::editor
