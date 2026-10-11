#include "math/Fixed16.hpp"
#include "render/ViewportCoordinates.hpp"
#include "render/RenderTransforms.hpp"
#include "math/Transform.hpp"
#include "world/SceneIO.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <typename F>
void check_throws(F&& fn, const char* message) {
    try {
        fn();
        check(false, message);
    } catch (const std::exception&) {
        // Expected.
    }
}
}

int main() {
    using wf::math::Fixed16;

    // SDL_GPU NDC is Y-up; viewport/input pixels are top-left-origin and Y-down.
    check(wf::render::viewport::ndc_y_up_from_pixel_y(0.0f, 100.0f) == 1.0f,
          "top pixel row maps to NDC Y=+1");
    check(wf::render::viewport::ndc_y_up_from_pixel_y(100.0f, 100.0f) == -1.0f,
          "bottom pixel row maps to NDC Y=-1");
    check(wf::render::viewport::pixel_y_from_ndc_y_up(1.0f, 100.0f) == 0.0f,
          "NDC top edge maps to the top pixel row");
    check(wf::render::viewport::pixel_y_from_ndc_y_up(-1.0f, 100.0f) == 100.0f,
          "NDC bottom edge maps to the bottom pixel row");
    check(wf::render::viewport::pixel_y_from_ndc_y_up(0.5f, 100.0f) < 50.0f,
          "positive NDC Y is above the screen center");
    check(std::abs(wf::render::viewport::pixel_y_from_ndc_y_up(
                       wf::render::viewport::ndc_y_up_from_pixel_y(23.0f, 100.0f), 100.0f) - 23.0f) < 1e-4f,
          "pixel/NDC Y conversion round trip");

    // The target is on the camera's viewing axis and must project to screen center.
    const wf::render::EditorCamera test_camera{};
    const auto view = wf::render::detail::make_view_matrix(test_camera);
    const auto projection = wf::render::detail::make_projection_matrix(16.0f / 9.0f);
    check(std::abs(view.at(0, 3)) < 1.0e-4f && std::abs(view.at(1, 3)) < 1.0e-4f,
          "camera target has no horizontal or vertical view offset");
    check(std::abs(view.at(2, 3) - static_cast<float>(test_camera.distance)) < 1.0e-4f,
          "camera target is in front of the camera by the orbit distance");
    check(projection.at(0, 0) > 0.0f && projection.at(1, 1) > 0.0f &&
          projection.at(2, 2) > 0.0f && projection.at(3, 2) == 1.0f,
          "projection matrix uses perspective with forward-positive depth");

    wf::world::ObjectInstance transform_test_object;
    transform_test_object.transform.position = {Fixed16::from_double(100.25),
                                                Fixed16::from_double(-2.0),
                                                Fixed16::from_double(13.5)};
    wf::render::EditorCamera precision_camera{};
    precision_camera.target_x = 100.0;
    precision_camera.target_y = -2.0;
    precision_camera.target_z = 13.0;
    const auto model = wf::render::detail::make_model_matrix(transform_test_object, precision_camera);
    check(std::abs(model.at(0, 3) - 0.25f) < 1.0e-5f &&
          std::abs(model.at(1, 3)) < 1.0e-5f &&
          std::abs(model.at(2, 3) - 0.5f) < 1.0e-5f,
          "model matrix subtracts camera-relative origin before float conversion");

    const auto one = Fixed16::from_int(1);
    const auto half = Fixed16::from_double(0.5);
    check(one.raw() == 65536, "Q16.16 represents one exactly");
    check((one + half).raw() == 98304, "addition");
    check((one * half).raw() == 32768, "multiplication");
    check((one / half).raw() == 131072, "division");
    check(std::abs(Fixed16::from_double(-12.25).to_double() + 12.25) < 1e-9,
          "negative fixed-point roundtrip");
    check_throws([] { (void)Fixed16::from_double(std::numeric_limits<double>::infinity()); },
                 "reject infinity");
    check_throws([] { (void)(Fixed16::from_raw(std::numeric_limits<std::int32_t>::max()) +
                                Fixed16::from_raw(1)); }, "detect addition overflow");
    check_throws([] { (void)(Fixed16::from_int(1) / Fixed16{}); }, "detect division by zero");

    wf::world::SceneDocument original;
    original.name = "Round-trip test world";
    wf::world::ObjectInstance object;
    object.id = 42;
    object.prefab = "prefabs/stone arch";
    object.layer = "Architecture / Exterior";
    object.transform.position = {Fixed16::from_double(10.25), Fixed16::from_int(-3),
                                 Fixed16::from_double(0.125)};
    object.transform.rotation = {123, -456, 789, static_cast<std::int32_t>(1LL << 30)};
    object.transform.scale = {Fixed16::from_int(2), Fixed16::from_int(1), Fixed16::from_int(3)};
    original.objects.push_back(object);

    const auto test_path = std::filesystem::temp_directory_path() / "worldforge-core-test.world";
    std::string error;
    check(wf::world::save_scene(test_path, original, error), "save scene: error should be empty on success");
    wf::world::SceneDocument restored;
    check(wf::world::load_scene(test_path, restored, error), "load scene: error should be empty on success");
    check(restored.name == original.name, "scene name survives roundtrip");
    check(restored.objects == original.objects, "objects survive exact raw-value roundtrip");
    check(restored.find(42) != nullptr, "scene lookup by stable object ID");
    restored.name = "Updated after reload";
    check(wf::world::save_scene(test_path, restored, error), "replace existing scene file");
    wf::world::SceneDocument replaced;
    check(wf::world::load_scene(test_path, replaced, error), "reload replaced scene file");
    check(replaced.name == "Updated after reload", "replacement becomes the saved scene");

    wf::world::SceneDocument invalid = original;
    invalid.objects.push_back(object);
    check(!wf::world::save_scene(test_path, invalid, error), "duplicate object IDs rejected");

    std::error_code ec;
    std::filesystem::remove(test_path, ec);
    std::filesystem::remove(test_path.string() + ".tmp", ec);
    std::filesystem::remove(test_path.string() + ".bak", ec);

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All core tests passed.\n";
    return 0;
}
