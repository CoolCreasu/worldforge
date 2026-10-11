#pragma once

namespace wf::render::viewport {

// SDL_GPU uses Y-up normalized device coordinates but Y-down pixel coordinates.
// Therefore the top pixel row maps to NDC +1 and the bottom row maps to NDC -1.
// These helpers are shared by projection, screen-space selection, and input rays.
[[nodiscard]] constexpr float ndc_y_up_from_pixel_y(float pixel_y,
                                                    float target_height) noexcept {
    return 1.0f - (2.0f * pixel_y / target_height);
}

[[nodiscard]] constexpr float pixel_y_from_ndc_y_up(float ndc_y,
                                                    float target_height) noexcept {
    return (1.0f - ndc_y) * 0.5f * target_height;
}

} // namespace wf::render::viewport
