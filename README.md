# Worldforge

**Worldforge is a small, game-specific world editor and runtime foundation written in C++20, SDL3, and SDL_GPU.** Its priority is making it easy to create and edit worlds for one game—not providing a universal engine for every possible game.

> **Project status:** early prototype. The scene editor currently works with built-in cube primitives. General model importing, prefab authoring, and the planned deferred renderer are not implemented yet.

## Current features

- SDL3 callback lifecycle: `SDL_AppInit`, `SDL_AppEvent`, `SDL_AppIterate`, and `SDL_AppQuit`.
- SDL_GPU-based primitive rendering, a ground grid, depth buffering, and GPU buffer uploads.
- Dear ImGui interface using its SDL3 platform and SDL_GPU renderer backends.
- Scene outliner, transform inspector, object creation/deletion, save, undo, and redo.
- Click-to-select and drag-to-move interaction, camera orbit/zoom, and keyboard shortcuts.
- Versioned text scene files that preserve raw fixed-point transform values.
- Q16.16 world positions/scales and Q2.30 quaternion storage.
- Dependency-free tests for core math and scene serialization.

## Current limitations

- The viewport uses built-in cubes, not imported meshes. The current prototype CPU-projects primitive vertices before uploading them; a conventional GPU transform pipeline is planned next.
- The editor has no general model importer, full transform gizmos, prefab override system, collision/physics, or production gameplay runtime yet.
- The intended renderer is deferred, but the G-buffer and lighting passes are not implemented. See [Rendering architecture](docs/RENDERING_ARCHITECTURE.md).
- Shaders are currently provided as SPIR-V for Vulkan and MSL for Metal. Direct3D 12/DXIL shader support is not yet included, so do not assume the app can start on every SDL_GPU backend.
- SDL's callback lifecycle is used, but actual support still depends on the target platform's available SDL_GPU backend and shader format.

## Requirements

- CMake 3.24 or later.
- A C++20 compiler. On Windows, Visual Studio 2022 with the C++ desktop development workload is recommended.
- For the application: SDL 3.2 or later and Dear ImGui v1.92.5 with its SDL3 and SDL_GPU backends. By default, CMake fetches the pinned upstream sources when suitable local dependencies are not found.

See [Build dependencies](DEPENDENCIES.md) for upstream repositories and pinned versions. The first app configuration needs Internet access unless the dependencies are available locally.

## Build on Visual Studio 2022

1. Clone this repository or download the source ZIP and extract it.
2. In Visual Studio, select **File → Open → Folder…** and open the folder containing `CMakeLists.txt`. Do not create a separate empty `.vcxproj`; CMake configures the C++20 language mode and required Windows compile definitions.
3. Allow CMake to configure. The first configuration may download SDL and Dear ImGui.
4. Select the `worldforge` startup target and build the x64 Debug configuration.
5. Run the target. The CMake project copies the `assets` directory into the build directory so the sample scene and shaders can be found.

If Visual Studio continues to show stale IntelliSense diagnostics after a source update, close the folder, remove the generated build directory (often under `out/build/`), then reopen the project and let CMake configure again. When diagnosing compile failures, use the first relevant error in **Build Output**, not just the Error List.

## Build from the command line

Build the editor application and core tests:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Run the application from its build directory so relative asset paths resolve. For example, with a single-configuration generator on Linux:

```sh
(cd build && ./worldforge)
```

With a multi-configuration Windows generator, the executable is commonly under `build/Debug/` or the corresponding Visual Studio output directory.

### Build only the core tests

The fixed-point and scene-serialization core deliberately has no SDL or Dear ImGui dependency. To configure and test only that code, without downloading the application dependencies:

```sh
cmake -S . -B build-core \
  -DWORLDFORGE_BUILD_APP=OFF \
  -DWORLDFORGE_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-core --config Debug
ctest --test-dir build-core -C Debug --output-on-failure
```

### Use locally installed dependencies

If SDL3 is installed with a discoverable CMake package, CMake uses it. To prevent SDL from being fetched when no package is found, configure with:

```sh
cmake -S . -B build -DWORLDFORGE_FETCH_SDL=OFF
```

If Dear ImGui is already checked out locally, provide its source directory:

```sh
cmake -S . -B build -DWORLDFORGE_IMGUI_DIR=/path/to/imgui
```

The source tree must contain `imgui.cpp` and `backends/imgui_impl_sdlgpu3.cpp`.

## Code layout

The implementation is split by responsibility: SDL lifecycle in `src/app/Main.cpp`, editor state and interaction in `src/editor/`, GPU resource management in `src/render/PrimitiveRenderer.cpp`, and transitional CPU geometry/picking in `src/render/PrimitiveGeometry.cpp`. See [Code structure](docs/CODE_STRUCTURE.md) for the module map and intended boundaries.

## Controls

| Input | Action |
|---|---|
| Click a cube | Select it |
| Left-drag a selected cube | Move it across the ground plane |
| Right-drag | Orbit the camera |
| Mouse wheel over the viewport | Zoom the camera |
| `A` | Add a cube |
| `Delete` / `Backspace` | Delete the selected object |
| Arrow keys | Move the selected object along X/Z |
| `Page Up` / `Page Down` | Move the selected object along Y |
| `Q` / `E` | Rotate the selected object by 15 degrees |
| `F` | Focus the camera on the selected object |
| `Shift` + movement key | Use a smaller movement step |
| `Ctrl+S` | Save the world file |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo |
| `Esc` | Close the application |

The toolbar and inspector also provide mouse-accessible controls for the main editing operations.

## Scene files

World files are versioned text files under `assets/scenes/`. Transform values are stored as raw integers so serialization does not introduce float rounding.

```text
WORLDFORGE_SCENE 1
NAME "Starter World"
OBJECTS 1
OBJECT 1 "primitive.cube" "Default" 0 0 0 0 0 0 1073741824 65536 65536 65536
```

An `OBJECT` record stores the object ID, quoted prefab key, quoted layer, position XYZ in Q16.16 raw units, quaternion XYZW in Q2.30 raw units, and scale XYZ in Q16.16 raw units. Object ID `0` is reserved as invalid.

## Math and rendering policy

- `Fixed16` stores signed Q16.16 values. Its arithmetic checks overflow; multiplication/division truncate toward zero, while conversion from `double` rounds to nearest.
- `QuatQ30` stores quaternion components in Q2.30. The current editor uses floating-point working calculations when rotating, then normalizes and quantizes the result.
- Camera-relative rendering is the intended policy: subtract a nearby world origin before converting relative positions to float. The present CPU-projected primitive renderer is transitional; the future renderer will transform local mesh vertices on the GPU.
- Fixed-point storage alone does not make gameplay deterministic. Determinism requires a consistent simulation and collision/physics math policy as well.
- The intended deferred renderer starts with base color/albedo and normal color targets plus a sampleable hardware depth texture. A packed material-properties target can be added later for metallic and roughness/smoothness data.

More detail is in [docs/RENDERING_ARCHITECTURE.md](docs/RENDERING_ARCHITECTURE.md). Planned milestones are tracked in [ROADMAP.md](ROADMAP.md).

## Continuous integration

The GitHub Actions workflow builds and runs the dependency-free core tests on Windows, macOS, and Linux. It does not currently build or launch the SDL_GPU editor application on those runners.

## License

No project license has been selected for this repository yet. Until a license is added, assume that the project code is **all rights reserved**; making the repository public does not by itself grant permission to reuse, modify, or redistribute it. Choose and add a license before inviting external code contributions or promising that others may reuse the project.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the project's focus and test commands. Please keep changes scoped to the needs of this specific game and update tests or documentation when changing core behavior or the world-file format.
