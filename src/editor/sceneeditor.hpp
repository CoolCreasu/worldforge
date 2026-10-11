#pragma once

#include "editor/editorstate.hpp"

namespace wf::editor {

[[nodiscard]] SceneDocument make_demo_scene();
void recalculate_next_id(AppState& app) noexcept;
void push_undo(AppState& app);
void undo(AppState& app);
void redo(AppState& app);
void save_scene(AppState& app);
void add_cube(AppState& app);
void delete_selected(AppState& app);
void move_selected(AppState& app, double dx, double dy, double dz);
void rotate_selected_y(AppState& app, double angle_degrees);

} // namespace wf::editor
