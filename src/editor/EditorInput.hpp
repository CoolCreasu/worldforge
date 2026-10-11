#pragma once

#include "editor/EditorState.hpp"

namespace wf::editor {

void process_event(AppState& app, const SDL_Event& event, bool& quit);

} // namespace wf::editor
