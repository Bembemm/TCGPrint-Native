#pragma once

#include <QSqlDatabase>
#include <QString>

namespace tcgprint::projects {

[[nodiscard]] QString projectDatabasePath(const QString& baseDirectory);

[[nodiscard]] QSqlDatabase openProjectDatabase(
    const QString& databasePath,
    const QString& connectionName
);

[[nodiscard]] QSqlDatabase openProjectDatabaseReadOnly(
    const QString& databasePath,
    const QString& connectionName
);

void closeProjectDatabase(QSqlDatabase& database);

} // namespace tcgprint::projects
