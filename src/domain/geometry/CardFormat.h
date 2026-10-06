#pragma once

#include "domain/geometry/Millimeters.h"

#include <string>

namespace tcgprint::geometry {

struct CardFormat final
{
    std::string id;
    std::string name;
    Millimeters trimWidth;
    Millimeters trimHeight;
    Millimeters cornerRadius;

    [[nodiscard]] bool isValid() const noexcept;

    [[nodiscard]] static CardFormat magicStandard();
};

} // namespace tcgprint::geometry
