#include "persistence/projects/ProjectDatabase.h"

#include "persistence/projects/ProjectRepository.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>

#include <stdexcept>

namespace tcgprint::projects {

QString projectDatabasePath(const QString& baseDirectory)
{
    QDir base(baseDirectory);
    return QDir::cleanPath(
        base.filePath(QStringLiteral(".tcgprint/projects.sqlite"))
    );
}

QSqlDatabase openProjectDatabase(
    const QString& databasePath,
    const QString& connectionName
)
{
    if (connectionName.trimmed().isEmpty()) {
        throw std::invalid_argument(
            "SQLite connection name must not be empty."
        );
    }

    if (QSqlDatabase::contains(connectionName)) {
        throw std::invalid_argument(
            "SQLite connection name is already in use."
        );
    }

    if (databasePath != QStringLiteral(":memory:")) {
        const QFileInfo info(databasePath);
        QDir directory;
        if (!directory.mkpath(info.absolutePath())) {
            throw ProjectRepositoryError(
                ProjectRepositoryErrorCode::DatabaseError,
                "Could not create Project database directory."
            );
        }
    }

    QSqlDatabase database =
        QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    database.setDatabaseName(databasePath);

    if (!database.open()) {
        const QString message = database.lastError().text();
        database = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);

        throw ProjectRepositoryError(
            ProjectRepositoryErrorCode::DatabaseError,
            (
                QStringLiteral("Could not open Project database: ")
                + message
            ).toStdString()
        );
    }

    try {
        initializeProjectDatabase(database);
        return database;
    } catch (...) {
        database.close();
        database = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName);
        throw;
    }
}

void closeProjectDatabase(QSqlDatabase& database)
{
    if (!database.isValid()) {
        return;
    }

    const QString connectionName = database.connectionName();
    database.close();
    database = QSqlDatabase();

    if (!connectionName.isEmpty()) {
        QSqlDatabase::removeDatabase(connectionName);
    }
}

} // namespace tcgprint::projects
