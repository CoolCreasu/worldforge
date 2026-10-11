#pragma once

namespace wf::render {

// Camera target is the world-space origin for camera-relative calculations.
// Angles are radians; distance is the orbit distance from the target.
struct EditorCamera final {
    double target_x = 0.0;
    double target_y = 0.0;
    double target_z = 0.0;
    double distance = 14.0;
    double yaw = 0.6747409422235527;
    double pitch = 0.4993467216801301;
};

} // namespace wf::render
