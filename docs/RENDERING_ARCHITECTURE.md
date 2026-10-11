# Worldforge rendering architecture

This document records rendering decisions for the game-specific renderer. These are constraints for future implementation, not a claim that deferred rendering is already implemented.

## Coordinate conventions

- World positions are stored as signed Q16.16 values; rotations are stored as Q2.30 quaternions.
- The editor camera and its working calculations may use floating point. Saved world transforms remain fixed-point.
- Every frame, choose a camera-relative origin (initially the camera position or orbit target). Subtract this origin from fixed-point world positions in a sufficiently wide representation **before** converting the relative displacement to float. Never convert large absolute world positions to float and then subtract.
- Cube meshes now remain in local space, with model, view, and projection matrices sent through SDL_GPU vertex uniforms. Model translations subtract the camera target in double precision before conversion to float. The editor grid intentionally remains CPU-projected so its line widths stay consistent in pixels.
- SDL_GPU uses Y-up normalized device coordinates but top-left-origin viewport coordinates. Any projection from screen-space pixel coordinates back into clip space must invert Y.

## Deferred rendering: initial layout

The scene renderer uses a geometry pass that fills a small G-buffer, followed by a lighting/composition pass. The initial G-buffer will have:

1. **Base color/albedo target** — material color, with an explicit and consistent linear/sRGB policy.
2. **Normal target** — normalized surface normals in a documented coordinate space. The initial implementation should use view-space normals so the lighting pass can reconstruct view-space position from depth and keep lighting calculations coherent.
3. **Hardware depth texture** — depth attachment during the geometry pass and sampleable during deferred lighting. Configure the SDL_GPU texture for both render-target/depth-stencil and sampling usage as required by the SDL version and chosen format.

A later material-properties target can pack surface channels such as metallic and roughness (or smoothness, if that's the game's preferred authoring convention), and potentially ambient-occlusion or other scalar material data. Prefer a packed target to adding separate attachments for every scalar. If smoothness is stored, define whether it is the inverse of perceptual roughness and convert at a single documented boundary.

SDL_GPU render passes can write multiple color attachments at once; the G-buffer pass should use multiple render targets (MRT) instead of separate passes for albedo and normals. The lighting pass samples the G-buffer and depth, then writes the lit result to the viewport's color target. The editor UI is composited after scene lighting.

## Initial resource and pass flow

```text
Scene + camera-relative transforms
               |
               v
       Geometry / G-buffer pass
       - Base color target
       - View-space normal target
       - Sampleable hardware depth
               |
               v
         Deferred lighting
       - Sample G-buffer + depth
       - Reconstruct view position
       - Evaluate game lighting
               |
               v
       Viewport output / swapchain
               |
               v
          Editor UI overlay
```

Resize the G-buffer textures with the viewport, not necessarily the full window. Keep render-target lifetime and recreation in the renderer, and treat the G-buffer as renderer-owned data rather than fields on scene objects. Scene materials should describe semantic properties (base color, normal map, metallic, roughness/smoothness); the renderer decides how those properties are packed into the G-buffer.

## Precision and lighting notes

- Camera-relative coordinates improve floating-point precision, but do not by themselves guarantee deterministic simulation.
- Normals should be transformed by the proper inverse-transpose normal transform if nonuniform scale is supported.
- Clear depth consistently for the chosen depth convention and compare operation; don't mix the current prototype's hand-projected depth with the future matrix-based depth pipeline.
- Choose target formats based on required precision and supported SDL_GPU formats. Validate format support on the selected device before creating the pipeline.
- Start with opaque geometry. Transparency normally needs a separate forward pass because order-independent blending is not provided merely by using a G-buffer.

## Migration sequence

1. Keep the CPU-projected grid separate from the local-space cube mesh; this first GPU-transform step is implemented.
2. The initial offscreen albedo target, normal target, and sampleable depth texture are implemented in `GBuffer`.
3. A fullscreen deferred composition pass currently applies simple directional diffuse lighting; improve the lighting model and material policy before adding metallic/roughness data.
4. Add a packed material-properties target only when the game's actual material properties need it.
5. Add mesh import and prefab material assignment against the renderer's semantic material model.


## Code organization

- `PrimitiveRenderer` owns SDL_GPU resources and orchestrates draw calls.
- `PrimitiveGeometry` builds editor grid vertices and the local-space cube mesh.
- `RenderTransforms` constructs model, view, and projection matrices without depending on SDL, so the math is tested by the dependency-free core suite.
- The vertex shader applies transforms; scene documents remain independent of GPU types.
- `GBuffer` owns the albedo, normal, and sampleable depth textures, including resize and failure cleanup. `PrimitiveRenderer` coordinates the geometry pass and the fullscreen composition pass.
- The first deferred composition shader uses a small ambient term plus directional diffuse lighting; it is a validation baseline, not the eventual PBR model.
