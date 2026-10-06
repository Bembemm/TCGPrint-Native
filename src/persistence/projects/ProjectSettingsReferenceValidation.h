#pragma once

#include <QJsonObject>

namespace tcgprint::projects {

void validateProjectSettingsReferences(
    const QJsonObject& settings
);

} // namespace tcgprint::projects
