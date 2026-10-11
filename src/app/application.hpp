#pragma once

#include "editor/editorstate.hpp"

#include <SDL3/SDL.h>

namespace wf::app {

class Application final {
public:
    Application() = default;
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    [[nodiscard]] SDL_AppResult initialize(int argc, char* argv[]);
    [[nodiscard]] SDL_AppResult handle_event(const SDL_Event* event);
    [[nodiscard]] SDL_AppResult iterate();
    void shutdown() noexcept;

private:
    [[nodiscard]] SDL_AppResult fail(const char* action);

    wf::editor::AppState state_;
};

} // namespace wf::app
