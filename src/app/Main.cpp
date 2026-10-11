#define SDL_MAIN_USE_CALLBACKS 1

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"

#include "editor/EditorInput.hpp"
#include "editor/EditorState.hpp"
#include "editor/EditorUI.hpp"
#include "editor/SceneEditor.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

using wf::editor::AppState;
using wf::editor::build_editor_ui;
using wf::editor::make_demo_scene;
using wf::editor::process_event;
using wf::editor::recalculate_next_id;

namespace {
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

