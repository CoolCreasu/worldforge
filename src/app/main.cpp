#define SDL_MAIN_USE_CALLBACKS 1

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "app/application.hpp"

#include <new>

SDL_AppResult SDLCALL SDL_AppInit(void** appstate, int argc, char* argv[]) {
    if (!appstate) return SDL_APP_FAILURE;

    auto* application = new (std::nothrow) wf::app::Application();
    if (!application) return SDL_APP_FAILURE;

    *appstate = application;
    return application->initialize(argc, argv);
}

SDL_AppResult SDLCALL SDL_AppEvent(void* appstate, SDL_Event* event) {
    auto* application = static_cast<wf::app::Application*>(appstate);
    if (!application) return SDL_APP_CONTINUE;
    return application->handle_event(event);
}

SDL_AppResult SDLCALL SDL_AppIterate(void* appstate) {
    auto* application = static_cast<wf::app::Application*>(appstate);
    if (!application) return SDL_APP_FAILURE;
    return application->iterate();
}

void SDLCALL SDL_AppQuit(void* appstate, SDL_AppResult result) {
    (void)result;
    delete static_cast<wf::app::Application*>(appstate);
}
