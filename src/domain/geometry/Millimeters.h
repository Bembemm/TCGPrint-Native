#pragma once

#include <compare>

namespace tcgprint::geometry {

class Millimeters final
{
public:
    constexpr explicit Millimeters(double value = 0.0) noexcept
        : value_(value)
    {
    }

    [[nodiscard]] constexpr double value() const noexcept
    {
        return value_;
    }

    [[nodiscard]] constexpr Millimeters operator+(Millimeters other) const noexcept
    {
        return Millimeters(value_ + other.value_);
    }

    [[nodiscard]] constexpr Millimeters operator-(Millimeters other) const noexcept
    {
        return Millimeters(value_ - other.value_);
    }

    [[nodiscard]] constexpr Millimeters operator*(double scalar) const noexcept
    {
        return Millimeters(value_ * scalar);
    }

    [[nodiscard]] constexpr Millimeters operator/(double scalar) const noexcept
    {
        return Millimeters(value_ / scalar);
    }

    constexpr auto operator<=>(const Millimeters&) const noexcept = default;

private:
    double value_;
};

inline constexpr double PointsPerInch = 72.0;
inline constexpr double MillimetersPerInch = 25.4;

[[nodiscard]] constexpr double toPdfPoints(Millimeters value) noexcept
{
    return value.value() * PointsPerInch / MillimetersPerInch;
}

} // namespace tcgprint::geometry
