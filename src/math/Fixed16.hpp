#pragma once

#include <cmath>
#include <compare>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace wf::math {

// Signed Q16.16 fixed point. Arithmetic is checked and throws on overflow;
// it never relies on signed integer wraparound.
class Fixed16 final {
public:
    static constexpr int fractional_bits = 16;
    static constexpr std::int64_t one_raw = std::int64_t{1} << fractional_bits;

    constexpr Fixed16() noexcept = default;

    [[nodiscard]] static constexpr Fixed16 from_raw(std::int32_t raw) noexcept {
        return Fixed16(raw, RawTag{});
    }

    [[nodiscard]] static Fixed16 from_int(std::int32_t value) {
        const auto scaled = static_cast<std::int64_t>(value) * one_raw;
        return from_checked_i64(scaled);
    }

    // Conversion is intended for editor input and render-boundary work, not
    // for fixed-point simulation hot paths. Values are rounded to nearest.
    [[nodiscard]] static Fixed16 from_double(double value) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument("Fixed16 cannot represent NaN or infinity");
        }
        const double scaled = std::round(value * static_cast<double>(one_raw));
        if (scaled < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
            scaled > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
            throw std::overflow_error("Fixed16 value is outside Q16.16 range");
        }
        return from_raw(static_cast<std::int32_t>(scaled));
    }

    [[nodiscard]] constexpr std::int32_t raw() const noexcept { return raw_; }
    [[nodiscard]] constexpr double to_double() const noexcept {
        return static_cast<double>(raw_) / static_cast<double>(one_raw);
    }

    friend Fixed16 operator+(Fixed16 lhs, Fixed16 rhs) {
        return from_checked_i64(static_cast<std::int64_t>(lhs.raw_) + rhs.raw_);
    }

    friend Fixed16 operator-(Fixed16 lhs, Fixed16 rhs) {
        return from_checked_i64(static_cast<std::int64_t>(lhs.raw_) - rhs.raw_);
    }

    friend Fixed16 operator-(Fixed16 value) {
        if (value.raw_ == std::numeric_limits<std::int32_t>::min()) {
            throw std::overflow_error("Fixed16 negation overflow");
        }
        return from_raw(-value.raw_);
    }

    friend Fixed16 operator*(Fixed16 lhs, Fixed16 rhs) {
        const auto product = static_cast<std::int64_t>(lhs.raw_) * rhs.raw_;
        // Signed integer division is defined to truncate toward zero.
        return from_checked_i64(product / one_raw);
    }

    friend Fixed16 operator/(Fixed16 lhs, Fixed16 rhs) {
        if (rhs.raw_ == 0) {
            throw std::domain_error("Fixed16 division by zero");
        }
        const auto numerator = static_cast<std::int64_t>(lhs.raw_) * one_raw;
        return from_checked_i64(numerator / rhs.raw_);
    }

    Fixed16& operator+=(Fixed16 rhs) { return *this = *this + rhs; }
    Fixed16& operator-=(Fixed16 rhs) { return *this = *this - rhs; }
    Fixed16& operator*=(Fixed16 rhs) { return *this = *this * rhs; }
    Fixed16& operator/=(Fixed16 rhs) { return *this = *this / rhs; }

    friend constexpr bool operator==(Fixed16, Fixed16) noexcept = default;
    friend constexpr auto operator<=>(Fixed16, Fixed16) noexcept = default;

private:
    struct RawTag {};
    constexpr Fixed16(std::int32_t raw, RawTag) noexcept : raw_(raw) {}

    [[nodiscard]] static Fixed16 from_checked_i64(std::int64_t raw) {
        if (raw < std::numeric_limits<std::int32_t>::min() ||
            raw > std::numeric_limits<std::int32_t>::max()) {
            throw std::overflow_error("Fixed16 arithmetic overflow");
        }
        return from_raw(static_cast<std::int32_t>(raw));
    }

    std::int32_t raw_ = 0;
};

} // namespace wf::math
