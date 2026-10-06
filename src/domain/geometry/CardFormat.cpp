#include "domain/geometry/CardFormat.h"

namespace tcgprint::geometry {

bool CardFormat::isValid() const noexcept
{
    return !id.empty()
        && !name.empty()
        && trimWidth.value() > 0.0
        && trimHeight.value() > 0.0
        && cornerRadius.value() >= 0.0;
}

CardFormat CardFormat::magicStandard()
{
    return CardFormat{
        .id = "magic-standard",
        .name = "Magic Standard",
        .trimWidth = Millimeters(63.5),
        .trimHeight = Millimeters(88.9),
        .cornerRadius = Millimeters(3.175),
    };
}

} // namespace tcgprint::geometry
