#pragma once

#include "domain/cards/WorkingCard.h"

#include <QJsonObject>

namespace tcgprint::projects {

[[nodiscard]] cards::WorkingCard parseWorkingCardDomain(
    const QJsonObject& raw
);

[[nodiscard]] QJsonObject serializeWorkingCardDomain(
    const cards::WorkingCard& card
);

} // namespace tcgprint::projects
