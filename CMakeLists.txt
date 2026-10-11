cmake_minimum_required(VERSION 3.24)
project(Worldforge LANGUAGES CXX)

option(WORLDFORGE_BUILD_APP "Build the SDL3 editor application" ON)
option(WORLDFORGE_BUILD_TESTS "Build dependency-free core tests" ON)
option(WORLDFORGE_FETCH_SDL "Fetch SDL3 if no SDL3 package is installed" ON)
option(WORLDFORGE_FETCH_IMGUI "Fetch Dear ImGui if WORLDFORGE_IMGUI_DIR is not set" ON)
set(WORLDFORGE_IMGUI_DIR "" CACHE PATH "Path to a Dear ImGui source checkout (optional)")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Scene data and fixed-point math deliberately have no SDL or editor-UI dependency.
add_library(worldforge_core STATIC
    src/world/SceneIO.cpp
    src/render/RenderTransforms.cpp
)
target_include_directories(worldforge_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(worldforge_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(worldforge_core PRIVATE /W4 /permissive-)
    target_compile_definitions(worldforge_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(worldforge_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

if(WORLDFORGE_BUILD_TESTS)
    include(CTest)
    if(BUILD_TESTING)
        add_executable(worldforge_core_tests tests/core_tests.cpp)
        target_link_libraries(worldforge_core_tests PRIVATE worldforge_core)
        target_compile_features(worldforge_core_tests PRIVATE cxx_std_20)
        add_test(NAME worldforge_core_tests COMMAND worldforge_core_tests)
    endif()
endif()

if(WORLDFORGE_BUILD_APP)
    include(FetchContent)
    find_package(Threads REQUIRED)

    find_package(SDL3 3.2 CONFIG QUIET)
    if(NOT SDL3_FOUND)
        if(NOT WORLDFORGE_FETCH_SDL)
            message(FATAL_ERROR
                "SDL3 >= 3.2 was not found. Install SDL3 or enable WORLDFORGE_FETCH_SDL.")
        endif()

        set(SDL_TESTS OFF CACHE BOOL "" FORCE)
        set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            SDL3
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG release-3.2.10
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(SDL3)
    endif()

    if(NOT WORLDFORGE_IMGUI_DIR)
        if(NOT WORLDFORGE_FETCH_IMGUI)
            message(FATAL_ERROR
                "Dear ImGui source was not provided. Set WORLDFORGE_IMGUI_DIR or enable WORLDFORGE_FETCH_IMGUI.")
        endif()

        FetchContent_Declare(
            imgui
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG v1.92.5
            GIT_SHALLOW TRUE
        )
        # Dear ImGui is source-only upstream, so MakeAvailable populates it and
        # the targets below compile the selected core and backend translation units.
        FetchContent_MakeAvailable(imgui)
        set(WORLDFORGE_IMGUI_DIR "${imgui_SOURCE_DIR}")
    endif()

    if(NOT EXISTS "${WORLDFORGE_IMGUI_DIR}/imgui.cpp" OR
       NOT EXISTS "${WORLDFORGE_IMGUI_DIR}/backends/imgui_impl_sdlgpu3.cpp")
        message(FATAL_ERROR
            "WORLDFORGE_IMGUI_DIR must point to a Dear ImGui source tree containing the SDL_GPU backend.")
    endif()

    add_library(worldforge_imgui STATIC
        "${WORLDFORGE_IMGUI_DIR}/imgui.cpp"
        "${WORLDFORGE_IMGUI_DIR}/imgui_draw.cpp"
        "${WORLDFORGE_IMGUI_DIR}/imgui_tables.cpp"
        "${WORLDFORGE_IMGUI_DIR}/imgui_widgets.cpp"
        "${WORLDFORGE_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp"
        "${WORLDFORGE_IMGUI_DIR}/backends/imgui_impl_sdlgpu3.cpp"
    )
    target_include_directories(worldforge_imgui PUBLIC
        "${WORLDFORGE_IMGUI_DIR}"
        "${WORLDFORGE_IMGUI_DIR}/backends"
    )
    target_link_libraries(worldforge_imgui PUBLIC SDL3::SDL3)
    target_compile_features(worldforge_imgui PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_definitions(worldforge_imgui PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()

    add_executable(worldforge
        src/app/Main.cpp
        src/editor/SceneEditor.cpp
        src/editor/EditorInput.cpp
        src/editor/EditorUI.cpp
        src/render/PrimitiveRenderer.cpp
        src/render/PrimitiveGeometry.cpp
        src/render/GBuffer.cpp
    )
    target_link_libraries(worldforge PRIVATE
        SDL3::SDL3
        worldforge_core
        worldforge_imgui
        Threads::Threads
    )
    target_compile_features(worldforge PRIVATE cxx_std_20)

    # SDL_main.h belongs in Main.cpp only: SDL owns the entry loop on platforms
    # that use the callback-style application lifecycle.
    if(MSVC)
        target_compile_options(worldforge PRIVATE /W4 /permissive-)
        target_compile_definitions(worldforge PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(worldforge PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    endif()

    # Keep sample worlds and shader assets adjacent to the executable's working directory.
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
endif()
