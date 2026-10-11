#include "math/Fixed16.hpp"
#include "render/ViewportCoordinates.hpp"
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

    // SDL_GPU's clip-space Y convention must agree with top-left-origin editor input.
    check(wf::render::viewport::clip_y_from_window_y(0.0f, 100.0f) == -1.0f,
          "top of window maps to top clip edge");
    check(wf::render::viewport::clip_y_from_window_y(100.0f, 100.0f) == 1.0f,
          "bottom of window maps to bottom clip edge");
    check(std::abs(wf::render::viewport::window_y_from_clip_y(
                       wf::render::viewport::clip_y_from_window_y(23.0f, 100.0f), 100.0f) - 23.0f) < 1e-4f,
          "window/clip Y conversion round trip");

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
