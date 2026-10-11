#include "editor/EditorUI.hpp"

#include "editor/SceneEditor.hpp"

#include "math/Transform.hpp"

#include "imgui.h"

#include <algorithm>
#include <string>

namespace wf::editor {
using wf::math::Fixed16;

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

} // namespace wf::editor
