#pragma once

#include "math/Fixed16.hpp"

#include <cstdint>

namespace wf::math {

struct Vec3Fx16 final {
    Fixed16 x{};
    Fixed16 y{};
    Fixed16 z{};

    [[nodiscard]] static Vec3Fx16 zero() noexcept { return {}; }
    [[nodiscard]] static Vec3Fx16 one() noexcept {
        const auto unit = Fixed16::from_raw(static_cast<std::int32_t>(Fixed16::one_raw));
        return {unit, unit, unit};
    }
    friend constexpr bool operator==(const Vec3Fx16&, const Vec3Fx16&) noexcept = default;
};

// Quaternion components use signed Q2.30 (30 fractional bits). This permits
// the exact identity component +1.0, unlike signed Q1.31.
struct QuatQ30 final {
    static constexpr std::int64_t one_raw = std::int64_t{1} << 30;

    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
    std::int32_t w = static_cast<std::int32_t>(one_raw);

    [[nodiscard]] static constexpr QuatQ30 identity() noexcept { return {}; }
    friend constexpr bool operator==(const QuatQ30&, const QuatQ30&) noexcept = default;
};

struct Transform final {
    Vec3Fx16 position{};
    QuatQ30 rotation{};
    Vec3Fx16 scale = Vec3Fx16::one();

    [[nodiscard]] static Transform identity() noexcept { return {}; }
    friend constexpr bool operator==(const Transform&, const Transform&) noexcept = default;
};

} // namespace wf::math
