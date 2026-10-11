# Contributing to Worldforge

Worldforge is a deliberately game-specific editor/runtime prototype. Prefer a small implementation that supports the intended game over a general-purpose framework.

## Before opening an issue

- For build errors, include the operating system, compiler, CMake version, SDL version, and the first relevant error from the build output.
- For a bug, include reproducible steps and expected versus actual behavior.
- Do not attach secrets, private files, or unrelated large build directories.

## Build and test

The core tests do not require SDL3 or Dear ImGui:

```sh
cmake -S . -B build-core -DWORLDFORGE_BUILD_APP=OFF -DWORLDFORGE_BUILD_TESTS=ON
cmake --build build-core --config Debug
ctest --test-dir build-core -C Debug --output-on-failure
```

To build the editor application, use the instructions in [README.md](README.md). The default configuration fetches pinned SDL and Dear ImGui sources when suitable local dependencies are not available.

## Design guidelines

- Keep saved world positions in Q16.16 and quaternion storage in Q2.30 unless an explicit design change is discussed.
- Keep scene serialization and fixed-point core code independent from SDL and editor UI code.
- Keep renderer resources inside the rendering module and scene documents independent of GPU implementation details.
- Update tests and documentation when changing file formats, coordinate conventions, or build requirements.
- Avoid adding dependencies unless they materially help build this specific game.
