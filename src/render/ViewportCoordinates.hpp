#pragma once

namespace wf::render::viewport {

// SDL_GPU window/viewport coordinates are top-left-origin. In the clip-space
// convention used by this renderer, NDC Y=-1 maps to the top edge and Y=+1
// maps to the bottom edge. Keep these conversions shared by rendering and picking.
[[nodiscard]] constexpr float clip_y_from_window_y(float window_y, float window_height) noexcept {
    return (2.0f * window_y / window_height) - 1.0f;
}

[[nodiscard]] constexpr float window_y_from_clip_y(float clip_y, float window_height) noexcept {
    return (clip_y + 1.0f) * 0.5f * window_height;
}

} // namespace wf::render::viewport
