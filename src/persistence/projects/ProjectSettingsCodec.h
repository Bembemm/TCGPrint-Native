#pragma once

#include "domain/projects/ProjectSettings.h"

#include <QJsonObject>

namespace tcgprint::projects {

[[nodiscard]] ProjectPrintSettings parseProjectPrintSettings(
    const QJsonObject& normalizedSettings
);

[[nodiscard]] QJsonObject overlayProjectPrintSettings(
    const QJsonObject& compatibleSettings,
    const ProjectPrintSettings& printSettings
);

} // namespace tcgprint::projects
