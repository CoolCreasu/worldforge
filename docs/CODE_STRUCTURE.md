# Code structure

Worldforge is intentionally organized by responsibility rather than by a generic engine-layer hierarchy.

## Application lifecycle

- `src/app/main.cpp` contains SDL3's callback entry points, startup/shutdown, and frame submission.
- `src/editor/editorstate.hpp` holds state shared by the editor modules.
- `src/editor/editorinput.cpp` handles keyboard/mouse events, selection and drag interaction.
- `src/editor/editorui.cpp` builds the Dear ImGui toolbar, outliner, inspector and status bar.
- `src/editor/sceneeditor.cpp` implements scene editing operations, object creation, transform edits, and undo/redo.

## Rendering

- `src/render/primitiverenderer.hpp` exposes the renderer interface and editor-camera types.
- `src/render/primitiverenderer.cpp` owns SDL_GPU pipelines, buffers, upload buffers, depth resources, and command recording.
- `src/render/primitivegeometry.cpp` contains the transitional CPU-side camera math, primitive/grid generation, projection, and picking rays.
- `src/render/primitivegeometry.hpp` describes the generated vertex format and geometry-generation boundary.
- `assets/shaders/` contains the current primitive shaders.

The primitive geometry module is intentionally transitional: the planned next renderer will submit local mesh vertices and camera-relative transforms to GPU-side shaders. Keeping geometry generation separate from GPU resource management makes that change easier to review.

## World and math

- `src/world/scenedocument.hpp` defines editable scene data.
- `src/world/sceneio.cpp` reads and writes versioned world files.
- `src/math/` contains fixed-point scalar and transform representations.
- `tests/core_tests.cpp` exercises the SDL-independent core.

When adding a feature, place it near the responsibility it changes. Keep SDL_GPU details out of world serialization and keep editor widgets out of scene-data types.
