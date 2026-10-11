# Roadmap

This is a directional list, not a schedule. Priorities may change as the needs of the game become clearer.

## Rendering foundation

- [ ] Replace CPU-projected primitive vertices with local mesh vertices and GPU-side model/view/projection transforms.
- [ ] Make camera-relative positions explicit in the renderer interface; subtract the chosen world origin before converting fixed-point displacement to float.
- [ ] Implement the first deferred geometry pass with base-color/albedo and normal targets plus sampleable hardware depth.
- [ ] Add a deferred lighting/composition pass.
- [ ] Add Direct3D 12/DXIL shader support, or a shader-cross-compilation step, and test the available SDL_GPU backends on real target platforms.

## World creation

- [ ] Add real mesh loading and a mesh/texture asset registry.
- [ ] Introduce reusable prefab definitions after the first asset pipeline is working.
- [ ] Add robust ray-based picking and transform gizmos.
- [ ] Improve multi-selection and batch editing for large scenes.
- [ ] Add scene validation and recovery/autosave.

## Game integration

- [ ] Define the minimum game-specific object types and properties.
- [ ] Separate edit-mode scene documents from runtime gameplay state.
- [ ] Add play mode using the same authored world data.
- [ ] Decide whether gameplay simulation needs deterministic fixed-point arithmetic beyond saved transform storage.

## Principles

- Build only the tooling that materially helps create this game.
- Keep world documents independent from GPU implementation details.
- Keep saved positions in Q16.16 and quaternion storage in Q2.30 unless a measured requirement changes that decision.
- Preserve a small, testable core that does not require the SDL editor dependencies.
