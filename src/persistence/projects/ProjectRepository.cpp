#include "persistence/projects/ProjectRepository.h"

#include <QDateTime>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

#include <utility>

namespace tcgprint::projects {
namespace {

[[noreturn]] void databaseError(
    const QString& context,
    const QSqlError& error
)
{
    throw ProjectRepositoryError(
        ProjectRepositoryErrorCode::DatabaseError,
        (context + QStringLiteral(": ") + error.text()).toStdString()
    );
}

void execOrThrow(QSqlQuery& query, const QString& sql)
{
    if (!query.exec(sql)) {
        databaseError(QStringLiteral("SQLite statement failed"), query.lastError());
    }
}

int databaseUserVersion(QSqlDatabase& database)
{
    QSqlQuery query(database);
    execOrThrow(query, QStringLiteral("PRAGMA user_version"));
    if (!query.next()) {
        throw ProjectRepositoryError(
            ProjectRepositoryErrorCode::DatabaseError,
            "Could not read SQLite user_version."
        );
    }
    return query.value(0).toInt();
}

bool tableExists(QSqlDatabase& database, const QString& tableName)
{
    QSqlQuery query(database);
    query.prepare(
        QStringLiteral(
            "SELECT 1 FROM sqlite_master "
            "WHERE type = 'table' AND name = ?"
        )
    );
    query.addBindValue(tableName);

    if (!query.exec()) {
        databaseError(QStringLiteral("Could not inspect SQLite schema"), query.lastError());
    }

    return query.next();
}

void requireCompatibleTables(QSqlDatabase& database)
{
    if (
        !tableExists(database, QStringLiteral("projects"))
        || !tableExists(database, QStringLiteral("project_recovery"))
    ) {
        throw ProjectRepositoryError(
            ProjectRepositoryErrorCode::InvalidDatabaseSchema,
            "Project database is missing required projects/recovery tables."
        );
    }
}

void beginImmediate(QSqlDatabase& database)
{
    QSqlQuery query(database);
    execOrThrow(query, QStringLiteral("BEGIN IMMEDIATE"));
}

void commit(QSqlDatabase& database)
{
    QSqlQuery query(database);
    execOrThrow(query, QStringLiteral("COMMIT"));
}

void rollbackNoThrow(QSqlDatabase& database) noexcept
{
    QSqlQuery query(database);
    query.exec(QStringLiteral("ROLLBACK"));
}

std::string nowIso()
{
    return QDateTime::currentDateTimeUtc()
        .toString(Qt::ISODateWithMs)
        .toStdString();
}

std::string newProjectId()
{
    return QUuid::createUuid()
        .toString(QUuid::WithoutBraces)
        .toStdString();
}

ProjectMetadata metadataFromQuery(const QSqlQuery& query)
{
    return ProjectMetadata{
        .id = query.value(QStringLiteral("id")).toString().toStdString(),
        .name = query.value(QStringLiteral("name")).toString().toStdString(),
        .projectSchemaVersion =
            query.value(QStringLiteral("project_schema_version")).toInt(),
        .revision = query.value(QStringLiteral("revision")).toInt(),
        .createdAt = query.value(QStringLiteral("created_at")).toString().toStdString(),
        .updatedAt = query.value(QStringLiteral("updated_at")).toString().toStdString(),
        .autosavedAt = query.value(QStringLiteral("autosaved_at")).toString().toStdString(),
    };
}

ProjectRecord recordFromQuery(const QSqlQuery& query)
{
    ProjectMetadata metadata = metadataFromQuery(query);
    ProjectSnapshotCompat snapshot = deserializeProjectSnapshot(
        query.value(QStringLiteral("snapshot_json")).toByteArray()
    );

    if (
        metadata.projectSchemaVersion >= CurrentProjectSchemaVersion
        && metadata.projectSchemaVersion != snapshot.projectSchemaVersion
    ) {
        throw ProjectRepositoryError(
            ProjectRepositoryErrorCode::InvalidDatabaseSchema,
            "Project database and snapshot schema versions do not match."
        );
    }

    metadata.projectSchemaVersion = snapshot.projectSchemaVersion;

    return ProjectRecord{
        .metadata = std::move(metadata),
        .snapshot = std::move(snapshot),
    };
}

ProjectRepositoryError notFound(const std::string& projectId)
{
    return ProjectRepositoryError(
        ProjectRepositoryErrorCode::ProjectNotFound,
        "Project " + projectId + " was not found."
    );
}

void bindProjectId(QSqlQuery& query, const std::string& projectId)
{
    query.addBindValue(QString::fromStdString(projectId));
}

} // namespace

ProjectRepositoryError::ProjectRepositoryError(
    ProjectRepositoryErrorCode code,
    std::string message,
    std::optional<int> expectedRevision,
    std::optional<int> actualRevision
)
    : std::runtime_error(std::move(message)),
      code_(code),
      expectedRevision_(expectedRevision),
      actualRevision_(actualRevision)
{
}

ProjectRepositoryErrorCode ProjectRepositoryError::code() const noexcept
{
    return code_;
}

std::optional<int> ProjectRepositoryError::expectedRevision() const noexcept
{
    return expectedRevision_;
}

std::optional<int> ProjectRepositoryError::actualRevision() const noexcept
{
    return actualRevision_;
}

void initializeProjectDatabase(QSqlDatabase& database)
{
    if (!database.isValid() || !database.isOpen()) {
        throw ProjectRepositoryError(
            ProjectRepositoryErrorCode::DatabaseError,
            "Project database must be open before initialization."
        );
    }

    {
        QSqlQuery query(database);
        execOrThrow(query, QStringLiteral("PRAGMA foreign_keys = ON"));
    }

    const int version = databaseUserVersion(database);

    if (version > LegacyProjectDatabaseSchemaMax) {
        throw ProjectRepositoryError(
            ProjectRepositoryErrorCode::InvalidDatabaseSchema,
            "Project database schema is newer than this native compatibility layer supports."
        );
    }

    if (version == 0) {
        beginImmediate(database);
        try {
            QSqlQuery query(database);
            execOrThrow(
                query,
                QStringLiteral(
                    "CREATE TABLE projects ("
                    "id TEXT PRIMARY KEY NOT NULL,"
                    "name TEXT NOT NULL,"
                    "project_schema_version INTEGER NOT NULL CHECK (project_schema_version >= 1),"
                    "revision INTEGER NOT NULL CHECK (revision >= 1),"
                    "snapshot_json TEXT NOT NULL,"
                    "created_at TEXT NOT NULL,"
                    "updated_at TEXT NOT NULL,"
                    "autosaved_at TEXT NOT NULL"
                    ")"
                )
            );
            execOrThrow(
                query,
                QStringLiteral(
                    "CREATE TABLE project_recovery ("
                    "project_id TEXT PRIMARY KEY NOT NULL REFERENCES projects(id) ON DELETE CASCADE,"
                    "base_revision INTEGER NOT NULL CHECK (base_revision >= 1),"
                    "project_schema_version INTEGER NOT NULL CHECK (project_schema_version >= 1),"
                    "snapshot_json TEXT NOT NULL,"
                    "created_at TEXT NOT NULL"
                    ")"
                )
            );
            execOrThrow(query, QStringLiteral("PRAGMA user_version = 1"));
            commit(database);
        } catch (...) {
            rollbackNoThrow(database);
            throw;
        }
    } else {
        requireCompatibleTables(database);
    }
}

ProjectRepository::ProjectRepository(QSqlDatabase database)
    : database_(std::move(database))
{
    initializeProjectDatabase(database_);
}

std::vector<ProjectMetadata> ProjectRepository::list() const
{
    QSqlQuery query(database_);
    execOrThrow(
        query,
        QStringLiteral(
            "SELECT id, name, project_schema_version, revision, "
            "created_at, updated_at, autosaved_at "
            "FROM projects "
            "ORDER BY updated_at DESC, id ASC"
        )
    );

    std::vector<ProjectMetadata> result;
    while (query.next()) {
        result.push_back(metadataFromQuery(query));
    }
    return result;
}

std::optional<ProjectRecord> ProjectRepository::get(
    const std::string& projectId
) const
{
    QSqlQuery query(database_);
    query.prepare(
        QStringLiteral(
            "SELECT id, name, project_schema_version, revision, snapshot_json, "
            "created_at, updated_at, autosaved_at "
            "FROM projects WHERE id = ?"
        )
    );
    bindProjectId(query, projectId);

    if (!query.exec()) {
        databaseError(QStringLiteral("Could not read Project"), query.lastError());
    }

    if (!query.next()) {
        return std::nullopt;
    }

    return recordFromQuery(query);
}

ProjectRecord ProjectRepository::open(const std::string& projectId) const
{
    const auto project = get(projectId);
    if (!project) {
        throw notFound(projectId);
    }
    return *project;
}

ProjectRecord ProjectRepository::create(
    const ProjectSnapshotCompat& snapshot,
    std::string name
)
{
    const QByteArray snapshotJson = serializeProjectSnapshot(snapshot);
    const ProjectSnapshotCompat canonical =
        deserializeProjectSnapshot(snapshotJson);

    const std::string id = newProjectId();
    const std::string timestamp = nowIso();

    QSqlQuery query(database_);
    query.prepare(
        QStringLiteral(
            "INSERT INTO projects "
            "(id, name, project_schema_version, revision, snapshot_json, "
            "created_at, updated_at, autosaved_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"
        )
    );
    query.addBindValue(QString::fromStdString(id));
    query.addBindValue(QString::fromStdString(name));
    query.addBindValue(CurrentProjectSchemaVersion);
    query.addBindValue(InitialProjectRevision);
    query.addBindValue(snapshotJson);
    query.addBindValue(QString::fromStdString(timestamp));
    query.addBindValue(QString::fromStdString(timestamp));
    query.addBindValue(QString::fromStdString(timestamp));

    if (!query.exec()) {
        databaseError(QStringLiteral("Could not create Project"), query.lastError());
    }

    return ProjectRecord{
        .metadata = ProjectMetadata{
            .id = id,
            .name = std::move(name),
            .projectSchemaVersion = CurrentProjectSchemaVersion,
            .revision = InitialProjectRevision,
            .createdAt = timestamp,
            .updatedAt = timestamp,
            .autosavedAt = timestamp,
        },
        .snapshot = canonical,
    };
}

ProjectRecord ProjectRepository::save(
    const std::string& projectId,
    int expectedRevision,
    const ProjectSnapshotCompat& snapshot
)
{
    if (expectedRevision < 1) {
        throw ProjectRepositoryError(
            ProjectRepositoryErrorCode::ProjectRevisionConflict,
            "Expected Project revision must be positive.",
            expectedRevision
        );
    }

    const QByteArray snapshotJson = serializeProjectSnapshot(snapshot);
    const std::string timestamp = nowIso();

    QSqlQuery update(database_);
    update.prepare(
        QStringLiteral(
            "UPDATE projects "
            "SET project_schema_version = ?, revision = revision + 1, "
            "snapshot_json = ?, updated_at = ?, autosaved_at = ? "
            "WHERE id = ? AND revision = ?"
        )
    );
    update.addBindValue(CurrentProjectSchemaVersion);
    update.addBindValue(snapshotJson);
    update.addBindValue(QString::fromStdString(timestamp));
    update.addBindValue(QString::fromStdString(timestamp));
    update.addBindValue(QString::fromStdString(projectId));
    update.addBindValue(expectedRevision);

    if (!update.exec()) {
        databaseError(QStringLiteral("Could not save Project"), update.lastError());
    }

    if (update.numRowsAffected() != 1) {
        QSqlQuery current(database_);
        current.prepare(QStringLiteral("SELECT revision FROM projects WHERE id = ?"));
        bindProjectId(current, projectId);

        if (!current.exec()) {
            databaseError(QStringLiteral("Could not resolve revision conflict"), current.lastError());
        }

        if (!current.next()) {
            throw notFound(projectId);
        }

        throw ProjectRepositoryError(
            ProjectRepositoryErrorCode::ProjectRevisionConflict,
            "Project revision changed before save.",
            expectedRevision,
            current.value(0).toInt()
        );
    }

    return open(projectId);
}

ProjectRecord ProjectRepository::duplicate(const std::string& projectId)
{
    const ProjectRecord original = open(projectId);

    return create(
        original.snapshot,
        original.metadata.name + " (cópia)"
    );
}

void ProjectRepository::remove(const std::string& projectId)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM projects WHERE id = ?"));
    bindProjectId(query, projectId);

    if (!query.exec()) {
        databaseError(QStringLiteral("Could not delete Project"), query.lastError());
    }

    if (query.numRowsAffected() != 1) {
        throw notFound(projectId);
    }
}

ProjectRecoveryRecord ProjectRepository::stageRecovery(
    const std::string& projectId,
    int baseRevision,
    const ProjectSnapshotCompat& snapshot
)
{
    const QByteArray snapshotJson = serializeProjectSnapshot(snapshot);

    beginImmediate(database_);
    try {
        const ProjectRecord current = open(projectId);
        if (current.metadata.revision != baseRevision) {
            throw ProjectRepositoryError(
                ProjectRepositoryErrorCode::ProjectRevisionConflict,
                "Recovery base revision does not match current Project revision.",
                baseRevision,
                current.metadata.revision
            );
        }

        const auto existing = readRecovery(projectId);
        if (existing) {
            if (
                existing->baseRevision == baseRevision
                && serializeProjectSnapshot(existing->snapshot) == snapshotJson
            ) {
                commit(database_);
                return *existing;
            }

            throw ProjectRepositoryError(
                ProjectRepositoryErrorCode::ProjectRecoveryExists,
                "Project already has a different staged recovery candidate."
            );
        }

        const std::string timestamp = nowIso();
        QSqlQuery insert(database_);
        insert.prepare(
            QStringLiteral(
                "INSERT INTO project_recovery "
                "(project_id, base_revision, project_schema_version, snapshot_json, created_at) "
                "VALUES (?, ?, ?, ?, ?)"
            )
        );
        insert.addBindValue(QString::fromStdString(projectId));
        insert.addBindValue(baseRevision);
        insert.addBindValue(CurrentProjectSchemaVersion);
        insert.addBindValue(snapshotJson);
        insert.addBindValue(QString::fromStdString(timestamp));

        if (!insert.exec()) {
            databaseError(QStringLiteral("Could not stage Project recovery"), insert.lastError());
        }

        commit(database_);

        return ProjectRecoveryRecord{
            .projectId = projectId,
            .baseRevision = baseRevision,
            .projectSchemaVersion = CurrentProjectSchemaVersion,
            .snapshot = deserializeProjectSnapshot(snapshotJson),
            .createdAt = timestamp,
        };
    } catch (...) {
        rollbackNoThrow(database_);
        throw;
    }
}

std::optional<ProjectRecoveryRecord> ProjectRepository::readRecovery(
    const std::string& projectId
) const
{
    QSqlQuery project(database_);
    project.prepare(QStringLiteral("SELECT 1 FROM projects WHERE id = ?"));
    bindProjectId(project, projectId);

    if (!project.exec()) {
        databaseError(QStringLiteral("Could not inspect Project"), project.lastError());
    }

    if (!project.next()) {
        throw notFound(projectId);
    }

    QSqlQuery query(database_);
    query.prepare(
        QStringLiteral(
            "SELECT project_id, base_revision, project_schema_version, "
            "snapshot_json, created_at "
            "FROM project_recovery WHERE project_id = ?"
        )
    );
    bindProjectId(query, projectId);

    if (!query.exec()) {
        databaseError(QStringLiteral("Could not read Project recovery"), query.lastError());
    }

    if (!query.next()) {
        return std::nullopt;
    }

    ProjectSnapshotCompat snapshot = deserializeProjectSnapshot(
        query.value(QStringLiteral("snapshot_json")).toByteArray()
    );

    return ProjectRecoveryRecord{
        .projectId =
            query.value(QStringLiteral("project_id")).toString().toStdString(),
        .baseRevision =
            query.value(QStringLiteral("base_revision")).toInt(),
        .projectSchemaVersion = snapshot.projectSchemaVersion,
        .snapshot = std::move(snapshot),
        .createdAt =
            query.value(QStringLiteral("created_at")).toString().toStdString(),
    };
}

ProjectRecord ProjectRepository::promoteRecovery(
    const std::string& projectId
)
{
    beginImmediate(database_);
    try {
        const ProjectRecord current = open(projectId);
        const auto recovery = readRecovery(projectId);

        if (!recovery) {
            throw ProjectRepositoryError(
                ProjectRepositoryErrorCode::ProjectRecoveryNotFound,
                "Project has no staged recovery candidate."
            );
        }

        if (current.metadata.revision != recovery->baseRevision) {
            throw ProjectRepositoryError(
                ProjectRepositoryErrorCode::ProjectRevisionConflict,
                "Recovery base revision no longer matches current Project revision.",
                recovery->baseRevision,
                current.metadata.revision
            );
        }

        const QByteArray snapshotJson =
            serializeProjectSnapshot(recovery->snapshot);
        const std::string timestamp = nowIso();

        QSqlQuery update(database_);
        update.prepare(
            QStringLiteral(
                "UPDATE projects "
                "SET project_schema_version = ?, revision = ?, snapshot_json = ?, "
                "updated_at = ?, autosaved_at = ? "
                "WHERE id = ? AND revision = ?"
            )
        );
        update.addBindValue(CurrentProjectSchemaVersion);
        update.addBindValue(recovery->baseRevision + 1);
        update.addBindValue(snapshotJson);
        update.addBindValue(QString::fromStdString(timestamp));
        update.addBindValue(QString::fromStdString(timestamp));
        update.addBindValue(QString::fromStdString(projectId));
        update.addBindValue(recovery->baseRevision);

        if (!update.exec()) {
            databaseError(QStringLiteral("Could not promote Project recovery"), update.lastError());
        }

        if (update.numRowsAffected() != 1) {
            throw ProjectRepositoryError(
                ProjectRepositoryErrorCode::ProjectRevisionConflict,
                "Project changed while promoting recovery.",
                recovery->baseRevision
            );
        }

        QSqlQuery remove(database_);
        remove.prepare(
            QStringLiteral(
                "DELETE FROM project_recovery "
                "WHERE project_id = ? AND base_revision = ?"
            )
        );
        remove.addBindValue(QString::fromStdString(projectId));
        remove.addBindValue(recovery->baseRevision);

        if (!remove.exec()) {
            databaseError(QStringLiteral("Could not clear promoted recovery"), remove.lastError());
        }

        if (remove.numRowsAffected() != 1) {
            throw ProjectRepositoryError(
                ProjectRepositoryErrorCode::ProjectRecoveryNotFound,
                "Recovery changed while being promoted."
            );
        }

        commit(database_);
        return open(projectId);
    } catch (...) {
        rollbackNoThrow(database_);
        throw;
    }
}

ProjectRecord ProjectRepository::copyRecovery(const std::string& projectId)
{
    beginImmediate(database_);
    try {
        const ProjectRecord source = open(projectId);
        const auto recovery = readRecovery(projectId);

        if (!recovery) {
            throw ProjectRepositoryError(
                ProjectRepositoryErrorCode::ProjectRecoveryNotFound,
                "Project has no staged recovery candidate."
            );
        }

        ProjectRecord copy = create(
            recovery->snapshot,
            source.metadata.name + " (recuperado)"
        );

        QSqlQuery remove(database_);
        remove.prepare(
            QStringLiteral(
                "DELETE FROM project_recovery WHERE project_id = ?"
            )
        );
        bindProjectId(remove, projectId);

        if (!remove.exec()) {
            databaseError(
                QStringLiteral("Could not clear copied recovery"),
                remove.lastError()
            );
        }

        if (remove.numRowsAffected() != 1) {
            throw ProjectRepositoryError(
                ProjectRepositoryErrorCode::ProjectRecoveryNotFound,
                "Recovery changed while being copied."
            );
        }

        commit(database_);
        return copy;
    } catch (...) {
        rollbackNoThrow(database_);
        throw;
    }
}

void ProjectRepository::discardRecovery(const std::string& projectId)
{
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("DELETE FROM project_recovery WHERE project_id = ?"));
    bindProjectId(query, projectId);

    if (!query.exec()) {
        databaseError(QStringLiteral("Could not discard Project recovery"), query.lastError());
    }

    if (query.numRowsAffected() != 1) {
        const auto project = get(projectId);
        if (!project) {
            throw notFound(projectId);
        }

        throw ProjectRepositoryError(
            ProjectRepositoryErrorCode::ProjectRecoveryNotFound,
            "Project has no staged recovery candidate."
        );
    }
}

} // namespace tcgprint::projects
