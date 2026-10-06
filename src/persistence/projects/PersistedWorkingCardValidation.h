#pragma once

#include <QJsonObject>

#include <string>

namespace tcgprint::projects {

void validatePersistedWorkingCardReferences(
    const QJsonObject& source,
    int schemaVersion,
    const std::string& path
);

} // namespace tcgprint::projects
