# Roadmap

This is a directional list, not a schedule. Priorities may change as the needs of the game become clearer.

## Rendering foundation

- [x] Replace CPU-projected cube vertices with local mesh vertices and GPU-side model/view/projection transforms (grid stays screen-space for stable line widths).
- [x] Make camera-relative positions explicit in model-matrix construction; subtract fixed-point position minus camera target in double precision before converting to float.
- [x] Implement the first deferred geometry pass with base-color/albedo and normal targets plus sampleable hardware depth.
- [x] Add an initial deferred lighting/composition pass (simple directional diffuse; PBR and material channels remain future work).
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
